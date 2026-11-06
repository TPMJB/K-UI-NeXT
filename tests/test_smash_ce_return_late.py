#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked late CE return copy before the frozen helper initializer.

The prepared transport and its live RAM seal are authored inputs at this
boundary. Actual linked copy/cache-publication/clear instructions execute.
The operand-cache fixture models dirty destination lines and their writeback;
it is not a Dreamcast cache, scheduler or wall-clock simulation.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import shlex
import subprocess
import sys
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from check_vmu_native_worker_gateway import read_elf
from test_smash_menu_loader_runtime import RuntimeCPU,FIRMWARE,returning_seed,card_receipt,phase_video_seed
from test_smash_native_gin_linked import Machine,State

LOW,HIGH,RAW_BYTES,CODE_OFFSET,FILE_BYTES=0xac008300,0xacfc4000,41152,41184,49152
LOW_LIMIT,STOP,STACK=0xac00c000,0x8c003000,0x8c007f80
RAW,SHADOW,SHADOW_END=0xac008100,0xac00b400,0xac00b600
MENU_CELL,OLD_HOOK=0xac0000e0,0x8c005000
PINS={1:'85e864ae8d386b4b2d5cec731f788441367fdf9f2bef86f57ccd12a430e214e8',
      2:'47b5687e400b25b43ffa378b2b04171e59c039c57e018e83ac4de6bf7a8719e8'}

def sha(data):return hashlib.sha256(data).hexdigest()
def crc(data,previous=0):return zlib.crc32(data,previous)&0xffffffff
def require(ok,message):
    if not ok:raise AssertionError(message)

def runtime_fixture(code,symbols,profile,raw_crc=0x76543210):
    """A sealed ROOT/VID/receipt fixture; no storage admission is implied."""
    offset=symbols['_kui_smash_ce_return_runtime'][0]-LOW
    normalized=bytearray(code);normalized[offset:offset+632]=bytes(632)
    wire=bytearray(returning_seed(True)+struct.pack('<14I',*FIRMWARE,0,raw_crc,
        profile,RAW_BYTES,0x54321076)+struct.pack('<8I',
        0xacfc12c0,0xacfc3500,0xacfc03bc,0xacfc1090,0xacfcfe78,0xacfc0000,0xac006000,0xac007000)+
        card_receipt(owner=profile+2,sequence=2)+phase_video_seed())
    require(len(wire)==632,'Runtime fixture geometry changed')
    struct.pack_into('<I',wire,404,crc(wire,crc(normalized)))
    return offset,bytes(wire)

class CopyCPU(RuntimeCPU):
    def __init__(self,code,helper,runtime_offset,runtime,symbols,kind='ready',raw=None):
        self.active=False
        super().__init__(b'')
        self.active=False;self.symbols=symbols;self.body_end=LOW+len(code)
        self.bss_begin=symbols['__smash_ce_return_bss_begin'][0]
        self.bss_end=symbols['__smash_ce_return_bss_end'][0]
        self.shadow='__kui_smash_ce_return_raw_shadow' in symbols
        self.raw=bytes((i*37+11)&255 for i in range(512))if raw is None else raw
        require(len(self.raw)==512,'Raw capture fixture must cover exactly512 bytes')
        self.transport=bytearray(helper+struct.pack('<8I',0x5349554b,0x31525443,1,32,
            RAW_BYTES,len(code),runtime_offset,632)+code)
        self.transport.extend(bytes(FILE_BYTES-len(self.transport)))
        self.transport[CODE_OFFSET+runtime_offset:CODE_OFFSET+runtime_offset+632]=runtime
        self.expected=bytearray(code);self.expected[runtime_offset:runtime_offset+632]=runtime
        self.store_block(0xacfc0000,bytes([0x91])*0x4000)
        self.store_block(HIGH-32,bytes([0x6b])*(FILE_BYTES+64))
        self.store_block(HIGH,self.transport)
        self.store_block(LOW-512,bytes([0xa7])*(LOW_LIMIT-LOW+1024))
        self.store_block(RAW,self.raw)
        if kind=='raw_source_crc_fault':self.write(RAW+20,self.read(RAW+20)^1)
        elif kind=='raw_padding_crc_fault':self.write(RAW+508,self.read(RAW+508)^1)
        self.raw_source=self.block(RAW,512)
        self.store_block(0xac004000,bytes([0x4d])*0x3ea0)
        self.dirty={line:bytes([0x3c])*32 for line in range(LOW,LOW_LIMIT,32)}
        self.dirty[MENU_CELL]=struct.pack('<I',OLD_HOOK+(4 if kind=='changed_e0_owner' else 0))+bytes([0x69])*28
        self.publications=[];self.direct_writes=[];self.first_copy=None;self.low_fetch=False
        self.flushing=False;self.copy_fault=kind=='copy_readback_fault';self.injected=False
        self.shadow_fault=kind=='raw_shadow_readback_fault'
        self.r=[0x12340000+i*0x10101 for i in range(16)];self.r[15]=STACK
        self.r[4]=OLD_HOOK
        self.sr,self.pr,self.gbr,self.vbr,self.mach,self.macl=0x700000f0,STOP,0x8c000000,0x8c00f400,17,29
        self.saved=(self.r[8:15].copy(),self.r[15],self.pr,self.gbr,self.vbr,self.mach,self.macl,self.sr&~1)
        self.copy_offset=symbols['__smash_ce_return_copy'][0]-LOW
        self.pc=HIGH+CODE_OFFSET+self.copy_offset
        self.active=True

    def write(self,address,value,count=4):
        if self.active and not self.flushing:
            require(count==4,'Late copy used a non-word store')
            low=LOW<=address<self.body_end or self.bss_begin<=address<self.bss_end
            shadow=self.shadow and SHADOW<=address<SHADOW_END
            padding=HIGH+RAW_BYTES<=address<HIGH+FILE_BYTES
            require(low or shadow or padding or address==MENU_CELL,f'Late copy wrote outside its owned ranges: {address:08x}')
            if LOW<=address<self.body_end and self.first_copy is None:
                require(not self.dirty,'Incoming P2 code was written before all dirty destination aliases retired')
                self.first_copy=self.instructions
            if shadow:
                require(self.low_fetch and not self.dirty,'Raw shadow written before low entry/cache publication')
                require(self.block(LOW,len(self.expected))==bytes(self.expected) and
                    self.block(self.bss_begin,self.bss_end-self.bss_begin)==bytes(self.bss_end-self.bss_begin),
                    'Raw shadow written before complete controller/runtime/BSS arrived')
                require(self.block(RAW,512)==self.raw_source,'Capture changed or substituted the original raw image')
            if padding:
                require(self.low_fetch,'Transport source was erased while execution was still high')
                require(self.block(LOW,len(self.expected))==bytes(self.expected),
                    'Transport erased before complete code/runtime arrived')
                require(self.block(self.bss_begin,self.bss_end-self.bss_begin)==bytes(self.bss_end-self.bss_begin),
                    'Transport erased before complete low BSS clear')
                require(value==0,'Transport tail was not zeroed')
                if self.shadow:require(self.block(SHADOW,512)==self.raw,'Transport erased before raw shadow readback')
            if address==MENU_CELL:
                require(self.low_fetch and value==LOW,'E0 cell published before the complete low controller was ready')
                if self.shadow:require(self.block(SHADOW,512)==self.raw,'E0 published before exact raw shadow arrived')
            if self.copy_fault and not self.injected and LOW<=address<self.body_end:
                value^=1;self.injected=True
            if self.shadow_fault and not self.injected and shadow:
                value^=1;self.injected=True
            self.direct_writes.append((address,value))
        return super().write(address,value,count)

    def plain(self,op,pc):
        if op&0xf0ff==0x00a3:
            line=(self.r[(op>>8)&15]|0x20000000)&~31
            require(LOW<=line<LOW_LIMIT or line==MENU_CELL,'Copy published an unowned cache alias')
            require(self.first_copy is None,'Destination cache publication happened after P2 writes')
            self.publications.append(line)
            if line in self.dirty:
                self.flushing=True
                self.store_block(line,self.dirty.pop(line))
                self.flushing=False
        return super().plain(op,pc)

    def step(self):
        if LOW<=self.pc<self.body_end:self.low_fetch=True
        require((LOW<=self.pc<self.body_end) or
            (HIGH+CODE_OFFSET<=self.pc<HIGH+CODE_OFFSET+len(self.expected)),
            f'Late copy fetched outside its authenticated high/low code: {self.pc:08x}')
        super().step()

def copy_audit(image,code,helper,profile,raw=None):
    sy=image[4]
    require(len(helper)==RAW_BYTES and sha(helper)==PINS[profile],'Frozen immutable helper changed')
    require(sy['__smash_ce_return_begin'][0]==LOW and sy['__smash_ce_return_binary_end'][0]-LOW==len(code),
        'Controller binary/ELF geometry differs')
    require(len(code)<=7968 and sy['_kui_smash_ce_return_runtime'][1]==632,
        'Controller exceeds bounded transport')
    shadow='__kui_smash_ce_return_raw_shadow' in sy
    canonical='__kui_smash_ce_return_raw_image' in sy
    if canonical:
        require(sy['__kui_smash_ce_return_raw_image']==(1,0,0,0xfff1) and not shadow and
            '__smash_ce_return_raw_shadow_begin' not in sy,
            'Canonical image retains a startup shadow lease')
        at,size,_,_=sy['_kui_smash_ce_return_raw_prefix']
        require(size==260 and LOW<=at<at+260<=LOW+len(code),'Canonical prefix escapes initialized code')
        expected_raw=code[at-LOW:at-LOW+260]+bytes(252)
        if raw is None:raw=expected_raw
        require(raw==expected_raw,'Canonical prefix plus zero tail differs from current raw reader')
    if shadow:
        require(sy['__kui_smash_ce_return_raw_shadow']==(1,0,0,0xfff1) and
            sy['__smash_ce_return_raw_shadow_begin'][0]==SHADOW and
            sy['__smash_ce_return_raw_shadow_end'][0]==SHADOW_END and
            sy['__smash_ce_return_bss_end'][0]<=SHADOW<SHADOW_END<=sy['__smash_ce_return_stack_bottom'][0],
            'Protected raw shadow overlaps initialized/BSS/stack state')
    if raw is None:raw=bytes((i*37+11)&255 for i in range(512))
    offset,runtime=runtime_fixture(code,sy,profile,crc(raw))
    require(code[offset:offset+632]==bytes(632),'Static controller runtime is not blank')
    cases=[]
    for kind in ('ready','copy_readback_fault','changed_e0_owner')+(
            ('raw_shadow_readback_fault','raw_source_crc_fault','raw_padding_crc_fault')if shadow else()):
        fault=kind!='ready'
        cpu=CopyCPU(code,helper,offset,runtime,sy,kind,raw)
        before_raw=cpu.block(HIGH,RAW_BYTES)
        before_low_raw=cpu.block(LOW-512,512)
        before_workspace=cpu.block(0xacfc0000,0x4000)
        before_low_after=cpu.block(LOW_LIMIT,32)
        before_reader=cpu.block(0xac004000,0x3ea0)
        for _ in range(200000):
            if cpu.pc==STOP:break
            cpu.step()
        else:raise AssertionError('Late copy did not return within its instruction bound')
        saved=(cpu.r[8:15],cpu.r[15],cpu.pr,cpu.gbr,cpu.vbr,cpu.mach,cpu.macl,cpu.sr&~1)
        require(saved==cpu.saved,'Late copy changed caller nonvolatile/stack/special state')
        require(cpu.block(HIGH,RAW_BYTES)==before_raw,'Late copy changed frozen helper bytes')
        require(cpu.block(LOW-512,512)==before_low_raw,'Late copy changed the low raw reader')
        require(cpu.block(0xacfc0000,0x4000)==before_workspace,'Late copy changed lower helper workspace')
        require(cpu.block(LOW_LIMIT,32)==before_low_after,'Late copy escaped the retired BOOT1 range')
        require(cpu.block(0xac004000,0x3ea0)==before_reader,'Late copy changed the existing reader or old E0 hook')
        require(cpu.block(HIGH-32,32)==bytes([0x6b])*32 and
            cpu.block(HIGH+FILE_BYTES,32)==bytes([0x6b])*32,'Transport canary changed')
        expected_publications=[MENU_CELL]+(list(range(LOW,LOW_LIMIT,32))if kind!='changed_e0_owner'else[])
        require(cpu.publications==expected_publications and (kind=='changed_e0_owner' or not cpu.dirty),
            'Late copy did not retire every destination alias exactly once')
        if fault:
            require(cpu.r[0]==0xffffffff and (cpu.injected or kind in(
                'changed_e0_owner','raw_source_crc_fault','raw_padding_crc_fault'))and
                (cpu.low_fetch==kind.startswith(('raw_shadow_','raw_source_','raw_padding_'))),
                kind+': copy failure was not refused at its exact high/low boundary')
            require(cpu.block(HIGH,FILE_BYTES)==bytes(cpu.transport),
                'Copy readback failure erased its source transport')
            require(cpu.read(MENU_CELL)==OLD_HOOK+(4 if kind=='changed_e0_owner'else 0),
                'Copy failure published the return E0 before complete readback')
        else:
            require(cpu.r[0]==0 and cpu.low_fetch,'Late copy failed to execute its low continuation')
            require(cpu.block(LOW,len(code))==bytes(cpu.expected),'Complete controller/runtime copy differs')
            require(cpu.block(LOW+offset,632)==runtime,'ROOT/receipt/video runtime bytes changed')
            expected_bss=bytearray(cpu.bss_end-cpu.bss_begin)
            previous=sy['_kui_smash_ce_return_previous_e0'][0]-cpu.bss_begin
            struct.pack_into('<I',expected_bss,previous,OLD_HOOK)
            require(cpu.block(cpu.bss_begin,len(expected_bss))==expected_bss,
                'Low BSS retains data other than the exact previous E0 hook')
            require(cpu.read(MENU_CELL)==LOW,'E0 did not name the completed low controller')
            require(cpu.block(HIGH+RAW_BYTES,FILE_BYTES-RAW_BYTES)==bytes(FILE_BYTES-RAW_BYTES),
                'Frozen helper mutable tail was not restored to zero')
            untouched=((cpu.bss_end,SHADOW),(SHADOW_END,LOW_LIMIT))if shadow else((cpu.bss_end,LOW_LIMIT),)
            for first,last in untouched:
                require(cpu.block(first,last-first)==bytes([0x3c])*(last-first),
                    'Late copy used the future return stack or its guard gap')
            if shadow:require(cpu.block(SHADOW,512)==cpu.raw,'Complete protected raw shadow differs from admitted source')
        cases.append(dict(case=kind if fault else'dirty_alias_copy_and_clear',
            result=-1 if fault else 0,instructions=cpu.instructions,
            destination_lines_published=len(cpu.publications),high_to_low_execution=cpu.low_fetch,
            raw_helper_unchanged=True,caller_nonvolatile_state_exact=True,
            protected_raw_shadow=shadow,raw_shadow_exact=not fault if shadow else None,
            no_new_direct_b400_writes=bool(canonical and not any(SHADOW<=a<SHADOW_END for a,_ in cpu.direct_writes))))
    return dict(schema='kui.smash-ce.late-copy.v1',status='PASS',profile=profile,
        controller_elf_sha256=sha(image[0]),controller_binary_sha256=sha(code),
        helper_binary_sha256=sha(helper),initialized_bytes=len(code),runtime_bytes=632,
        low_bss_bytes=sy['__smash_ce_return_bss_end'][0]-sy['__smash_ce_return_bss_begin'][0],
        protected_raw_shadow=shadow,raw_shadow_interval=[hex(SHADOW),hex(SHADOW_END)]if shadow else None,
        canonical_raw_image=canonical,no_new_direct_b400_writes=bool(canonical),
        actual_copy_instructions=True,dirty_destination_aliases_retired=True,
        helper_tail_zero_before_initializer=True,old_reader_unchanged=True,source_bound_e0_publication=True,cases=cases,
        limits=['Transport CRC admission and the original pre-NK caller are separate boundaries; this copy proof alone grants no entry authority.',
            'Dirty cache lines/writeback are authored operand-cache fixtures; no physical Dreamcast cache or console timing is certified.',
            'Stops immediately before the frozen helper initializer; its original clear/install contract has separate linked proofs.'])

GATE_SOURCE=r'''
#include "retail_async.h"
#include "kui/native_ocram_diagnostic.h"
#include "vc2_ce_vmu_symbols.h"
kui_retail_async_region_type kui_retail_async_region;
struct kui_retail_manifest gate_map;
volatile struct kui_native_ocram_diagnostic kui_native_ocram_trace;
uint32_t gate_initializer_calls;
#define RETAIL_MANIFEST (&gate_map)
static uint32_t gate_hw(uint32_t address) {
 if(address==UINT32_C(0xacfc4000))return *(volatile uint32_t *)(uintptr_t)address;
 return address==UINT32_C(0xff00001c)?UINT32_C(0x909):0u;
}
static uint32_t gate_initializer(void *r,uint32_t token,volatile void *trace) {
 if(r!=&kui_retail_async_region || token!=7u || trace!=&kui_native_ocram_trace)return 999u;
 ++gate_initializer_calls;return 0u;
}
#define KUI_VC2_VMU_GATE_READ32(a) gate_hw(a)
/* Separate linked raw-wire tests qualify this physical-arrival boundary. */
#define KUI_VC2_VMU_GATE_RAW_PURGE() ((void)0)
#define KUI_VC2_VMU_GATE_RAW_CRC() UINT32_C(0x76543210)
#define KUI_VC2_VMU_GATE_LOAD(failed) ((void)(failed),0u)
#define KUI_VC2_VMU_GATE_INITIALIZE(r,t,tr) gate_initializer(r,t,tr)
#include "vc2_ce_vmu_gate.inc"
uint32_t gate_entry(uint32_t token,uint32_t function) {return vc2_ce_vmu_gate(token,function);}
'''

def gate_build(build,directory):
    compiler=str(os.environ.get('SH_CC')or ROOT/'.deps/sh-elf/bin/sh-elf-gcc')
    flags=shlex.split((build/'config').read_text().splitlines()[2])
    flags=[x for x in flags if x!='-flto']+['-fno-lto','-I'+str(build/'helper')]
    source=directory/'gate.c';source.write_text(GATE_SOURCE)
    obj,script,elf=directory/'gate.o',directory/'gate.ld',directory/'gate.elf'
    subprocess.run([compiler,*flags,'-c',str(source),'-o',str(obj)],cwd=ROOT,check=True,capture_output=True)
    script.write_text('OUTPUT_FORMAT("elf32-shl")\nOUTPUT_ARCH(sh)\nENTRY(_gate_entry)\n'
        '_kui_retail_menu_hook = 0x8c005000;\nSECTIONS { . = 0x8cd00000; '
        '.text : { *(.text .text.*) } .rodata : { *(.rodata .rodata.*) } '
        '.data : { *(.data .data.*) } .bss : { *(.bss .bss.* COMMON) } '
        '/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) } }\n')
    subprocess.run([compiler,'-m4-single','-ml','-nostdlib','-Wl,--gc-sections',
        '-Wl,--build-id=none','-T'+str(script),str(obj),'-lgcc','-o',str(elf)],
        cwd=ROOT,check=True,capture_output=True)
    decoder=directory/'integer.so'
    subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-O2','-fPIC','-shared',
        str(ROOT/'tests/sh4_integer_fast.c'),'-o',str(decoder)],check=True,capture_output=True)
    library=ctypes.CDLL(str(decoder));run=library.sh4_fast_run
    run.argtypes=[ctypes.POINTER(State),ctypes.c_uint32,ctypes.c_uint64];run.restype=ctypes.c_int
    return read_elf(elf),run

def gate_audit(build,image,code,helper,profile):
    raw=bytes((i*37+11)&255 for i in range(512))
    offset,runtime=runtime_fixture(code,image[4],profile,crc(raw))
    carrier=bytearray(helper+struct.pack('<8I',0x5349554b,0x31525443,1,32,
        RAW_BYTES,len(code),offset,632)+code)
    carrier.extend(bytes(FILE_BYTES-len(carrier)))
    carrier[CODE_OFFSET+offset:CODE_OFFSET+offset+632]=runtime
    live_crc=crc(carrier);require(live_crc!=0,'Fixture live CRC unexpectedly zero')
    cases=[]
    with tempfile.TemporaryDirectory(prefix='kui-ce-late-gate-')as name:
        harness,run=gate_build(build,Path(name));sy=harness[4]
        for kind in ('current_live_seal','missing_live_seal','old_runtime','mixed_static_code',
            'changed_raw_helper','changed_padding','raw_reader_refused','unrelated_caller'):
            cpu=Machine(run);cpu.load(harness)
            actual=bytearray(carrier)
            if kind=='old_runtime':actual[CODE_OFFSET+offset+456+20]^=1
            elif kind=='mixed_static_code':actual[CODE_OFFSET+32]^=1
            elif kind=='changed_raw_helper':actual[128]^=1
            elif kind=='changed_padding':actual[-1]^=1
            cpu.put(HIGH,actual);cpu.put(LOW,bytes([0xa7])*(LOW_LIMIT-LOW))
            cpu.put(RAW,raw)
            cpu.put(0xac005000,bytes([0x57])*32)
            cpu.put(MENU_CELL,struct.pack('<I',OLD_HOOK))
            m=sy['_gate_map'][0];trace=sy['_kui_native_ocram_trace'][0]
            cpu.put(m+60,struct.pack('<I',0x76543210^(1 if kind=='raw_reader_refused' else 0)))
            cpu.put(m+248,struct.pack('<I',0 if kind=='missing_live_seal'else live_crc))
            cpu.put(trace+20,struct.pack('<I',0x8ce09362+(2 if kind=='unrelated_caller' else 0)))
            cpu.put(trace+28,struct.pack('<I',0x700000f0))
            for index,(at,n)in enumerate(((LOW,LOW_LIMIT-LOW),(HIGH+RAW_BYTES,FILE_BYTES-RAW_BYTES),(0xac005000,32))):
                cpu.c.watch_first[index]=at&0x1fffffff;cpu.c.watch_end[index]=(at&0x1fffffff)+n
            value,instructions,status=cpu.call(sy['_gate_entry'][0],[7,8])
            require(status==0,'Actual source gate did not return')
            calls=struct.unpack('<I',cpu.block(sy['_gate_initializer_calls'][0],4))[0]
            good=kind=='current_live_seal'
            expected=0 if good or kind=='unrelated_caller' else 0x210 if kind=='raw_reader_refused' else 0x20a
            require(value==expected,f'{kind}: late gate status {value:x} differs from {expected:x}')
            require(calls==int(good),kind+': initializer boundary admission differs')
            if good:
                expected_code=bytearray(code);expected_code[offset:offset+632]=runtime
                require(cpu.block(LOW,len(code))==expected_code,'Actual gate/copy changed live controller')
                require(cpu.block(HIGH+RAW_BYTES,FILE_BYTES-RAW_BYTES)==bytes(FILE_BYTES-RAW_BYTES),
                    'Actual gate reached initializer with nonzero helper mutable tail')
            else:
                require(not any(cpu.c.watch_writes),kind+': rejected transport wrote code, padding or E0')
                require(cpu.block(HIGH,FILE_BYTES)==actual,kind+': rejected transport erased the source')
            cases.append(dict(case=kind,status=hex(value),instructions=instructions,
                initializer_calls=calls,actual_source_gate=True,no_rejected_copy=not good))
    return dict(cases=cases,physical_load_and_raw_crc='authored peripheral boundaries',
        complete_49152_byte_crc_executes=True,copy_executes_on_admitted_current_seal=True,
        missing_ram_seal_cannot_import_disk_runtime=True,old_runtime_and_mixed_static_refused=True,
        limits='The production gate include is target-compiled independently with the exact native flags; raw arrival/CRC, MMIO and the final helper initializer are explicit fixtures. Full resident entry and original MAINANA lifetime remain separate proofs.')

def audit(build,profile):
    build=Path(build)
    paths=[build/'ce-return.elf',build/'ce-return.bin',build/'helper/helper.bin',
        build/'stage.elf',build/'resident-sci.elf',build/'helper/prepared.bin',build/'vc2-raw.bin']
    before={str(p):sha(p.read_bytes())for p in paths}
    raw=paths[6].read_bytes();require(len(raw)==512,'Actual raw image is not exactly512 bytes')
    resident=read_elf(paths[4]);sections=resident[3]
    section=sections[resident[4]['_kui_vc2_ce_vmu_read_card'][3]]
    linked=resident[0][section[4]:section[4]+section[5]]
    require(raw==linked+bytes(512-len(linked)),'Capture source differs from actual linked/padded raw reader')
    report=copy_audit(read_elf(paths[0]),paths[1].read_bytes(),paths[2].read_bytes(),profile,raw)
    report['live_seal_gate']=gate_audit(build,read_elf(paths[0]),paths[1].read_bytes(),paths[2].read_bytes(),profile)
    require(before=={str(p):sha(p.read_bytes())for p in paths},'Linked inputs changed during proof')
    report['inputs']=before
    report.update(stage_elf_sha256=before[str(paths[3])],
        resident_elf_sha256=before[str(paths[4])],helper_sha256=before[str(paths[2])],
        prepared_sha256=before[str(paths[5])])
    report['test_sha256']=sha(Path(__file__).read_bytes())
    sources={Path(__file__).resolve(),ROOT/'tests/sh4_integer_fast.c'}
    for module in tuple(sys.modules.values()):
        name=getattr(module,'__file__',None)
        if name:
            path=Path(name).resolve()
            if path.is_relative_to(ROOT) and path.suffix=='.py':sources.add(path)
    sources.update(
        (ROOT/'src/loader/smash_ce_return_copy.S',ROOT/'src/loader/smash_ce_return_loader.ld',
         ROOT/'src/loader/vc2_ce_vmu_gate.inc',ROOT/'src/loader/vc2_ce_vmu_late_stage.inc',
         ROOT/'src/loader/vc2_ce_vmu_persist_stage.inc',ROOT/'include/kui/smash_ce_transport.h'))
    report['source_sha256']={str(p.relative_to(ROOT)):sha(p.read_bytes())for p in sorted(sources)}
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True)
    p.add_argument('--profile',type=int,choices=(1,2),required=True);p.add_argument('--report',type=Path)
    a=p.parse_args();r=audit(a.build,a.profile);text=json.dumps(r,indent=2)+'\n'
    if a.report:a.report.write_text(text)
    print(text,end='')
