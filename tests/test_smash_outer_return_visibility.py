#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute final linked outer return markers/stoppers and bounded PVR reporter.

Only framebuffer registers and VRAM are modeled. Actual outer helper, CRC,
strlen, stopped/relay_stopped prefixes and reporter instructions execute.
Stage-main cases stop at the first capture entry; no card I/O, renderer,
worker lifecycle, complete launch or physical display timing is simulated.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf
from test_smash_return_fault_display import FaultCPU

STACK,RETURN,MESSAGE=0xac00c000,0x8c003000,0x8cd10000
TEXT=b'OUTER LIVE ALLOCATION REFUSED'
DETAIL=0x89abcdef
def sha(data):return hashlib.sha256(data).hexdigest()
def address(elf,name):return elf[4][name][0]
def registers(fallback=False):
    return {0xa05f8044:9 if fallback else 0x800005,0xa05f8050:0x120000,
        0xa05f8054:0,0xa05f80d0:0x100,0xa05f805c:319|(479<<10)|(1<<20)}

class OuterCPU(FaultCPU):
    def __init__(self,elf,regs):
        super().__init__(elf,regs);self.trace_address=address(elf,'_smash_outer_return_trace')
        self.trace_writes=[];self.reporter=address(elf,'_kui_smash_return_fault_display')
        self.progress=address(elf,'_kui_smash_return_progress');self.progress_calls=[]
        self.calls=[];self.reporter_return=None;self.reporter_finished=False
        self.helpers={address(elf,n):n for n in ('_smash_outer_return_mark','_smash_outer_return_stop')}
        self.old={address(elf,n):n for n in ('_retail_display_capture','_retail_display_restore',
            '_retail_display_line','_retail_display_hex') if n in elf[4]}
        self.forbid_old=True;self.helper_visits=[]
    def preset(self,at,data):
        self.active=False;self.store_block(at,data);self.active=True
    def write(self,at,value,count=4):
        if self.active and at==getattr(self,'trace_address',None):
            assert count==1 and value in (0,1)
            self.trace_writes.append(value);self.active=False
            try:return super().write(at,value,count)
            finally:self.active=True
        return super().write(at,value,count)
    def step(self):
        if self.pc in self.old and self.forbid_old:raise AssertionError('Old renderer reached: '+self.old[self.pc])
        if self.pc in self.helpers:self.helper_visits.append(self.helpers[self.pc])
        if self.pc==self.reporter:
            self.calls.append(tuple(self.r[4:6]));self.reporter_return=self.pr
        if self.pc==self.progress:self.progress_calls.append(tuple(self.r[4:6]))
        if self.reporter_return==self.pc:self.reporter_finished=True
        return super().step()

def exercise(elf,name,entry,trace,fallback=False,main_flags=None):
    cpu=OuterCPU(elf,registers(fallback));cpu.preset(MESSAGE,TEXT+b'\0')
    cpu.preset(cpu.trace_address,bytes((trace,)))
    if main_flags is not None:
        cpu.preset(address(elf,'__smash_menu_return_seed')+16,main_flags.to_bytes(4,'little'))
    cpu.r[15]=STACK;cpu.pc=address(elf,entry);cpu.pr=RETURN
    cpu.r[4:6]=[0x111,DETAIL] if entry=='_smash_outer_return_mark' else [MESSAGE,DETAIL]
    if main_flags is not None:cpu.r[4]=0x8cc01000
    saved=cpu.r[8:15].copy();before=(cpu.gbr,cpu.vbr,cpu.original_ccr,cpu.sr&~1)
    visits=Counter();park=None;capture=address(elf,'_retail_display_capture')
    for _ in range(600000):
        if cpu.pc==RETURN:break
        if main_flags is not None and cpu.pc==capture:break
        if cpu.reporter_finished:
            visits[cpu.pc]+=1
            if visits[cpu.pc]>=5:park=cpu.pc;break
        cpu.step()
    else:raise AssertionError('Outer visibility code exceeded finite instruction observation bound')
    assert (cpu.gbr,cpu.vbr,cpu.original_ccr,cpu.sr&~1)==before
    if main_flags is not None:
        assert cpu.pc==capture and cpu.trace_writes==[int(main_flags==3)]
        expected=[];expected_progress=[(0x110,0)] if main_flags==3 else []
    elif entry=='_smash_outer_return_mark':
        assert cpu.pc==RETURN and cpu.r[15]==STACK and cpu.r[8:15]==saved
        expected=[];expected_progress=[(0x111,0)] if trace else []
    else:
        expected=[(zlib.crc32(TEXT)|0x80000000,DETAIL)];expected_progress=[]
        assert park is not None and cpu.helper_visits==['_smash_outer_return_stop']
    assert cpu.calls==expected
    assert cpu.progress_calls==expected_progress
    expected_writes=[]
    for phase,detail in expected_progress:
        expected_writes.extend(((cpu.progress_address+4,detail),(cpu.progress_address,phase)))
    assert cpu.progress_writes==expected_writes
    if not expected:
        assert not cpu.pvr_reads and not cpu.pvr_writes and not cpu.pixel_writes
        assert cpu.vram==bytearray([0x5b])*0x800000
    elif fallback:assert not cpu.pixel_writes and cpu.pvr_writes==[(0xa05f8040,0xff0000)]
    else:
        assert cpu.pixel_writes>6000 and not cpu.pvr_writes
        assert len(cpu.pvr_reads)==5 and set(cpu.pvr_reads)==set(registers())
    return {'case':name,'entry':entry,'reporter_arguments':[list(v) for v in cpu.calls],
        'RAM_progress_arguments':[list(v) for v in cpu.progress_calls],
        'pixel_stores':cpu.pixel_writes,'border_stores':len(cpu.pvr_writes),
        'parked_before_old_renderer':park is not None,'reached_capture_after_marker':main_flags is not None,
        'actual_SH_instructions':cpu.instructions,'observed_private_stack_bytes':STACK-cpu.low_stack}

def source_bounds():
    source=(ROOT/'src/loader/retail_stage.c').read_text()
    gate='#if KUI_SMASH_MENU_LOADER && KUI_SMASH_CARD_SESSION && KUI_SMASH_PHASE_DIAGNOSTIC'
    assert source.count(gate)>=3
    start=source.index('static void __attribute__((noipa)) smash_outer_return_mark(uint32_t phase) {')
    marker=source[start:source.index('\n}',start)]
    assert 'kui_smash_return_progress(phase,0u);' in marker and 'fault_display' not in marker
    for name in ('stopped','relay_stopped'):
        start=source.index('static void '+name+'(const char *message,uint32_t detail) {')
        body=source[start:source.index('\n}',start)]
        assert body.index('smash_outer_return_stop(message,detail);')<body.index('retail_display_')
    main=source[source.index('void kui_retail_stage_main(const uint8_t *wire) {'):]
    assert main.index('smash_outer_return_trace=smash_menu_return_seed.flags==KUI_SMASH_MENU_RETURN_CONTEXT;')<\
        main.index('smash_outer_return_mark(UINT32_C(0x110));')<main.index('retail_display_capture(&display);')
    return {'source_sha256':sha(source.encode()),'outer_session_diagnostic_gate':True,
        'both_stoppers_route_before_old_renderer':True,'RAM_marker_precedes_capture':True,
        'successful_markers_do_not_call_renderer':True}
def audit(path):
    path=Path(path);elf=read_elf(path);before=sha(elf[0])
    for name in ('_smash_outer_return_mark','_smash_outer_return_stop','_stopped','_relay_stopped',
                 '_kui_smash_return_fault_display','_kui_smash_return_progress'):
        assert name in elf[4] and elf[4][name][2]==2 and elf[4][name][1]>0
    cases=[exercise(elf,'initial GUI leaves inherited scanout untouched','_kui_retail_stage_main',False,main_flags=1),
        exercise(elf,'captured return records110 in RAM before capture','_kui_retail_stage_main',False,main_flags=3),
        exercise(elf,'disabled marker is inert','_smash_outer_return_mark',False),
        exercise(elf,'enabled successful marker writes only named RAM state','_smash_outer_return_mark',True),
        exercise(elf,'stopped uses inherited framebuffer and parks','_stopped',True),
        exercise(elf,'relay stopped uses border fallback and parks','_relay_stopped',True,True)]
    assert before==sha(path.read_bytes()),'ELF changed during visibility proof'
    return {'schema':'kui.smash.outer-return-visibility.v2','status':'PASS','outer_stage_elf_sha256':before,
        'source_bounds':source_bounds(),'cases':cases,'actual_helper_crc_reporter_instructions_executed':True,
        'model_sha256':sha((ROOT/'tests/test_smash_return_fault_display.py').read_bytes()),
        'test_sha256':sha(Path(__file__).read_bytes()),'hardware_timing_measured':False,
        'limits':'Fixed PVR registers/VRAM and coherent RAM are modeled; stage-main stops at first display capture. No physical display or complete launch is proved.'}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--report',type=Path);args=parser.parse_args();result=audit(args.elf)
    text=json.dumps(result,indent=2)+'\n'
    if args.report:args.report.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
