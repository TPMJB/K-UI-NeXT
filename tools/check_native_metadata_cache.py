#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check linked immutable metadata publication with a targeted operand-cache model.

Only the isolated metadata and its two neighboring lines have cache tags here.
The original SDK and linked worker C execute in the existing bounded SH model.
This does not model cache capacity/conflicts, CPU cycles, bus latency, IRQ
scheduling, storage transport, a full game, or hardware timing.
"""
import argparse
import copy
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf
from check_vmu_native_readonly import relocated
from profile_vmu_native_release_sh import ReleaseSH
from test_vmu_native_heap_execution import SH, MASK
from check_native_isolation_linked import IsolationWorker, LoanBoundary, poll
from profile_vmu_native_record_worker_sh import Record, fresh_verify, make_handoff, RETURN, OWNER_STACK

LINE = 32
READY = 0x4e4d4331
ONE = (1, 0, 0, 0xfff1)
OBJECTS = dict(config='_config', live='_live_plan', lifetime='_lifetime_plan', hooks='_hook_witness')
SOURCES = ('tools/check_native_metadata_cache.py', 'src/loader/vmu_native_metadata_cache.inc',
    'src/loader/vmu_native_metadata_cache.ld', 'src/loader/vmu_native_passive.c',
    'src/core/vmu_native_live.c', 'include/kui/native_isolation.h',
    'tools/check_native_isolation_linked.py', 'tools/check_vmu_native_worker_gateway.py',
    'tools/check_vmu_native_readonly.py',
    'tools/profile_vmu_native_record_worker_sh.py', 'tools/profile_vmu_native_sdk_worker_sh.py',
    'tools/profile_vmu_native_release_sh.py', 'tests/test_vmu_native_heap_execution.py',
    'tests/test_vmu_native_sdk_poll_execution.py', 'tests/test_native_metadata_cache_model.py')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def physical(address):
    return address & 0x1fffffff


@dataclass
class CacheLine:
    data: bytearray
    dirty: bool


class OperandLines:
    """Explicit dirty/stale tags; P2 backing accesses bypass this object."""
    def __init__(self, raw_block, raw_store):
        self.raw_block, self.raw_store = raw_block, raw_store
        self.lines = {}
        self.fills = self.hits = self.writebacks = 0

    def seed(self, address, data, dirty):
        require(not physical(address) & 31 and len(data) == LINE, 'Bad seeded cache line')
        self.lines[physical(address)] = CacheLine(bytearray(data), dirty)

    def read(self, address, count):
        at = physical(address); line = at & ~31
        require(count > 0 and at + count <= line + LINE, 'Modeled read crosses a line')
        if line not in self.lines:
            self.lines[line] = CacheLine(bytearray(self.raw_block(line, LINE)), False)
            self.fills += 1
        else:
            self.hits += 1
        return int.from_bytes(self.lines[line].data[at-line:at-line+count], 'little')

    def ocbi(self, address):
        self.lines.pop(physical(address) & ~31, None)

    def ocbp(self, address):
        line = physical(address) & ~31
        tagged = self.lines.pop(line, None)
        if tagged is not None and tagged.dirty:
            self.raw_store(line, bytes(tagged.data))
            self.writebacks += 1


def layout(worker):
    symbols = worker[4]
    begin = symbols['__kui_native_metadata_begin'][0]
    end = symbols['__kui_native_metadata_end'][0]
    require(begin & 0xe0000000 == 0xa0000000 and begin < end and not (begin | end) & 31,
            'Metadata group is not exact whole P2 cache lines')
    result = {}; cursor = begin
    for name, symbol in OBJECTS.items():
        lo = symbols['__kui_native_metadata_'+name+'_begin'][0]
        hi = symbols['__kui_native_metadata_'+name+'_end'][0]
        address, size, kind, _ = symbols[symbol]
        require(lo == cursor and not (lo | hi) & 31 and hi > lo and address == lo and
                kind == 1 and 0 < size <= hi-lo and hi-lo-size < LINE,
                'Metadata object is not isolated/padded exactly: '+name)
        result[name] = dict(canonical_p2=hex(address), cached_p1=hex(address-0x20000000),
            p2_begin=hex(lo), p2_end=hex(hi), object_bytes=size,
            padded_bytes=hi-lo, cache_lines=(hi-lo)//LINE)
        cursor = hi
    require(cursor == end, 'Metadata objects do not cover their exact publication group')
    for name, (address, size, kind, _) in symbols.items():
        if kind != 1 or not size or name in OBJECTS.values():
            continue
        lo, hi = physical(address), physical(address)+size
        require(not (lo < physical(end) and physical(begin) < hi),
                'Mutable/other object shares metadata lines: '+name)
    for name in ('_native_metadata_ready', '_hook_witness_count', '_hook_witness_ready', '_state'):
        require(not physical(begin) <= physical(symbols[name][0]) < physical(end),
                'Mutable publication/control state entered metadata lines: '+name)
    require(physical(end) <= physical(symbols['__vmu_native_worker_bss_end'][0]) <=
            physical(symbols['__vmu_native_worker_stack_bottom'][0]), 'Metadata escaped worker BSS')
    return result, begin, end


class PublicationCPU(ReleaseSH):
    """Execute the actual OCBI loop at relocated addresses with dirty tags."""
    def __init__(self, worker, binary, wire, destination, seed=0):
        SH.__init__(self, b'')
        delta = physical(destination)-0x0c100000
        self.symbols = {name: (((value+delta)&MASK if 0x0c100000 <= physical(value) <= 0x0c120000
                               and value & 0xe0000000 in (0x80000000, 0xa0000000) else value),
                              size, kind, owner)
                        for name, (value, size, kind, owner) in worker[4].items()}
        SH.store_block(self, destination, bytes(0x20000))
        SH.store_block(self, destination, relocated(binary, wire, destination))
        self.active = False; self.guest_words = set(); self.stubs = {}; self.macl = 0
        self.private_reads = self.private_writes = self.guest_reads = 0
        self.begin = self.symbols['__kui_native_metadata_begin'][0]
        self.end = self.symbols['__kui_native_metadata_end'][0]
        self.expected = list(range((physical(self.begin)|0x80000000),
                                   (physical(self.end)|0x80000000), LINE))
        self.ready = self.symbols['_native_metadata_ready'][0]
        self.invalidations = []; self.ready_writes = 0; self.p1_stores = 0
        self.cache = OperandLines(self.cache_raw_block, self.cache_raw_store)
        self.fresh = bytes(((i*17+seed*29+3)&255) for i in range(self.end-self.begin))
        SH.store_block(self, self.begin, self.fresh)
        for line in range(physical(self.begin)-LINE, physical(self.end)+LINE, LINE):
            self.cache.seed(line, bytes([(0xa5+seed)&255])*LINE, seed != 1 or line & LINE != 0)
        self.neighbors = {a: copy.deepcopy(self.cache.lines[a]) for a in
                          (physical(self.begin)-LINE, physical(self.end))}

    def cache_raw_block(self,address,count):
        return SH.block(self,address,count)

    def cache_raw_store(self,address,data):
        SH.store_block(self,address,data)

    def write(self, address, value, count=4):
        if physical(self.begin) <= physical(address) < physical(self.end):
            if address & 0xe0000000 == 0x80000000:
                self.p1_stores += 1
            raise ValueError('Publication loop stored into metadata')
        if physical(address) == physical(self.ready):
            require(address & 0xe0000000 == 0xa0000000 and value == READY and
                    self.invalidations == self.expected and
                    SH.block(self,self.begin,self.end-self.begin) == self.fresh,
                    'Ready published before exact OCBI completion or fresh backing was corrupted')
            self.ready_writes += 1
        return super().write(address,value,count)

    def plain(self, op, pc):
        if op & 0xf0ff in (0x0093, 0x00a3):
            at = self.r[op>>8 & 15]
            require(at in self.expected, 'Publication touched a neighboring/unowned cache line')
            if op & 0xf0ff == 0x00a3:
                self.cache.ocbp(at)
                require(SH.block(self,self.begin,self.end-self.begin) == self.fresh,
                        'OCBP wrote stale dirty P1 data over fresh P2 metadata')
                raise ValueError('Publication used OCBP instead of OCBI')
            self.cache.ocbi(at); self.invalidations.append(at)
            self.executing = pc; self.instructions += 1
            return
        return super().plain(op,pc)

    def verify(self):
        self.call('_native_metadata_publish', [], measure=False)
        require(self.ready_writes == 1 and self.invalidations == self.expected and not self.p1_stores,
                'Publication did not retire every exact line before ready')
        for off in range(0,len(self.fresh),4):
            require(self.cache.read((physical(self.begin)|0x80000000)+off,4) ==
                    int.from_bytes(self.fresh[off:off+4],'little'), 'P1 view retained stale data')
        require(all(self.cache.lines[a] == line for a,line in self.neighbors.items()),
                'Publication modified a neighboring dirty cache line')
        require(self.cache.writebacks == 0, 'Publication wrote cache contents back')
        # A later game invalidation discards only clean read-only copies.
        for at in self.expected:
            require(not self.cache.lines[physical(at)].dirty, 'Read view became dirty')
            self.cache.ocbi(at)
        require(SH.block(self,self.begin,len(self.fresh)) == self.fresh,
                'Cold invalidation changed authoritative metadata')
        return dict(destination=hex(physical(self.begin)-0x0c100000+0xac100000),
                    lines=len(self.expected), stale_seed=self.fresh[0],
                    ready_after_all_invalidations=True, dirty_writebacks=0,
                    neighbors_preserved=True, cold_read_views_match_p2=True)


class CachedSDK(IsolationWorker):
    """Targeted metadata cache over actual linked C + original SDK execution."""
    def __init__(self,*args,**kwargs):
        self.cache = None
        super().__init__(*args,**kwargs)
        self.begin = self.symbols['__kui_native_metadata_begin'][0]
        self.end = self.symbols['__kui_native_metadata_end'][0]
        self.ready = self.symbols['_native_metadata_ready'][0]
        self.expected = list(range(physical(self.begin)|0x80000000,
                                   physical(self.end)|0x80000000, LINE))
        self.cache = OperandLines(self.cache_raw_block,self.cache_raw_store)
        for at in range(physical(self.begin)-LINE, physical(self.end)+LINE, LINE):
            self.cache.seed(at,bytes([0xa5])*LINE,True)
        self.neighbors = {a:copy.deepcopy(self.cache.lines[a]) for a in
                          (physical(self.begin)-LINE,physical(self.end))}
        self.invalidations = []; self.publish_count = 0
        self.p1_reads = []; self.p1_stores = 0; self.p2_stores_after_ready = 0
        self.live_return = None; self.live_successes = 0
        self.publication_snapshot = None; self.prepare_snapshot = None
        self.raw_control_reads = set()
        self.ready_p2_reads = 0

    def cache_raw_block(self,address,count):
        return SH.block(self,address,count)

    def cache_raw_store(self,address,data):
        SH.store_block(self,address,data)

    def worker(self,name,args):
        previous=getattr(self,'current_worker',None);self.current_worker=name
        try:
            return super().worker(name,args)
        finally:
            self.current_worker=previous

    def inside_metadata(self,address):
        return physical(self.begin) <= physical(address) < physical(self.end)

    def read(self,address,count=4):
        if self.cache is not None and physical(address)==physical(self.ready) and getattr(self,'inside_worker',False):
            require(address & 0xe0000000==0xa0000000,'Metadata eligibility gate read through P1')
            self.ready_p2_reads += 1
        if self.cache is not None and self.inside_metadata(address):
            if address & 0xe0000000 == 0x80000000:
                require(SH.read(self,self.ready) == READY and self.publish_count == 1,
                        'P1 metadata read occurred before successful publication')
                if getattr(self,'inside_worker',False):
                    self.p1_reads.append(dict(pc=self.pc,address=address,width=count,
                                             opcode=SH.read(self,self.pc,2),worker=self.current_worker))
                return self.cache.read(address,count)
            if self.publish_count and getattr(self,'inside_worker',False):
                self.raw_control_reads.add(physical(address))
        return super().read(address,count)

    def write(self,address,value,count=4):
        if self.cache is not None and self.inside_metadata(address):
            require(address & 0xe0000000 == 0xa0000000, 'P1 store into read-only metadata view')
            if self.publish_count:
                self.p2_stores_after_ready += 1
                raise ValueError('P2 metadata changed after publication')
        if self.cache is not None and physical(address) == physical(self.ready) and value == READY:
            require(self.live_successes == 1 and self.invalidations == self.expected and
                    self.prepare_snapshot == SH.block(self,self.begin,self.end-self.begin),
                    'Metadata ready preceded successful live_prepare commit/exact invalidation')
            require(all(self.cache.lines[a] == line for a,line in self.neighbors.items()),
                    'Publication touched a mutable neighboring cache line')
            self.publish_count += 1
            self.publication_snapshot = SH.block(self,self.begin,self.end-self.begin)
        return super().write(address,value,count)

    def plain(self,op,pc):
        if op & 0xf0ff == 0x0093:
            at = self.r[op>>8 & 15]
            require(at in self.expected and not self.publish_count and self.live_successes == 1,
                    'OCBI occurred outside exact post-prepare metadata publication')
            self.cache.ocbi(at);self.invalidations.append(at)
            self.executing=pc;self.instructions+=1
            return
        return super().plain(op,pc)

    def step(self):
        if self.cache is not None:
            if self.pc == self.symbols['_kui_vmu_native_live_prepare'][0]:
                require(self.live_return is None and not self.publish_count,
                        'Live preparation repeated after metadata publication')
                self.live_return = self.pr
            elif self.live_return is not None and self.pc == self.live_return:
                if self.r[0] == 0:
                    self.live_successes += 1
                    self.prepare_snapshot = SH.block(self,self.begin,self.end-self.begin)
                self.live_return = None
        return super().step()


def refused(name, run):
    try:
        run()
    except (AssertionError,ValueError) as error:
        return dict(case=name,refused=True,reason=str(error))
    raise ValueError('Mutation was accepted: '+name)


def publication_cases(worker,binary,wire):
    positives = [PublicationCPU(worker,binary,wire,base,seed).verify()
                 for base in (0xac100000,0xac300000,0xac700000) for seed in (0,1,2)]
    cpu = PublicationCPU(worker,binary,wire,0xac300000)
    address,size,*_ = cpu.symbols['_native_metadata_publish']
    candidates = [pc for pc in range(address,address+size,2)
                  if SH.read(cpu,pc,2)&0xf0ff == 0x0093]
    require(len(candidates)==1,'Publisher lacks its one explicit OCBI loop instruction')
    pc=candidates[0];word=SH.read(cpu,pc,2)
    negatives=[]
    for name,changed in [('OCBI-replaced-by-OCBP',word^0x30),('OCBI-omitted',0x0009)]:
        trial=PublicationCPU(worker,binary,wire,0xac300000)
        SH.write(trial,pc,changed,2)
        negatives.append(refused(name,trial.verify))
    end_literals=[at for at in range((address+3)&~3,address+size-3,4)
                  if SH.read(cpu,at)==cpu.end]
    require(len(end_literals)==1,'Publisher end boundary is not one exact linked literal')
    trial=PublicationCPU(worker,binary,wire,0xac300000)
    SH.write(trial,end_literals[0],trial.end+LINE)
    negatives.append(refused('publication-end-includes-neighbor',trial.verify))
    return positives,negatives


def sdk_cases(startup,record_path,build):
    source,image,extent=fresh_verify(startup,record_path)
    record=Record(record_path.read_bytes(),image,source)
    cpu=CachedSDK(startup,build,make_handoff(),record,'metadata_cache',True,
                  cached_metadata=True)
    cpu.prepare();initializer=cpu.initialize_sdk()
    require(cpu.publish_count==0 and not cpu.p1_reads,'Metadata cached before live binding')
    cpu.seed_tick_boundary()
    prebinding=copy.deepcopy(cpu)
    rounds=[]
    for index in range(8):
        if index in (3,6):
            for at in cpu.expected:cpu.cache.ocbi(at)
        rounds.append(poll(cpu))
    require(cpu.live_successes==cpu.publish_count==1 and cpu.invalidations==cpu.expected and
            cpu.p1_reads and not cpu.p1_stores and not cpu.p2_stores_after_ready,
            'Actual SDK did not use one immutable cache publication')
    require(cpu.cache.writebacks==0 and
            SH.block(cpu,cpu.begin,cpu.end-cpu.begin)==cpu.publication_snapshot,
            'Actual SDK cache model changed immutable backing bytes')
    rejected=copy.deepcopy(prebinding)
    rejected.write(record.layout[60],rejected.read(record.layout[60])^32)
    failed_prepare=refused('first-live-prepare-input-invalid',rejected.submit)
    require(failed_prepare['reason'].startswith('Actual worker refused with code ') and
            rejected.publish_count==0 and not rejected.invalidations and not rejected.p1_reads and
            SH.read(rejected,rejected.ready)==0,
            'Failed first binding published/selected a cached metadata view')
    for name,offsets in [('lifetime',(0,4,8)),('live',(0,4,64))]:
        start=cpu.symbols[OBJECTS[name]][0]
        require(all(physical(start+off) in cpu.raw_control_reads for off in offsets),
                'Canonical P2 identity/control checks disappeared: '+name)
    forced=[]
    for command in (1,2,10,11):
        trial=copy.deepcopy(cpu);trial.inject_command=command
        try:
            poll(trial,consume=False)
        except LoanBoundary:
            require(command==11 and trial.calls['actual_low_sd_loan_boundary']==1 and
                    trial.block(trial.injected_rx,4)==bytes([0xa5])*4,
                    'Unexpected/unpublished storage boundary in the cache model')
            forced.append(dict(command=command,actual_sd_loan_entry_reached=True,
                               successful_storage_result_substituted=False,rx_unpublished=True))
            continue
        require(command!=11 and trial.read(trial.injected_rx)&255=={1:5,2:6,10:8}[command],
                'Cached-metadata forced packet did not execute its real protocol path')
        require(not trial.p1_stores and not trial.p2_stores_after_ready,
                'Forced protocol packet wrote immutable metadata')
        forced.append(dict(command=command,actual_reply_code=trial.read(trial.injected_rx)&255,
                           metadata_stores=0))
    # Enter the genuine original completion wrapper before mutating only live
    # SDK/heap/MMIO witnesses. Deep copies keep dirty/cache tags independent.
    cpu.submit();r=record
    cpu.pc,cpu.pr,cpu.r[15],cpu.sr=r.completion_wrapper,RETURN,OWNER_STACK,cpu.owner_sr
    cpu.until(cpu.read(r.driver[12]));cpu.frame('cache_negative_baseline')
    manager=cpu.read(r.driver[9]);hooks=cpu.symbols['_hook_witness'][0]
    count=cpu.read(cpu.symbols['_hook_witness_count'][0])
    hook_address=cpu.read(hooks+(count-1)*12)
    identity=cpu.read(cpu.symbols['_lifetime_plan'][0]+40)
    cases=[('live-manager-identity',r.driver[9],manager+32),
           ('live-bank-selector',manager+0x10b4,cpu.read(manager+0x10b4)^1),
           ('live-initializer-input',r.layout[60],cpu.read(r.layout[60])^32),
           ('live-timer-callback',r.layout[57],0xec000000),
           ('live-hook-word',hook_address,cpu.read(hook_address)^4),
           ('live-heap-identity',identity,cpu.read(identity)^4),
           ('live-MDST-active',0xa05f6c18,1)]
    negatives=[]
    for name,address,value in cases:
        trial=copy.deepcopy(cpu)
        # Latch a contradictory MMIO observation directly. Writing MDST=1
        # through the hardware model would start and immediately complete a
        # synchronous DMA fixture, clearing the fault before C can sample it.
        if address==0xa05f6c18:trial.mmio[address]=value
        else:trial.write(address,value)
        before_tx=trial.block(r.tx,0x6000);before_rx=trial.rx_snapshot()
        result=refused(name,lambda:trial.worker('_kui_vmu_native_passive_complete',[trial.frame_address]))
        require(result['reason'].startswith('Actual worker refused with code '),
                'Live witness failed in the model instead of an actual C guard: '+name)
        require(trial.block(r.tx,0x6000)==before_tx and trial.rx_snapshot()==before_rx,
                'Refused live witness published TX/RX changes: '+name)
        result.update(address=hex(address),tx_or_rx_changes=False)
        negatives.append(result)
    # Fault setup deliberately bypasses the normal immutable-write assertion.
    # Warm P1 copies keep the valid old controls; actual canonical P2 checks
    # must nevertheless see and refuse the changed self/ready/count words.
    lifetime=cpu.symbols['_lifetime_plan'][0];live=cpu.symbols['_live_plan'][0]
    controls=[('lifetime-self',lifetime,cpu.read(lifetime)^32),
              ('lifetime-ready',lifetime+4,0),('lifetime-count',lifetime+8,65),
              ('live-self',live,cpu.read(live)^32),('live-ready',live+4,0),
              ('live-input-count',live+64,25),
              ('hook-ready',cpu.symbols['_hook_witness_ready'][0],0),
              ('hook-count',cpu.symbols['_hook_witness_count'][0],33)]
    control_negatives=[]
    for name,address,value in controls:
        trial=copy.deepcopy(cpu)
        if trial.inside_metadata(address):trial.cache.read(physical(address)|0x80000000,4)
        SH.write(trial,address,value)
        before_tx=trial.block(r.tx,0x6000);before_rx=trial.rx_snapshot()
        result=refused(name,lambda:trial.worker('_kui_vmu_native_passive_complete',[trial.frame_address]))
        require(result['reason'].startswith('Actual worker refused with code '),
                'Canonical control was not refused by an actual guard: '+name)
        require(trial.block(r.tx,0x6000)==before_tx and trial.rx_snapshot()==before_rx,
                'Canonical-control refusal published TX/RX changes: '+name)
        result.update(address=hex(address),injected_after_warm_p1=True,tx_or_rx_changes=False)
        control_negatives.append(result)
    # Turn an executed common-guard metadata load into its store counterpart.
    # The effective address remains identical, so the read-only-view assertion
    # must catch the actual SH store before it can dirty a cached line.
    chosen=None
    for event in cpu.p1_reads:
        if event['worker']!='_kui_vmu_native_passive_complete' or event['width']!=4:
            continue
        pc,op=event['pc'],event['opcode']
        common=any(cpu.symbols.get(name,(0,0))[0]<=pc<sum(cpu.symbols.get(name,(0,0))[:2])
                   for name in ('_installed_hooks_valid','_poll_lease_valid'))
        if not common:continue
        n,m=op>>8&15,op>>4&15
        if op&0xf00f==0x6002:changed=0x2002|(m<<8)|(n<<4)
        elif op>>12==5:changed=0x1000|(m<<8)|(n<<4)|(op&15)
        elif op&0xf00f==0x000e:changed=0x0006|(m<<8)|(n<<4)
        else:continue
        chosen=(pc,changed);break
    require(chosen is not None,'No executed common-guard P1 load available for store mutation')
    trial=copy.deepcopy(cpu);SH.write(trial,chosen[0],chosen[1],2)
    store_refusal=refused('P1-metadata-load-replaced-by-store',
                         lambda:trial.worker('_kui_vmu_native_passive_complete',[trial.frame_address]))
    require('P1 store into read-only metadata view' in store_refusal['reason'],
            'P1-store mutation failed before reaching its actual metadata store')
    return dict(polls=8,sdk_initializer=initializer,publication_count=cpu.publish_count,
        actual_live_prepare_successes=cpu.live_successes,metadata_ocbi_lines=len(cpu.invalidations),
        p1_metadata_reads=len(cpu.p1_reads),cache_fills=cpu.cache.fills,
        canonical_p2_readiness_gate_reads=cpu.ready_p2_reads,
        p1_metadata_stores=cpu.p1_stores,p2_metadata_stores_after_publication=cpu.p2_stores_after_ready,
        cache_writebacks=cpu.cache.writebacks,cold_read_view_evictions=2,
        real_virtual_a2=True,controller_tick_calls=cpu.tick_calls,rounds=rounds,
        failed_prepare_refusal=failed_prepare,
        forced_packets=forced,
        live_witness_refusals=negatives,canonical_p2_control_refusals=control_negatives,
        readonly_store_mutation_refusal=store_refusal,input_extent=extent)


def check(args):
    build=args.build.resolve();worker=read_elf(build/'native-worker.elf')
    groups,begin,end=layout(worker)
    marker='__kui_native_metadata_cache_isolation_'
    require(worker[4].get(marker+'worker')==ONE,'Missing compiled metadata-cache worker marker')
    artifacts={name:sha((build/name).read_bytes()) for name in
               ('native-worker.elf','native-worker.bin','native-worker.reloc')}
    if not args.publication_only:
        for name in ('stage.elf','entry.elf'):
            artifacts[name]=sha((build/name).read_bytes())
        require(read_elf(build/'stage.elf')[4].get(marker+'stage')==ONE,
                'Missing paired stage marker')
        require(args.source_commit and len(args.source_commit)==40 and
                args.source_commit[:12].encode() in (build/'entry.elf').read_bytes(),
                'Final entry does not bind the exact runtime freeze')
    binary=(build/'native-worker.bin').read_bytes();wire=(build/'native-worker.reloc').read_bytes()
    positives,mutations=publication_cases(worker,binary,wire)
    sdk=None if args.publication_only else sdk_cases(args.startup,args.record,build)
    return dict(schema='kui.native.metadata-cache-publication.v1',
        status='PARTIAL' if args.publication_only else 'PASS',source_commit=args.source_commit,
        paired_markers=not args.publication_only,artifact_sha256=artifacts,
        source_sha256={name:sha((ROOT/name).read_bytes()) for name in SOURCES},
        metadata_layout=groups,metadata_bytes=end-begin,whole_cache_line_isolation=True,
        cache_publication=dict(ocbi_only=True,no_ocbp=True,exact_range=True,
            ready_after_all_invalidations=True,published_after_live_prepare=sdk is not None),
        read_only_p1_views=sdk is not None,no_p1_metadata_stores=sdk is not None,
        coherent_live_witnesses=sdk is not None,
        publication_cases=positives,publication_mutation_refusals=mutations,
        actual_sdk=sdk,scope=__doc__.strip(),
        limitations=['Metadata operand-line states only; no cache-capacity/conflict or bus model.',
            'SDK registration scaffolding and assembly save/restore bridges use existing bounded models.',
            'Live values are mutated after warm publication and must be refused by actual linked C before TX/RX publication.',
            'No SD transfer, durable-write success, full game, FMV, hardware IRQ schedule or performance result is modeled.'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--startup',type=Path)
    p.add_argument('--record',type=Path,default=ROOT/'tests/fixtures/native-cache/evolution1.nvr')
    p.add_argument('--source-commit')
    p.add_argument('--publication-only',action='store_true',help='Pilot publisher-only receipt; never final PASS')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    require(a.publication_only or a.startup is not None,'Final proof requires the original executable')
    r=check(a);a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps(dict(schema=r['schema'],status=r['status'],output=str(a.output),
                         metadata_bytes=r['metadata_bytes'],publication_cases=len(r['publication_cases']))))


if __name__=='__main__':main()
