#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded host scanout writes plus actual integer-only SH code/stack budget."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_retail_instructions import audit
from check_vmu_native_worker_gateway import read_elf
from test_smash_menu_loader_runtime import RuntimeCPU


def run(arguments,**kwargs):
    return subprocess.check_output([str(x) for x in arguments],cwd=ROOT,text=True,**kwargs)


class FaultCPU(RuntimeCPU):
    """Actual SH instructions; only fixed PVR reads and VRAM stores are modeled."""
    def __init__(self,elf,registers):
        self.active=False
        super().__init__(b'')
        self.registers=registers;self.pvr_reads=[];self.pvr_writes=[];self.pixel_writes=0
        progress=elf[4]['_kui_smash_return_progress_state'];assert progress[1]==8
        self.progress_address=progress[0];self.progress_writes=[]
        self.vram=bytearray([0x5b])*0x800000;self.low_stack=0xac00c000
        for p in elf[2]:
            if p[0]==1:self.store_block(p[2],elf[0][p[1]:p[1]+p[4]]+bytes(p[5]-p[4]))
        self.active=True

    def read(self,address,count=4):
        if self.active and address in self.registers:
            assert count==4;self.pvr_reads.append(address);return self.registers[address]
        return super().read(address,count)

    def write(self,address,value,count=4):
        if not self.active:return super().write(address,value,count)
        if self.progress_address<=address<self.progress_address+8:
            assert count==4 and address in (self.progress_address,self.progress_address+4)
            self.progress_writes.append((address,value&0xffffffff));self.active=False
            try:return super().write(address,value,count)
            finally:self.active=True
        if address==0xa05f8040:
            assert count==4 and value==0xff0000 and not self.pixel_writes
            self.pvr_writes.append((address,value));return
        if 0xa5000000<=address<0xa5800000:
            at=address-0xa5000000
            assert count in (2,4) and at<=len(self.vram)-count and not (at&(count-1))
            assert not self.pvr_writes
            size=self.registers[0xa05f805c];words=(size&1023)+1
            stride=(words-1+(size>>20&1023))*4;line=words*4
            assert stride and line>=160*count
            fields=[self.registers[0xa05f8050]]
            if self.registers[0xa05f80d0]&16:fields.append(self.registers[0xa05f8054])
            inside=False
            for base in fields:
                if at<base:continue
                row,xbytes=divmod(at-base,stride)
                inside|=8<=row<48 and xbytes%count==0 and 8<=xbytes//count<160 and xbytes+count<=line
            assert inside,(hex(address),count)
            self.vram[at:at+count]=(value&((1<<(count*8))-1)).to_bytes(count,'little')
            self.pixel_writes+=1;return
        # No renderer/helper call may publish RAM state: all actual stores
        # outside VRAM are confined to this caller's private2KiB frame.
        assert 0xac00b800<=address<=0xac00c000-count,(hex(address),count)
        self.low_stack=min(self.low_stack,address)
        return super().write(address,value,count)


def linked_execution(path):
    elf=read_elf(path);entry=elf[4]['_kui_smash_return_fault_display'][0];results=[]
    for mode in (0,1,3):
        for interlace in (False,True):
            bpp=4 if mode==3 else 2;words=640*bpp//4;base=0x120000
            registers={0xa05f8044:0x800001|(mode<<2),0xa05f8050:base,
                0xa05f8054:base+640*bpp,0xa05f80d0:0x150 if interlace else 0x100,
                0xa05f805c:(words-1)|((239 if interlace else 479)<<10)|((words+1 if interlace else 1)<<20)}
            results.append(execute_case(elf,entry,registers,'RGB'+{0:'555',1:'565',3:'0888'}[mode],interlace,False))
    for name,cfg,size,base in (('packed24',9,319|(479<<10)|(1<<20),0),
            ('disabled',4,319|(479<<10)|(1<<20),0),
            ('zero_modulo',5,319|(479<<10),0),('bad_base',5,319|(479<<10)|(1<<20),0xffffffff)):
        registers={0xa05f8044:cfg,0xa05f8050:base,0xa05f8054:0,0xa05f80d0:0x100,0xa05f805c:size}
        results.append(execute_case(elf,entry,registers,name,False,True))
    progress=execute_progress_case(elf)
    return dict(elf_sha256=hashlib.sha256(elf[0]).hexdigest(),reporter_entry=hex(entry),cases=results,
        progress_helper=progress,
        all_reporter_instructions_executed_without_substitution=True,
        only_fixed_PVR_reads_and_validated_VRAM_or_border_stores=True)


def execute_progress_case(elf):
    registers={at:0xffffffff for at in (0xa05f8044,0xa05f8050,0xa05f8054,0xa05f80d0,0xa05f805c)}
    cpu=FaultCPU(elf,registers);stack=0xac00c000;sentinel=0x8c003000
    cpu.r[4:6]=[0x10a,0x89abcdef];cpu.r[15]=stack;cpu.pr=sentinel
    cpu.pc=elf[4]['_kui_smash_return_progress'][0];saved=cpu.r[8:15].copy();sr=cpu.sr;ccr=cpu.original_ccr
    for _ in range(1000):
        if cpu.pc==sentinel:break
        cpu.step()
    else:raise AssertionError('Actual RAM-only progress helper exceeded instruction bound')
    assert cpu.r[15]==stack and cpu.r[8:15]==saved and cpu.original_ccr==ccr and cpu.sr&~1==sr&~1
    assert cpu.progress_writes==[(cpu.progress_address+4,0x89abcdef),(cpu.progress_address,0x10a)]
    assert cpu.block(cpu.progress_address,8)==bytes.fromhex('0a010000efcdab89')
    assert not cpu.pvr_reads and not cpu.pvr_writes and not cpu.pixel_writes
    assert cpu.vram==bytearray([0x5b])*0x800000
    return dict(actual_SH_instructions=cpu.instructions,progress_state_bytes=8,
        detail_published_before_phase=True,PVR_reads=0,PVR_writes=0,VRAM_writes=0,
        actual_stack_bytes=stack-cpu.low_stack)


def execute_case(elf,entry,registers,name,interlace,fallback):
    cpu=FaultCPU(elf,registers);stack=0xac00c000;sentinel=0x8c003000
    cpu.r[4:6]=[1,0x89abcdef];cpu.r[15]=stack;cpu.pc=entry;cpu.pr=sentinel
    saved=list(cpu.r[8:15]);sr=cpu.sr;ccr=cpu.original_ccr
    for _ in range(500000):
        if cpu.pc==sentinel:break
        cpu.step()
    else:raise AssertionError('Actual linked fault renderer exceeded instruction bound')
    assert cpu.r[15]==stack and cpu.r[8:15]==saved and cpu.original_ccr==ccr and cpu.sr&~1==sr&~1
    assert not cpu.progress_writes,'Fault rendering changed successful-path progress state'
    assert sorted(cpu.pvr_reads)==sorted(registers) and len(cpu.pvr_reads)==5
    if fallback:
        assert not cpu.pixel_writes and cpu.pvr_writes==[(0xa05f8040,0xff0000)]
    else:
        assert cpu.pixel_writes and not cpu.pvr_writes
        mode=registers[0xa05f8044]>>2&3;bpp=4 if mode==3 else 2;stride=640*bpp*(2 if interlace else 1)
        white={0:0x7fff,1:0xffff,3:0xffffff}[mode];red={0:0x5000,1:0xa000,3:0xa00000}[mode]
        fields=[registers[0xa05f8050]]+([registers[0xa05f8054]] if interlace else [])
        for base in fields:
            for x,y,color in ((12,12,white),(20,12,red),(144,12,red),(148,12,white),(12,30,red),(14,30,white)):
                at=base+y*stride+x*bpp;assert int.from_bytes(cpu.vram[at:at+bpp],'little')==color
            for at in (base,base+7*stride,base+48*stride,base+8*stride+160*bpp):assert cpu.vram[at]==0x5b
    return dict(format=name,interlaced=interlace,fallback=fallback,actual_SH_instructions=cpu.instructions,
        pixel_stores=cpu.pixel_writes,border_stores=len(cpu.pvr_writes),actual_stack_bytes=stack-cpu.low_stack)


def check(directory,cc,prefix,actual_elf=None):
    source=ROOT/'src/loader/smash_return_fault_display.c'
    fixture=ROOT/'tests/test_smash_return_fault_display.c'
    native=directory/'fault.o';linked=directory/'fault.elf';host=directory/'host'
    common=['-std=c11','-Wall','-Wextra','-Werror','-Wpedantic']
    run([cc,*common,'-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
        '-fno-pie','-no-pie','-DKUI_SMASH_RETURN_FAULT_HOST_TEST',fixture,source,'-o',host])
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
    host_result=run([host],env=env).strip();assert host_result.startswith('PASS early return fault display:')
    cpu=['-m4-single','-ml','-mdiv=call-div1','-mfixed-range=fr0-fr15,fpul-fpul']
    run([prefix+'gcc',*common,*cpu,'-Os','-ffreestanding','-fno-builtin','-fno-stack-protector',
        '-fno-pic','-ffunction-sections','-fdata-sections','-fstack-usage','-fcallgraph-info=su',
        '-c',source,'-o',native])
    undefined=run([prefix+'nm','-u',native]);assert not undefined.strip(),undefined
    script=directory/'fault.ld'
    script.write_text('SECTIONS { . = 0xac008300; .text : { *(.text*) *(.rodata*) } '
        '/DISCARD/ : { *(.comment) *(.note*) *(.stack*) } }\n')
    run([prefix+'gcc',*cpu,'-nostdlib','-Wl,-T,'+str(script)+',-e,_kui_smash_return_fault_display',
        native,'-o',linked])
    disassembly=run([prefix+'objdump','-d',linked]);instructions=audit('return-fault-display',disassembly)
    sections=run([prefix+'size','-A',native]);allocated=0
    for line in sections.splitlines():
        match=re.match(r'\s*(\.(?:text|rodata|data|bss)\S*)\s+(\d+)\s+',line)
        if match:allocated+=int(match[2])
    assert 0<allocated<7*1024,sections
    stack={}
    for line in native.with_suffix('.su').read_text().splitlines():
        fields=line.split('\t');assert fields[2]=='static',line
        stack[fields[0].rsplit(':',1)[1]]=int(fields[1])
    # Summing every compiled function safely overestimates the only call chain.
    assert stack and sum(stack.values())<2048,stack
    regs=ROOT/'.deps/kos/kernel/arch/dreamcast/include/dc/pvr/pvr_regs.h'
    video=ROOT/'.deps/kos/kernel/arch/dreamcast/hardware/video.c'
    text=regs.read_text()
    for name,value in [('PVR_BORDER_COLOR',0x40),('PVR_FB_CFG_1',0x44),('PVR_FB_ADDR',0x50),
        ('PVR_FB_IL_ADDR',0x54),('PVR_FB_SIZE',0x5c),('PVR_IL_CFG',0xd0),('PVR_RAM_BASE',0xa5000000)]:
        match=re.search(r'^#define\s+'+name+r'\s+(0x[0-9a-fA-F]+)\b',text,re.M)
        assert match and int(match[1],16)==value,name
    execution=linked_execution(actual_elf or linked)
    return dict(schema='kui.smash.return-fault-display.v1',status='PASS',host=host_result,
        target_allocated_bytes=allocated,conservative_C_stack_bytes=sum(stack.values()),
        target_frames=stack,actual_integer_SH_instructions=instructions,no_undefined_symbols=True,
        actual_linked_SH_execution=execution,
        source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in (source,source.with_suffix('.h'),fixture,Path(__file__),regs,video)},
        limits=['No console output or physical display timing was measured.',
            'Packed RGB888, disabled scanout and unsupported or unsafe geometry use only a fixed red border.',
            'The terminal reporter never changes timing, enable, base or stride registers.'])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default='cc')
    parser.add_argument('--prefix',default=str(ROOT/'.deps/sh-elf/bin/sh-elf-'))
    parser.add_argument('--report',type=Path)
    parser.add_argument('--elf',type=Path,help='Optionally execute the reporter from a final production return ELF')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='kui-return-fault-') as path:
        result=check(Path(path),args.cc,args.prefix,args.elf)
    output=json.dumps(result,indent=2)+'\n'
    if args.report:args.report.write_text(output)
    print(output,end='')
