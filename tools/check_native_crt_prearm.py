#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the compiled cold CRT interception, including its cache publisher.

The owner program is synthetic. No C admission or memory helper is replaced:
the actual stage instructions select the low reader, encode the veneer,
compare the source, write through P1, purge dirty lines, invalidate the I-cache,
verify through P2, and retain the new expected window. This is a bounded SH
execution audit, not a claim about console compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
from check_native_dual_callbacks import expected_entries, linked_table, require
from check_native_dual_readers import ScalarOps
from check_vmu_native_sdk_sci_route import SDKCPU, RETURN, assert_c_abi
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from test_vmu_native_heap_execution import SH, signed
from sh4_delay_slots import validate_delay_slot

HELPER = '_native_crt_bootstrap_prearm'
PATCHER = '_readonly_patch_entry'
PUBLISH = '_kui_native_passive_publish'
SELECTOR = '_native_low_bindings'
GAME, GAME_BYTES, STACK = 0x8c100000, 4096, 0x8cfef000
CCR, MASK = 0xff00001c, 0xffffffff
ORIGINAL = struct.pack('<6H', 0x006a, 0xc906, 0x406a, 0xc700, 0x6f02, 0xd500)
NAMES = (HELPER, PATCHER, SELECTOR, '_kui_native_dual_u32',
         '_kui_vmu_native_patch_encode', '_ram', '_memcpy', '_memcmp')


def canonical(address):
    return (address & 0x1fffffff) | 0x80000000


def initialized(image, address, count):
    for kind, offset, begin, _, size, _, _, _ in image[2]:
        if kind == 1 and begin <= address < address+count <= begin+size:
            return image[0][offset+address-begin:offset+address-begin+count]
    raise ValueError('Unbounded initialized stage bytes')


def code_contract(stage):
    bounds, hashes = [], {}
    for name in NAMES:
        address, body, section = symbol_bytes(stage, name, 2)
        require(section[2]&7 == 6, 'CRT helper must be immutable executable code: '+name)
        bounds.append((address, address+len(body)))
        hashes[name] = dict(address=hex(address), bytes=len(body), sha256=hashlib.sha256(body).hexdigest())
    # The authored publisher has no ST_SIZE. Its complete instructions and
    # literals are independently closed by the next typed function, and its
    # P2 branch/CCR literal are checked before any execution.
    address, size, kind, owner = stage[4][PUBLISH]
    require(size == 0 and kind == 0 and owner < len(stage[3]), 'Publisher symbol contract changed')
    body = initialized(stage, address, 0x44)
    words = (0xd001,0x402b,0x0009,0x0009, (address+12+0x20000000)&0xffff,
             (address+12+0x20000000)>>16, 0xe0e0,0x2409,0x3452,0x8903,
             0x04a3,0x7420,0xaffa,0x0009,0xd007,0x6102,0xd207,0x212b,
             0x2012,0x0009,0x0009,0x0009,0x0009,0x0009,0x0009,0x0009,0x0009,
             0x000b,0x0009,0x0009,0x001c,0xff00,0x0800,0x0000)
    # The final zero halfword ends the constant pool at the next function.
    require(body == struct.pack('<34H', *words), 'Publisher instructions/P2/CCR literals changed')
    bounds.append((address, address+0x44))
    hashes[PUBLISH] = dict(address=hex(address), bytes=len(body), sha256=hashlib.sha256(body).hexdigest())
    for name, count in (('_native_exits',52), ('_persistent_exit_windows',48)):
        at, size, kind, owner = stage[4][name]
        section = stage[3][owner]
        require(size == count and kind == 1 and section[1] == 8 and section[2]&7 == 3 and
                section[3] <= at < at+size <= section[3]+section[5], 'CRT retained object is not typed BSS: '+name)
    return bounds, hashes


class Stopped(Exception):
    pass


class PrearmCPU(ScalarOps, SDKCPU):
    def __init__(self, stage, resident, backing, reader, low, *, cache=True, offset=0x100,
                 alias=0x80000000, mask=15, bank=1, seed=0):
        super().__init__(resident, seed=seed, mask=mask, bank=bank)
        self.memory = backing
        self.ram, self.cache, self.cache_on = {}, {}, cache
        self.stage, self.symbols, self.low = stage, stage[4], low
        self.code_bounds, _ = code_contract(stage)
        self.entry = (GAME+offset)&0x1fffffff | alias
        self.r[15], self.pr = STACK, RETURN
        self.targets = {self.symbols['_native_report'][0], self.symbols['_stopped'][0]}
        self.flushes, self.readbacks, self.ccr_writes, self.visited = [], [], [], set()
        self.ccr, self.stop = 0x909, None
        self.readback_fault = self.cached_verify_fault = False
        self.put(GAME, bytes((i*17+0x53)&255 for i in range(GAME_BYTES)))
        self.put(self.entry, ORIGINAL)
        self.game_before = self.bytes_at(GAME, GAME_BYTES, ram=True)
        exits = self.symbols['_native_exits'][0]
        descriptor = (2,52,6,GAME,GAME_BYTES,4,GAME+0x10,GAME+0x40,GAME+0x70,self.entry,0,0,0)
        self.put(exits, struct.pack('<13I', *descriptor))
        captures = b''.join(self.bytes_at(e,12,ram=True) for e in descriptor[6:10])
        self.put(self.symbols['_persistent_exit_windows'][0],captures)
        self.captured_before = captures
        self.put(self.symbols['_manifest'][0]+44,struct.pack('<II',1,reader))
        # An unrelated dirty neighbour must survive publication of the whole
        # game line. This deliberately differs from the backing RAM sentinel.
        self.sentinel = canonical(self.entry)&~31
        self.sentinel = self.sentinel+31 if (canonical(self.entry)&31) < 20 else self.sentinel
        self.write(self.sentinel,0xd7,1)
        self.initial = self.snapshot()
        self.writes = []

    def put(self,address,data):
        at=self.offset(address,len(data));self.ram.update((at+i,b) for i,b in enumerate(data))

    def raw(self,at,count):
        return bytes(self.ram.get(at+i,self.memory[at+i]) for i in range(count))

    def line(self,at):
        base=at&~31
        if base not in self.cache:self.cache[base]=[bytearray(self.raw(base,32)),False]
        return self.cache[base]

    def bytes_at(self,address,count,*,ram=False):
        if ram:return self.raw(self.offset(address,count),count)
        return bytes(self.read(address+i,1) for i in range(count))

    def read(self,address,count=4):
        if not hasattr(self,'ram'):return super().read(address,count)
        if address == CCR:return self.ccr
        at=self.offset(address,count)
        if address&0xe0000000 == 0x80000000 and self.cache_on:
            data=bytes(self.line(at+i)[0][(at+i)&31] for i in range(count))
        else:data=self.raw(at,count)
        if address&0xe0000000 == 0xa0000000 and self.pr == self.symbols[PATCHER][0]+0x40 and self.offset(self.entry,12) <= at < self.offset(self.entry,12)+12:
            self.readbacks.append((address,count,len(self.flushes),len(self.ccr_writes)))
            require(self.flushes and self.ccr_writes, 'CRT P2 verification preceded cache publication')
            if self.readback_fault:data=bytes([data[0]^1])+data[1:]
        if self.cached_verify_fault and address&0xe0000000 == 0x80000000 and self.ccr_writes and \
                self.offset(self.entry,12) <= at < self.offset(self.entry,12)+12:
            data=bytes([data[0]^1])+data[1:]
        return int.from_bytes(data,'little')

    def write(self,address,value,count=4):
        if not hasattr(self,'ram'):return super().write(address,value,count)
        if address == CCR:
            require(canonical(self.executing) >= self.symbols[PUBLISH][0] and self.executing&0xe0000000 == 0xa0000000,
                    'CRT instruction invalidation did not execute through P2')
            self.ccr=value;self.ccr_writes.append(value);return
        at=self.offset(address,count);data=(value&((1<<(8*count))-1)).to_bytes(count,'little')
        self.writes.append((self.executing,address,value,count))
        if address&0xe0000000 == 0x80000000 and self.cache_on:
            for i,b in enumerate(data):line=self.line(at+i);line[0][(at+i)&31]=b;line[1]=True
        else:self.put(address,data)

    def plain(self,op,pc):
        self.executing=pc
        n,m=op>>8&15,op>>4&15
        if op&0xf0ff == 0x00a3:
            at=self.offset(self.r[n],1)&~31;line=self.cache.pop(at,None)
            if line and line[1]:self.ram.update((at+i,b) for i,b in enumerate(line[0]))
            self.flushes.append(at);return
        if op&0xf0ff == 0x4010:self.r[n]=(self.r[n]-1)&MASK;self.flag(self.r[n]==0);return
        if op&0xf0ff == 0x4029:self.r[n]>>=16;return
        if op&0xf00f == 0x600a:
            value=self.r[m]+(self.sr&1);self.r[n]=(-value)&MASK;self.flag(value!=0);return
        if op&0xf00f == 0x6005:
            self.r[n]=signed(self.read(self.r[m],2),16)&MASK
            if n!=m:self.r[m]=(self.r[m]+2)&MASK
            return
        if op&0xf00f == 0x6004:
            self.r[n]=signed(self.read(self.r[m],1),8)&MASK
            if n!=m:self.r[m]=(self.r[m]+1)&MASK
            return
        if op&0xf00f == 0x2000:self.write(self.r[n],self.r[m],1);return
        if op>>8 == 0x81:self.write(self.r[m]+(op&15)*2,self.r[0],2);return
        if op&0xf00f == 0x0005:self.write(self.r[n]+self.r[0],self.r[m],2);return
        super().plain(op,pc)

    def callee(self):
        self.stop=(self.pc,self.r[4],self.r[5]);raise Stopped()

    def run_to(self,entry,stop):
        self.pc=entry
        for _ in range(10000):
            if self.pc==stop:return
            pc=self.pc;address=canonical(pc)
            require(any(a<=address<b for a,b in self.code_bounds) or canonical(self.entry)<=address<canonical(self.entry)+6,
                    'CRT execution escaped reviewed helpers: '+hex(pc))
            self.visited.add(address)
            op=self.read(pc,2);n=op>>8&15;target=None;delay=False
            if op>>12 in (0xa,0xb):
                target=pc+4+2*signed(op&4095,12);delay=True
                if op>>12==0xb:self.pr=pc+4
            elif op&0xf0ff in (0x400b,0x402b):
                target=self.r[n];delay=True
                if op&0xf0ff==0x400b:self.pr=pc+4
            elif op==0xb:target=self.pr;delay=True
            elif op>>8 in (0x89,0x8b,0x8d,0x8f):
                take=bool(self.sr&1)==(op>>8 in (0x89,0x8d));delay=op>>8 in (0x8d,0x8f)
                target=pc+4+2*signed(op&255,8) if take else pc+(4 if delay else 2)
            if target is None:self.plain(op,pc);self.pc=pc+2;continue
            if delay:
                slot=self.read(pc+2,2);validate_delay_slot(slot,pc+2);self.plain(slot,pc+2)
            self.pc=target&MASK
            if self.pc in self.targets:self.callee()
        raise ValueError('CRT prearm exceeded its bounded instruction budget')


def expected_patch(entry,target):
    words=[0xd001,0x402b,0x0009,0x0009,0x0009,0x0009]
    at=8 if not entry&2 else 6
    patch=bytearray(struct.pack('<6H',*words));struct.pack_into('<I',patch,at,target)
    return bytes(patch)


def run_case(stage,resident,backing,reader,low,*,variant=None,**options):
    cpu=PrearmCPU(stage,resident,backing,reader,low,**options)
    descriptor=stage[4]['_native_exits'][0];captured=stage[4]['_persistent_exit_windows'][0]
    expected=True;stopped=False
    if isinstance(variant,tuple) and variant[0]=='source':
        index,bit=variant[1:];at=cpu.entry+index
        cpu.write(at,cpu.read(at,1)^(1<<bit),1);expected=False
    elif isinstance(variant,tuple) and variant[0]=='capture':
        index,bit=variant[1:];at=captured+36+index
        cpu.write(at,cpu.read(at,1)^(1<<bit),1);expected=False
    elif isinstance(variant,tuple) and variant[0]=='descriptor':
        word,value=variant[1:];cpu.put(descriptor+4*word,struct.pack('<I',value));expected=False
    elif variant=='readback':cpu.readback_fault=True;expected=False;stopped=True
    elif variant=='cached-verify':cpu.cached_verify_fault=True;expected=False
    elif variant=='ordinary':
        cpu.put(descriptor+8,struct.pack('<I',2));cpu.put(descriptor+20,struct.pack('<I',3));cpu.put(descriptor+36,bytes(4))
    elif isinstance(variant,tuple) and variant[0]=='instruction':
        name,offset,op=variant[1:];cpu.put(stage[4][name][0]+offset,struct.pack('<H',op))
    before_game=cpu.bytes_at(GAME,GAME_BYTES);before_capture=cpu.bytes_at(captured,48)
    cpu.writes=[];before=cpu.snapshot()
    try:cpu.run_symbol(HELPER)
    except Stopped:
        require(stopped and cpu.stop[1:]==(9,6),'CRT publication failed through the wrong terminal report')
    else:
        require(not stopped and bool(cpu.r[0])==expected,'CRT prearm returned the wrong admission result')
        assert_c_abi(cpu,before)
    for _,address,_,count in cpu.writes:
        at=canonical(address)
        require(STACK-256<=at<at+count<=STACK or
                canonical(cpu.entry)<=at<at+count<=canonical(cpu.entry)+12 or
                captured+36<=at<at+count<=captured+48,
                'CRT helper wrote outside its stack, fourth capture or exact entry window')
    if expected and variant!='ordinary':
        patch=expected_patch(cpu.entry,low[1])
        require(cpu.bytes_at(cpu.entry,12)==patch and cpu.bytes_at(cpu.entry,12,ram=True)==patch,
                'CRT did not publish the exact selected reader veneer')
        require(cpu.bytes_at(captured,36)==before_capture[:36] and cpu.bytes_at(captured+36,12)==patch,
                'CRT did not retain exactly its fourth published window')
        lines=list(range(cpu.offset(cpu.entry,12)&~31,(cpu.offset(cpu.entry,12)+11&~31)+1,32))
        require(cpu.flushes==lines and cpu.ccr_writes==[0x909|0x800] and cpu.readbacks,
                'CRT omitted exact data/instruction cache publication and P2 verification')
        require(cpu.bytes_at(cpu.sentinel,1,ram=True)==b'\xd7','Whole-line publication lost dirty neighbour data')
        for i in range(GAME_BYTES):
            at=GAME+i
            if not canonical(cpu.entry)<=at<canonical(cpu.entry)+12 and at!=cpu.sentinel:
                require(cpu.read(at,1)==before_game[i],'CRT wrote outside its twelve-byte window')
        # Start the actual veneer as the first owner instruction. Stop at
        # the selected low terminal hook before any original FPSCR/SP/BSS code.
        saved=(cpu.r[15],cpu.fpscr,cpu.bytes_at(GAME+0x800,32));writes=len(cpu.writes)
        cpu.run_to(canonical(cpu.entry),low[1])
        require((cpu.r[15],cpu.fpscr,cpu.bytes_at(GAME+0x800,32))==saved and len(cpu.writes)==writes,
                'Cold CRT executed owner stack/FPSCR/BSS changes before its terminal hook')
        # A later installer sees the retained veneer as its source witness.
        # Re-publishing must execute the same real path and keep that window.
        cpu.flushes=[];cpu.ccr_writes=[];cpu.readbacks=[];before=cpu.snapshot()
        cpu.run_symbol(HELPER);assert_c_abi(cpu,before)
        require(cpu.r[0]==1 and cpu.flushes==lines and cpu.readbacks and
                cpu.bytes_at(captured+36,12)==patch and cpu.bytes_at(cpu.entry,12,ram=True)==patch,
                'CRT late re-publication failed its already-armed source witness')
    else:
        require(cpu.bytes_at(captured,48)==before_capture,'Refused CRT source changed its captured windows')
        if not cpu.flushes:
            require(cpu.bytes_at(GAME,GAME_BYTES)==before_game and not cpu.ccr_writes,
                    'Refused CRT source modified owner memory/cache')
        if variant=='ordinary':require(stage[4][SELECTOR][0] not in cpu.visited,
                'An ordinary exit family unexpectedly selected/patched a CRT entry')
    return cpu


def check(directory,*,self_test=False):
    require(__debug__,'CRT audit requires assertions enabled')
    directory=Path(directory);stage=read_elf(directory/'stage.elf')
    residents=[read_elf(directory/name) for name in ('resident-sci.elf','resident-legacy.elf')]
    expected=[expected_entries(image) for image in residents];linked_table(stage,expected)
    _,hashes=code_contract(stage)
    backing=SH(b'')
    for image in (residents[0],stage):
        for kind,offset,address,_,count,_,_,_ in image[2]:
            if kind==1 and count:backing.store_block(address,image[0][offset:offset+count])
    backing=bytes(backing.memory);positives=refusals=mutants=0
    for reader in (1,2):
        # Only stage code executes. SDKCPU supplies the initial register state;
        # legacy callback addresses still come from their own typed ELF above.
        resident=residents[0]
        low=expected[reader-1]
        for cache in (False,True):
            for offset in (0x100,0x102,0x11e):
                for alias in (0,0x80000000,0xa0000000):
                    for bank in (0,1):
                        run_case(stage,resident,backing,reader,low,cache=cache,offset=offset,alias=alias,bank=bank);positives+=1
        for byte in range(12):
            for bit in range(8):
                run_case(stage,resident,backing,reader,low,variant=('source',byte,bit));refusals+=1
                run_case(stage,resident,backing,reader,low,variant=('capture',byte,bit));refusals+=1
        for word,value in ((0,1),(1,48),(5,3),(5,5),(9,0),(9,GAME-2),(9,GAME+GAME_BYTES-10),
                           (9,GAME+0x101),(4,11)):
            run_case(stage,resident,backing,reader,low,variant=('descriptor',word,value));refusals+=1
        for variant in ('readback','cached-verify'):
            run_case(stage,resident,backing,reader,low,variant=variant);refusals+=1
        run_case(stage,resident,backing,reader,low,variant='ordinary');positives+=1
        if self_test:
            # Deliberately broken real SH instructions must fail the replay,
            # including a lost fourth capture, missing patch, omitted OCBP,
            # and omitted instruction invalidation. No source model changes.
            for name,offset,op in ((HELPER,0x80,0x0009),(HELPER,0x96,0x0009),
                                   (PUBLISH,0x14,0x0009),(PUBLISH,0x24,0x0009),
                                   (HELPER,0x58,0x0009),(HELPER,0x62,0x0009)):
                try:run_case(stage,resident,backing,reader,low,variant=('instruction',name,offset,op))
                except (AssertionError,ValueError,Stopped):mutants+=1
                else:raise ValueError('CRT replay accepted a broken compiled instruction '+name+'+'+hex(offset))
    return dict(schema='kui.native.crt-prearm.audit.v1',stage_sha256=hashlib.sha256(stage[0]).hexdigest(),
        positive_cases=positives,refusal_cases=refusals,compiled_instruction_mutants_rejected=mutants,
        both_selected_reader_roles=True,actual_patch_encoder_memcpy_memcmp_executed=True,
        distinct_p1_dirty_cache_and_p2_ram=True,actual_ocbp_and_p2_ccr_invalidation_executed=True,
        original_stack_fpscr_bss_not_executed=True,exact_fourth_capture_updated_only_after_verification=True,
        compiled_late_republication_cases=positives-2,
        instructions=hashes,console_compatibility_verified=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    parser.add_argument('--self-test',action='store_true');parser.add_argument('--output',type=Path)
    args=parser.parse_args();result=check(args.directory,self_test=args.self_test)
    output=json.dumps(result,indent=2,sort_keys=True)+'\n'
    if args.output:args.output.write_text(output)
    print(output,end='')


if __name__=='__main__':main()
