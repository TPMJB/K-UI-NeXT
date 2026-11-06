#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked original-GIN resume with two physical register banks.

One trace executes every whole-RAM purge instruction. Independent suffix
traces exercise cache profiles and active/inactive-bank canaries after that
publication boundary. Cache timing, SDK, media and CE are outside this test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes

MASK=0xffffffff
FRAME,TARGET,CCR=0x8c00f3ac,0xac010000,0xff00001c
RAM,END=0x8c000000,0x8d000000
LINES=(END-RAM)//32
PURGE=struct.pack('<4H',0x00a3,0x7020,0x3012,0x8bfb)

def signed8(v):return v-256 if v&128 else v

class Resume:
    def __init__(self,base,code,pattern,ccr,published=False):
        self.base,self.code=base,code
        self.pc=base+0x20000000
        self.r=[(0xfedcba98+i*0x1234567)&MASK for i in range(16)]
        self.inactive=[(0x89abcdef+i*0x7654321+pattern)&MASK for i in range(8)]
        self.inactive_before=self.inactive.copy()
        self.sr=0x700000f0
        self.macl,self.mach,self.gbr,self.vbr,self.ssr,self.pr,self.spc=1,2,3,4,5,6,7
        self.frame=[0x28,0,0x8c000000,0x8c00f400,0x700000f0,0]+[0]*7
        self.frame += [(pattern^(i*0x1234567))&MASK for i in range(8)]
        self.mem={FRAME+4*i:v for i,v in enumerate(self.frame)}
        self.r[4],self.r[5],self.r[15]=FRAME,ccr,0x8cff0000
        self.original_ccr=ccr
        self.ccr_writes=[];self.bank_changes=[];self.instructions=0
        self.purges=LINES if published else 0
        self.executed_purges=0
        # The closed decoder accepts no FPU instruction. These separate
        # canaries document the state that must survive unchanged.
        self.fpscr=0x00340001;self.fpul=0x01020304
        self.fr=[(0x3f000000+i*0x12345+pattern)&MASK for i in range(32)]
        self.fp_before=(self.fpscr,self.fpul,self.fr.copy())

    def normalized(self,a):return (a&0x1fffffff)|0x80000000

    def read(self,a,n=4):
        a=self.normalized(a)
        if a in self.mem:
            assert n==4 and self.purges==LINES,'Frame read before publication'
            return self.mem[a]
        offset=a-self.base
        assert 0<=offset<=len(self.code)-n,hex(a)
        return int.from_bytes(self.code[offset:offset+n],'little')

    def set_sr(self,value):
        if bool(value&0x20000000)!=bool(self.sr&0x20000000):
            self.bank_changes.append((self.sr,value))
            self.r[:8],self.inactive=self.inactive,self.r[:8]
        self.sr=value

    def flag(self,value):self.set_sr((self.sr&~1)|int(bool(value)))

    def plain(self,op,pc):
        self.instructions+=1
        n,m=op>>8&15,op>>4&15
        form=op&0xf0ff
        if op==0x0009:return
        if op>>12==0xd:
            self.r[n]=self.read(((pc+4)&~3)+(op&255)*4)
        elif op>>12==7:self.r[n]=(self.r[n]+signed8(op&255))&MASK
        elif op&0xf00f==0x3002:self.flag(self.r[n]>=self.r[m])
        elif form==0x00a3:
            assert self.purges<LINES and self.r[n]==RAM+self.purges*32
            self.purges+=1;self.executed_purges+=1
        elif op&0xf00f==0x200b:self.r[n]|=self.r[m]
        elif op&0xf00f==0x2002:
            assert self.r[n]==CCR and self.purges==LINES
            assert self.pc&0xe0000000==0xa0000000,'CCR write outside P2'
            self.ccr_writes.append(self.r[m])
            assert self.r[m]==self.original_ccr|0x808
            assert [self.read(pc+2+2*i,2) for i in range(8)]==[9]*8
        elif op&0xf00f==0x6003:self.r[n]=self.r[m]
        elif op&0xf00f==0x6006:
            value=self.read(self.r[m]);self.r[m]=(self.r[m]+4)&MASK;self.r[n]=value
        elif form in (0x4006,0x4016,0x4026,0x4017,0x4027,0x4037):
            name={0x4006:'mach',0x4016:'macl',0x4026:'pr',0x4017:'gbr',0x4027:'vbr',0x4037:'ssr'}[form]
            setattr(self,name,self.read(self.r[n]));self.r[n]=(self.r[n]+4)&MASK
        elif form==0x404e:self.spc=self.r[n]
        elif form==0x400e:self.set_sr(self.r[n])
        else:raise AssertionError('Unmodeled instruction '+hex(op)+' at '+hex(pc))

    def run(self):
        while self.pc!=TARGET:
            assert self.instructions<LINES*5+200,'Resume exceeded instruction bound'
            pc=self.pc;op=self.read(pc,2)
            if op>>8==0x8b:
                self.instructions+=1
                self.pc=(pc+4+2*signed8(op&255))&MASK if not self.sr&1 else pc+2
            elif op==0x002b:
                self.instructions+=1
                self.plain(self.read(pc+2,2),pc+2)
                self.set_sr(self.ssr);self.pc=self.spc
            else:
                self.plain(op,pc);self.pc=(pc+2)&MASK
        expected=list(reversed(self.frame[6:]))+[0x8c00f400]
        assert self.r==expected
        assert (self.macl,self.mach,self.gbr,self.vbr,self.sr,self.pr)==tuple(self.frame[:6])
        assert self.inactive==self.inactive_before and not self.bank_changes
        assert self.fp_before==(self.fpscr,self.fpul,self.fr)
        assert self.ccr_writes==[self.original_ccr|0x808]
        return dict(instructions=self.instructions,purge_lines_executed=self.executed_purges,
            ccr=hex(self.original_ccr),active_and_inactive_banks_preserved=True)

def audit(elf):
    image=read_elf(elf)
    base,code,_=symbol_bytes(image,'_kui_smash_ce_gin_resume',2)
    assert code.count(PURGE)==1
    purge=code.index(PURGE);assert purge==4
    full=Resume(base,code,0xdeadbeef,0x109).run()
    assert full['purge_lines_executed']==LINES
    suffix=[]
    for pattern in (0,0x12345678,0xa5a5a5a5,0xffffffff):
        for ccr in (0,0x100,0x109,0x129,0x909):
            cpu=Resume(base,code,pattern,ccr,published=True)
            cpu.pc=base+0x20000000+purge+len(PURGE)
            suffix.append(cpu.run())
    # Reject an incorrect restore and an actual SR-write/bank-toggle mutant.
    mutations=[]
    original=struct.pack('<H',0x6ef6) # MOV.L @R15+,R14
    assert code.count(original)==1
    changed=code.replace(original,struct.pack('<H',0x6df6),1)
    mutations.append(('frame',changed))
    # Replace MOV R4,R15 with an actual LDC R0,SR. R0 still contains the
    # restored CCR word; the decoder must switch physical register banks,
    # exposing this otherwise destructive loss of the captured RB1 state.
    stack_move=struct.pack('<H',0x6f43);assert code.count(stack_move)==1
    mutations.append(('bank',code.replace(stack_move,struct.pack('<H',0x400e),1)))
    for name,changed in mutations:
        cpu=Resume(base,changed,0x31415926,0x109,published=True)
        cpu.pc=base+0x20000000+purge+len(PURGE)
        try:cpu.run()
        except AssertionError:
            if name=='bank':assert cpu.bank_changes,'Bank mutation was not executed'
        else:raise AssertionError('Restore mutation escaped the model')
    return dict(schema='kui.smash-ce.original-gin-resume.v1',status='PASS',
        elf_sha256=hashlib.sha256(image[0]).hexdigest(),full_purge_trace=full,
        suffix_cases=len(suffix),mutation_refusals=len(mutations),frame=hex(FRAME),target=hex(TARGET),
        no_FPU_instructions=True,no_SR_bank_change=True,
        limits=['Executes the actual linked resume instructions, including every whole-RAM purge in one trace.',
            'Suffix cases start after the independently exercised publication boundary; no purge instruction is folded.',
            'Models register banks and required publication order, not cache timing, original SDK, media or CE gameplay.'])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--elf',type=Path,required=True)
    p.add_argument('--report',type=Path);a=p.parse_args();r=audit(a.elf)
    rendered=json.dumps(r,indent=2)+'\n'
    if a.report:a.report.write_text(rendered)
    print(rendered,end='')
