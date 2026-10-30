#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the split controller lane, its retained C baseline and refusals.

Original SDK producer/consumer instructions and linked worker C execute in
flat coherent RAM. Maple DMA is synchronous authored hardware. The baseline
selects the retained old full C submit entry at the modeled assembly bridge;
no worker helper or SDK function returns a substituted success. Instruction
counts are interpreted C-prefix counts, not hardware cycles or deadlines.
"""
import argparse
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_native_isolation_linked import IsolationWorker,poll,LoanBoundary
from profile_vmu_native_record_worker_sh import Record,fresh_verify,make_handoff
from test_vmu_native_heap_execution import SH, MASK

SCHEMA='kui.native.split-controller-execution.v1'
SOURCES=('tools/check_native_split_controller.py','tools/check_native_isolation_linked.py',
    'tools/profile_vmu_native_record_worker_sh.py','tools/profile_vmu_native_sdk_worker_sh.py',
    'tests/test_vmu_native_sdk_poll_execution.py','src/loader/vmu_native_passive.c',
    'src/loader/vmu_native_split_controller.inc',
    'src/loader/vmu_native_passive_hooks.S','src/core/vmu_native_live.c',
    'src/core/vmu_native_lifetime.c','include/kui/native_isolation.h')


def require(ok,message):
    if not ok:raise ValueError(message)


def sha(data):return hashlib.sha256(data).hexdigest()


class FallbackBoundary(Exception):pass


class SplitSDK(IsolationWorker):
    def __init__(self,*args,**kwargs):
        self.baseline=False;self.observe_fallback=False;self.fallback_observed=False
        self.inject_on_complete=None;self.worker_runs=[];self.external_c_stores=[];self.mdst_writes=0
        self.inject_on_submit=None
        self.injected_rx_snapshot=None;self.mutation_rx_snapshot=None
        super().__init__(*args,**kwargs)

    def worker(self,name,args):
        if name=='_kui_vmu_native_passive_split_submit' and self.inject_on_submit:
            mutation=self.inject_on_submit;self.inject_on_submit=None;mutation(self)
            self.mutation_rx_snapshot=self.rx_snapshot()
        if name=='_kui_vmu_native_passive_complete' and self.inject_on_complete:
            mutation=self.inject_on_complete;self.inject_on_complete=None;mutation(self)
            self.mutation_rx_snapshot=self.rx_snapshot()
        if self.baseline and name=='_kui_vmu_native_passive_split_submit':
            name='_kui_vmu_native_passive_submit'
        ready=self.symbols.get('_live_plan',(None,))[0]
        ready_before=self.read(ready+4) if ready is not None else None
        result=super().worker(name,args)
        self.worker_runs.append(dict(function=name,instructions=result['instructions'],
            live_ready_before=ready_before,live_ready_after=self.read(ready+4) if ready is not None else None,
            capture_tag=self.read(self.symbols['_controller_pair'][0])))
        return result

    def step(self):
        if (self.observe_fallback and getattr(self,'inside_worker',False) and
                self.pc==self.symbols['_kui_vmu_native_passive_submit'][0]):
            self.fallback_observed=True
            raise FallbackBoundary
        return super().step()

    def inject_packet(self,command):
        super().inject_packet(command)
        self.injected_rx_snapshot=self.rx_snapshot()

    def write(self,address,value,count=4):
        if address==0xa05f6c18 and count==4 and value==1:self.mdst_writes+=1
        if (getattr(self,'inside_worker',False) and getattr(self,'active',False) and
                not(self.worker_physical_low<=address&0x1fffffff<self.worker_physical_high)):
            self.external_c_stores.append((address,count,value&((1<<(8*count))-1)))
        return super().write(address,value,count)

    def reset_observations(self):
        self.worker_runs=[];self.external_c_stores=[];self.mdst_writes=0


def fresh(args, *, general=False):
    source,image,extent=fresh_verify(args.startup,args.record)
    record=Record(args.record.read_bytes(),image,source)
    cpu=SplitSDK(args.startup,args.build,make_handoff(),record,'split_controller',not general,split_controller=True)
    cpu.prepare();initializer=cpu.initialize_sdk();cpu.seed_tick_boundary()
    cpu.reset_observations()
    return cpu,source,extent,initializer


def counters(cpu):return cpu.c_callback_calls.copy()


def parent_store_only(cpu):
    stores=cpu.external_c_stores
    rx={address+2 for address in cpu.record.rx_slots if address}
    return len(stores)==1 and stores[0][0]&0x1fffffff in rx and stores[0][1]==1


def compare_polls(pristine,count):
    candidate=copy.deepcopy(pristine);baseline=copy.deepcopy(pristine);baseline.baseline=True
    rows=[]
    for index in range(count):
        candidate.reset_observations();baseline.reset_observations()
        before=counters(candidate)
        b=poll(baseline);c=poll(candidate)
        delta=counters(candidate)-before
        captured=delta.get('submit',0)==0
        rx_equal=candidate.rx_snapshot()==baseline.rx_snapshot()
        packets_equal=candidate.batch['frames']==baseline.batch['frames']
        require(rx_equal and packets_equal,'Split lane changed actual SDK RX bytes or physical packets')
        require(delta.get('split_submit',0)==delta.get('complete',0)==1,
                'SDK poll did not traverse both actual split/complete C entries')
        if captured:
            require(c['submit_instructions']<b['submit_instructions'] and parent_store_only(candidate),
                    'Captured poll did not reduce its C prefix or wrote beyond one parent byte')
        submit_run=candidate.worker_runs[0]
        cold_info=any(int(frame['header'],16)==0x2001 for frame in candidate.batch['frames'])
        rows.append(dict(round=index+1,cold=index==0,captured=captured,
            tiny_submit_calls=delta.get('split_submit',0),full_submit_fallback_calls=delta.get('submit',0),
            baseline_submit_instructions=b['submit_instructions'],candidate_submit_instructions=c['submit_instructions'],
            baseline_complete_instructions=b['complete_instructions'],candidate_complete_instructions=c['complete_instructions'],
            rx_equal=rx_equal,physical_packets_equal=packets_equal,
            parent_byte_only=parent_store_only(candidate) if captured else None,
            cold_physical_info=cold_info,submit_capture_tag=submit_run['capture_tag'],
            live_ready_before_submit=submit_run['live_ready_before'],
            live_ready_after_submit=submit_run['live_ready_after'],
            rx_sha256=sha(candidate.rx_snapshot()),physical_packets=candidate.batch['frames']))
    require(rows[0]['captured'] and any(row['captured'] for row in rows[1:]),
            'Cold INFO or steady controller poll missed the split lane')
    require(rows[0]['cold_physical_info'] and rows[0]['submit_capture_tag']==2 and
            rows[0]['live_ready_before_submit']==rows[0]['live_ready_after_submit']==0,
            'Cold INFO was not captured before lazy completion preparation')
    require(candidate.tick_calls==0 and candidate.overlay_stats()['advertisements']>0,
            'Split workload lost virtual A2 or called controller TICK')
    return rows,candidate


def all_virtual_fallbacks(pristine):
    rows=[]
    for command in range(256):
        cpu=copy.deepcopy(pristine);cpu.observe_fallback=True;cpu.inject_command=command
        pair=cpu.symbols['_controller_pair'][0]
        before_pair=cpu.block(pair,28)+cpu.block(pair+156,32)
        before_state=cpu.block(cpu.symbols['_state'][0],cpu.symbols['_state'][1])
        before_overlay=cpu.block(cpu.symbols['_overlay'][0],cpu.symbols['_overlay'][1])
        try:cpu.submit()
        except FallbackBoundary:pass
        else:raise ValueError('Virtual command failed to reach the retained full submit entry: '+str(command))
        require(cpu.fallback_observed and cpu.read(pair)!=2 and cpu.mdst_writes==0 and
                cpu.injected_rx_snapshot is not None and cpu.rx_snapshot()==cpu.injected_rx_snapshot,
                'Virtual fallback committed raw capture, started DMA, or published RX')
        require(cpu.block(pair,28)+cpu.block(pair+156,32)==before_pair and
                cpu.block(cpu.symbols['_state'][0],cpu.symbols['_state'][1])==before_state and
                cpu.block(cpu.symbols['_overlay'][0],cpu.symbols['_overlay'][1])==before_overlay,
                'Virtual fallback changed published state beyond authorized snapshot scratch')
        rows.append(dict(command=command,actual_full_submit_entry=True,raw_capture_committed=False,
                         before_mdst=True,storage_success_substituted=False,other_published_state_unchanged=True))
    return rows


def authored_cold_allinfo(pristine):
    """Change only the authored A main-unit command after SDK construction."""
    def author(cpu):
        at=cpu.mmio[0xa05f6c04]
        for _ in range(32):
            control=cpu.read(at)
            if control==0x700:at+=4;continue
            header=cpu.read(at+8)
            if header==0x2001:
                raw_write(cpu,at+8,0x2002);return
            if control&0x80000000:break
            at+=12+4*(control&255)
        raise ValueError('Authored cold ALLINFO lacks the actual SDK A INFO packet')
    candidate=copy.deepcopy(pristine);baseline=copy.deepcopy(pristine);baseline.baseline=True
    for cpu in (candidate,baseline):cpu.reset_observations();cpu.inject_on_submit=author
    before=counters(candidate)
    b=poll(baseline);c=poll(candidate);delta=counters(candidate)-before
    first=candidate.worker_runs[0];last=candidate.worker_runs[-1]
    rx_equal=candidate.rx_snapshot()==baseline.rx_snapshot()
    packets_equal=candidate.batch['frames']==baseline.batch['frames']
    require(delta.get('split_submit',0)==delta.get('complete',0)==1 and delta.get('submit',0)==0 and
            first['capture_tag']==2 and first['live_ready_before']==first['live_ready_after']==0 and
            last['live_ready_after'] and rx_equal and packets_equal and parent_store_only(candidate) and
            c['submit_instructions']<b['submit_instructions'] and
            any(int(row['header'],16)==0x2002 for row in candidate.batch['frames']),
            'Authored cold ALLINFO did not capture, bind at completion and preserve the real C/RX result')
    return dict(authored_packet=True,natural_sdk_producer_command=False,command=2,
        tiny_submit_calls=1,full_submit_fallback_calls=0,complete_calls=1,submit_capture_tag=2,
        live_ready_before_submit=0,live_ready_after_submit=0,live_ready_after_complete=last['live_ready_after'],
        baseline_submit_instructions=b['submit_instructions'],candidate_submit_instructions=c['submit_instructions'],
        rx_equal=rx_equal,physical_packets_equal=packets_equal,parent_byte_only=True,
        rx_sha256=sha(candidate.rx_snapshot()))


def raw_write(cpu,address,value,width=4):SH.write(cpu,address,value,width)


def mutation_cases(pristine,warm):
    rows=[]
    pair=warm.symbols['_controller_pair'][0]
    live=warm.symbols['_live_plan'][0];life=warm.symbols['_lifetime_plan'][0]
    hooks=warm.symbols['_hook_witness'][0]
    manager=warm.record.driver[10]
    cases=[
        ('capture-tag-idle','capture',lambda c:raw_write(c,pair,0)),
        ('capture-tag-prepared','capture',lambda c:raw_write(c,pair,1)),
        ('capture-tag','capture',lambda c:raw_write(c,pair,3)),
        ('capture-words','capture',lambda c:raw_write(c,pair+12,33)),
        ('capture-bank','capture',lambda c:raw_write(c,pair+4,c.read(pair+4)^1)),
        ('capture-word','capture',lambda c:raw_write(c,pair+28,c.read(pair+28)^1)),
        ('capture-source','source',lambda c:raw_write(c,pair+8,c.read(pair+8)+32)),
        ('issued-tx-word','source',lambda c:raw_write(c,c.read(pair+8),c.read(c.read(pair+8))^1)),
        ('MDSTAR','context',lambda c:c.mmio.__setitem__(0xa05f6c04,c.mmio[0xa05f6c04]+32)),
        ('post-selector','context',lambda c:raw_write(c,manager+0x10b4,c.read(manager+0x10b4)^1)),
        ('SDK-busy','context',lambda c:raw_write(c,manager+0xfc0,1)),
        ('installed-hook','hooks',lambda c:raw_write(c,c.read(hooks),c.read(c.read(hooks))^4)),
        ('hook-ready','hooks',lambda c:raw_write(c,c.symbols['_hook_witness_ready'][0],0)),
        ('heap-identity','heap',lambda c:raw_write(c,c.read(life+40),c.read(c.read(life+40))^32)),
        ('TX-base','tx_metadata',lambda c:raw_write(c,manager+0xfc4,c.read(manager+0xfc4)+32)),
        ('initializer-input','tx_metadata',lambda c:raw_write(c,c.read(live+68),c.read(c.read(live+68))^4)),
        ('timer-callback','tx_metadata',lambda c:raw_write(c,c.read(live+56),0x8c004000)),
        ('named-RX-slot','rx_slot',lambda c:raw_write(c,manager+0xfe8+c.read(pair+4)*96,
                                                    c.read(manager+0xfe8+c.read(pair+4)*96)+32)),
        ('parent-RX-header','rx_slot',lambda c:raw_write(c,c.record.rx_slots[c.read(pair+4)*24],
                                                        c.read(c.record.rx_slots[c.read(pair+4)*24])^1)),
    ]
    for name,category,mutation in cases:
        cpu=copy.deepcopy(warm);cpu.reset_observations();cpu.submit()
        require(cpu.read(pair)==2,'Mutation setup did not capture a controller poll')
        before=cpu.rx_snapshot();cpu.inject_on_complete=mutation
        try:cpu.complete()
        except AssertionError as error:
            require(str(error).startswith('Actual worker refused with code '),
                    'Mutation stopped outside actual linked C refusal: '+name+': '+str(error))
        else:raise ValueError('Mutation was accepted before parent publication: '+name)
        require(cpu.mutation_rx_snapshot is not None and cpu.rx_snapshot()==cpu.mutation_rx_snapshot and
                not cpu.external_c_stores,
                'Refused mutation published RX or another external C store: '+name)
        rows.append(dict(name=name,category=category,refused=True,before_parent_store=True,
            rx_unchanged=True,actual_c_refusal=True))
    # Mutate at the real C boundary after SDK packet construction, so every
    # refusal executes linked admission rather than failing fixture setup.
    cold_cases=[
        ('cold-descriptor-RX-slot',lambda c:raw_write(c,c.mmio[0xa05f6c04]+4,
                                                     c.read(c.mmio[0xa05f6c04]+4)+32)),
        ('cold-RX-lease-overlap',lambda c:raw_write(c,c.mmio[0xa05f6c04]+4,c.actual_heap_payload&0x1fffffff)),
        ('cold-RX-alias',lambda c:raw_write(c,c.mmio[0xa05f6c04]+4,c.read(c.mmio[0xa05f6c04]+4)|0x80000000)),
        ('cold-TX-bank',lambda c:c.mmio.__setitem__(0xa05f6c04,c.mmio[0xa05f6c04]+32)),
        ('cold-TX-alias',lambda c:c.mmio.__setitem__(0xa05f6c04,c.mmio[0xa05f6c04]|0xa0000000)),
        ('cold-MMIO-base',lambda c:raw_write(c,manager+0x10f4,0xa05f6c04)),
        ('cold-manager',lambda c:raw_write(c,c.record.driver[9],manager+4)),
        ('cold-lock-pointer',lambda c:raw_write(c,c.record.driver[11],c.read(c.record.driver[11])+4)),
        ('cold-lock-byte',lambda c:raw_write(c,c.read(c.record.driver[11]),0,1)),
        ('cold-MDTSEL',lambda c:c.mmio.__setitem__(0xa05f6c10,1)),
        ('cold-MDEN',lambda c:c.mmio.__setitem__(0xa05f6c14,0)),
        ('cold-MDST-busy',lambda c:c.mmio.__setitem__(0xa05f6c18,1)),
    ]
    for name,mutation in cold_cases:
        cpu=copy.deepcopy(pristine);cpu.reset_observations();cpu.inject_on_submit=mutation
        try:cpu.submit()
        except AssertionError as error:
            require(str(error).startswith('Actual worker refused with code '),'Cold input missed C refusal: '+name)
        else:raise ValueError('Cold invalid preflight input was admitted: '+name)
        require(cpu.mdst_writes==0 and cpu.mutation_rx_snapshot is not None and
                cpu.rx_snapshot()==cpu.mutation_rx_snapshot and not cpu.external_c_stores,
                'Cold preflight refusal started DMA or published RX: '+name)
        rows.append(dict(name=name,category='cold_slot',refused=True,before_parent_store=True,
            rx_unchanged=True,actual_c_refusal=True,before_mdst=True))
    # Cold response checks run before the first lazy phase1 publication.
    for name,category in (('cold-parent-controller-function','rx_slot'),):
        cpu=copy.deepcopy(pristine);cpu.reset_observations()
        cpu.submit()
        def mutate_function(c):
            rx=c.record.rx_slots[c.read(pair+4)*24];raw_write(c,rx+4,0x02000000)
        cpu.inject_on_complete=mutate_function
        try:cpu.complete()
        except AssertionError as error:
            require(str(error).startswith('Actual worker refused with code '),'Cold response missed C refusal')
        else:raise ValueError('Cold INFO non-controller response was admitted')
        expected=cpu.mutation_rx_snapshot
        require(expected is not None and cpu.rx_snapshot()==expected and not cpu.external_c_stores,
                'Cold refusal published RX')
        rows.append(dict(name=name,category=category,refused=True,before_parent_store=True,
            rx_unchanged=True,actual_c_refusal=True,before_mdst=False))
    return rows


def general_counter_cases(args):
    """Age only private pair counters; execute the original SDK and linked C.

    Each fixture starts with the real allocator, arm and SDK initializer. The
    natural cold controller poll and authored virtual INFO force the separate
    split/full submission paths. No successful storage reply is substituted.
    """
    args.build=args.build.resolve()
    pristine,source,extent,initializer=fresh(args,general=True)
    require(pristine.general,'Counter proof requires the explicit lean General profile')
    require(pristine.symbols.get('__kui_native_general_unbounded_pairs_worker')==(1,0,0,0xfff1),
            'Counter proof lacks the General uncapped-pair policy marker')
    provenance=json.loads((args.build/'entry.compile.json').read_text())
    require(provenance['commit']==args.source_commit,'Counter proof source commit differs from compile receipt')
    build_id=args.build_id or args.source_commit[:12]
    require(len(build_id)==12 and all(c in '0123456789abcdef' for c in build_id) and
            build_id.encode() in (args.build/'entry.elf').read_bytes(),
            'Counter proof requires its exact12-hex linked build identifier')
    state=pristine.symbols['_state'][0]
    require(pristine.symbols['_state'][1:3]==(100,1),'Pair state is not its reviewed100-byte object')

    def seed(cpu,value):
        require(cpu.read(state)==1 and cpu.read(state+4)==0,'Counter fixture is not idle and armed')
        raw_write(cpu,state+8,value);raw_write(cpu,state+12,value)

    rows=[]
    for initial in (179999,180000,180001,MASK-1,MASK):
        for path in ('split','full'):
            cpu=copy.deepcopy(pristine);cpu.reset_observations();seed(cpu,initial)
            if path=='full':cpu.inject_command=1
            before=counters(cpu);cpu.submit();after_submit=counters(cpu)
            wrapped=(initial+1)&MASK
            require(cpu.read(state+4)==1 and cpu.read(state+8)==wrapped and cpu.read(state+12)==initial,
                    'Actual submit did not preserve modulo32 pair ordering')
            captured=cpu.read(cpu.symbols['_controller_pair'][0])==2
            require(captured is (path=='split') and after_submit['split_submit']-before['split_submit']==1 and
                    after_submit['submit']-before['submit']==int(path=='full'),
                    'Counter fixture did not traverse its actual split/full submission path')
            cpu.complete();cpu.original_call(cpu.record.consumer);cpu.original_call(cpu.record.ready)
            require(cpu.read(state+4)==0 and cpu.read(state+8)==cpu.read(state+12)==wrapped,
                    'Actual completion did not retire the modulo32 pair')
            if path=='full':require(cpu.read(cpu.injected_rx)&255==5,'Full path lost actual virtual INFO reply')
            require(cpu.overlay_stats()['advertisements']>0 and cpu.tick_calls==int(path=='split'),
                    'Aged General pair lost virtual attachment or its selected TICK path: '+
                    repr((initial,path,cpu.overlay_stats()['advertisements'],cpu.tick_calls)))
            cpu.check_build_unchanged()
            rows.append(dict(initial=initial,path=path,pending_submitted=wrapped,pending_completed=initial,
                final_submitted=wrapped,final_completed=wrapped,controller_tick_calls=cpu.tick_calls,
                full_submit_fallback_calls=after_submit['submit']-before['submit'],
                original_sdk_and_consumer_executed=True,actual_c_pair_order_checked=True))

    refusals=[]
    for path in ('split','full'):
        for field,value in ((4,0),(8,1),(12,0)):
            cpu=copy.deepcopy(pristine);cpu.reset_observations();seed(cpu,MASK)
            if path=='full':cpu.inject_command=1
            cpu.submit()
            require(cpu.read(state+8)==0 and cpu.read(state+12)==MASK,'Refusal fixture did not wrap submitted')
            cpu.reset_observations()
            cpu.inject_on_complete=lambda c,offset=field,v=value:raw_write(c,state+offset,v)
            try:cpu.complete()
            except AssertionError as error:
                require(str(error).startswith('Actual worker refused with code 0x8;'),
                        'Wrapped completion missed its actual pair-order refusal: '+str(error))
            else:raise ValueError('Wrapped completion admitted corrupted pending/counter state')
            require(cpu.mutation_rx_snapshot is not None and cpu.rx_snapshot()==cpu.mutation_rx_snapshot and
                    not cpu.external_c_stores,'Wrapped counter refusal published RX')
            cpu.check_build_unchanged()
            refusals.append(dict(path=path,state_offset=field,value=value,refusal_code=8,
                rx_unchanged=True,before_parent_store=True,actual_c_refusal=True))

    loan=copy.deepcopy(pristine);loan.reset_observations();seed(loan,MASK);loan.inject_command=11
    loan.submit()
    try:loan.complete()
    except LoanBoundary:pass
    else:raise ValueError('Wrapped READ did not reach the actual low SD-loan entry')
    require(loan.calls['actual_low_sd_loan_boundary']==1 and loan.read(state+8)==0 and
            loan.read(state+12)==MASK and loan.block(loan.injected_rx,4)==bytes([0xa5])*4,
            'Wrapped storage request changed pair order or published an acknowledgement')
    storage=[]
    for function in ('_persistent_storage_guard','_persistent_context_guard'):
        for offset,value in ((None,None),(0,0),(4,0),(8,1),(12,0)):
            cpu=copy.deepcopy(loan);cpu.reset_observations()
            if offset is not None:raw_write(cpu,state+offset,value)
            before=cpu.rx_snapshot();result=cpu.worker(function,[])['result']
            require(result==int(offset is None) and cpu.rx_snapshot()==before and not cpu.external_c_stores,
                    'Actual wrapped storage authority accepted corruption or changed RX: '+
                    repr((function,offset,value,result,cpu.rx_snapshot()==before,cpu.external_c_stores)))
            cpu.check_build_unchanged()
            storage.append(dict(function=function,state_offset=offset,value=value,
                accepted=offset is None,actual_linked_c_guard=True,rx_unchanged=True))
    return dict(schema='kui.native.general-pair-counter-execution.v1',status='PASS',
        source_commit=args.source_commit,build=build_id,compile_receipt=provenance,
        artifact_sha256={name:sha((args.build/name).read_bytes()) for name in
            ('entry.elf','stage.elf','native-worker.elf','native-worker.bin','native-worker.reloc','resident-sci.elf')},
        source_sha256={name:sha((ROOT/name).read_bytes()) for name in
            (*SOURCES,'tools/check_native_general_lean_profile.py','tools/native_runtime_layout.py')},
        inputs=dict(startup_sha256=sha(source),record_sha256=sha(args.record.read_bytes()),extent=extent),
        lease_bytes=pristine.lease_bytes,sdk_initializer=initializer,pair_cases=rows,
        completion_refusals=refusals,storage_guard_cases=storage,
        wrapped_read=dict(actual_low_sd_loan_entry=True,loan_boundary=loan.loan_boundary,
            success_or_ack_substituted=False,rx_unpublished=True),
        storage_guard_execution='Actual guards called on their private P1 C stack from a copy of the '
            'real wrapped READ at its low-loan boundary; no game or storage function is substituted.',
        worker_helpers_substituted=False,original_game_or_sdk_functions_substituted=False,
        auxiliary_boundary_substituted=True,limitations=__doc__.strip())


def check(args):
    args.build=args.build.resolve()
    cpu,source,extent,initializer=fresh(args)
    provenance=json.loads((args.build/'entry.compile.json').read_text())
    require(provenance['commit']==args.source_commit and provenance['source_dirty'] is False,
            'Split proof does not use the clean final runtime freeze')
    rows,warm=compare_polls(cpu,args.polls)
    allinfo=authored_cold_allinfo(cpu)
    virtual=all_virtual_fallbacks(cpu)
    mutations=mutation_cases(cpu,warm)
    return dict(schema=SCHEMA,status='PASS',source_commit=args.source_commit,
        artifact_sha256={name:sha((args.build/name).read_bytes()) for name in
            ('entry.elf','stage.elf','native-worker.elf','native-worker.bin','native-worker.reloc','resident-sci.elf')},
        source_sha256={name:sha((ROOT/name).read_bytes()) for name in SOURCES},
        split_controller=True,cached_metadata=False,real_virtual_a2=True,real_c_callbacks=True,
        controller_tick_calls=warm.tick_calls,cold_info_captured_without_full_submit=rows[0]['captured'],
        prefix_reduced=True,rx_equivalence=True,physical_packet_equivalence=True,
        parent_byte_only_on_captured_polls=True,polls=rows,virtual_fallback_cases=virtual,
        authored_cold_allinfo=allinfo,
        mutation_refusals=mutations,sdk_initializer=initializer,
        inputs=dict(startup_sha256=sha(source),record_sha256=sha(args.record.read_bytes()),extent=extent),
        baseline='Same linked worker, retained unmodified full C submit entry selected at the modeled ABI bridge.',
        limitations=__doc__.strip())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','startup','record','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--source-commit',required=True);p.add_argument('--polls',type=int,default=8)
    p.add_argument('--general-counters',action='store_true',
                   help='Execute General split/full pairs at the former cap and uint32 rollover')
    p.add_argument('--build-id',help='Exact12-hex linked identifier when explicitly overridden at compile time')
    args=p.parse_args();require(args.polls>=8,'At least8 original SDK polls are required')
    require(not args.build_id or args.general_counters,'Build-ID override is specific to General counter proof')
    report=general_counter_cases(args) if args.general_counters else check(args)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    if args.general_counters:
        print(json.dumps(dict(status='PASS',pairs=len(report['pair_cases']),
            completion_refusals=len(report['completion_refusals']),storage_guards=len(report['storage_guard_cases']),
            output=str(args.output))))
    else:
        print(json.dumps(dict(status='PASS',polls=len(report['polls']),virtual_fallbacks=len(report['virtual_fallback_cases']),
                              mutations=len(report['mutation_refusals']),output=str(args.output))))


if __name__=='__main__':main()
