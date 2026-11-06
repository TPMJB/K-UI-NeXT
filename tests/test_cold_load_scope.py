#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked cold-call cache/ABI scope, with a bounded ABI adversary."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from test_smash_menu_loader_entry import LoaderCPU,CCR

MMU=0xff000010
STACK=0xac00f000
CALLBACK=0xac400000
RETURN=0xac500000
MASK=0xffffffff


class ScopeCPU(LoaderCPU):
    def __init__(self,image,ccr,mmu,entry,result,mask,seed,body=None):
        super().__init__(ccr)
        for ph in image[2]:
            if ph[0]==1:self.store_block(ph[2],image[0][ph[1]:ph[1]+ph[4]])
        self.begin,linked,_=symbol_bytes(image,'_kui_cold_load_call3',2)
        if body is not None:self.store_block(self.begin,body)
        self.r=[(0x13579bdf+seed*37+i*0x1234567)&MASK for i in range(16)]
        self.r[4:8]=[entry,0xf1234567,0xac600003,0x76543210]
        self.r[15],self.pc,self.pr=STACK,self.begin,RETURN
        self.sr=0x70000000|(mask<<4)|(seed&1)
        self.fpscr=0x40001^seed;self.fr=[(seed+i*0x1234567)&MASK for i in range(32)]
        self.before=(self.r.copy(),self.pr,self.sr,self.fpscr,self.fr.copy(),self.gbr)
        self.owner_ccr,self.mmu,self.entry,self.result=ccr,mmu,entry,result
        self.ccr_writes,self.calls=[],[]
        self.wait=0
        self.eligible=(ccr in (0,0x100) and not mmu&1 and
            entry&0xe0000000 in (0x80000000,0xa0000000) and not entry&1 and
            0x0c000000<=entry&0x1fffffff<0x0d000000)

    def read(self,address,count=4):
        if address==MMU:
            assert count==4;return self.mmu
        return super().read(address,count)

    def write(self,address,value,count=4):
        if not hasattr(self,'before'):
            return super().write(address,value,count)
        if address==CCR:
            assert count==4 and self.pc&0xe0000000==0xa0000000
            assert self.eligible and not self.wait
            expected=0x908 if not self.ccr_writes else self.owner_ccr|0x808
            assert value==expected and len(self.ccr_writes)<2
            self.original_ccr=value&~0x808
            self.ccr_writes.append(value);self.wait=8
            return
        assert count==4 and STACK-16<=address<STACK
        return super().write(address,value,count)

    def plain(self,op,pc):
        if self.wait:
            assert op==9,'CCR update lacks its eight settling NOPs'
            self.wait-=1
        assert op>>12!=15,'Scope introduced an FPU instruction'
        return super().plain(op,pc)

    def callback(self):
        assert not self.wait and not self.calls
        expected=self.entry&0xdfffffff if self.eligible else self.entry
        assert self.pc==expected
        assert self.r[4:7]==self.before[0][5:8]
        assert self.r[15]==STACK-16
        assert self.sr&~1==self.before[2]&~1
        assert self.original_ccr==(0x100 if self.eligible else self.owner_ccr)
        self.calls.append(self.pc)
        after=self.pr
        for i in range(8):self.r[i]=(0x89abcdef+i*0x111111)&MASK
        self.r[0]=self.result
        self.pr^=0x2468ace0
        self.sr^=1
        self.pc=after

    def run(self):
        for _ in range(180):
            if self.pc==RETURN:break
            if self.pc in (self.entry,self.entry&0xdfffffff):self.callback()
            else:self.step()
        else:raise AssertionError('Cold scope exceeded bounded instruction count')
        assert self.pc==RETURN and len(self.calls)==1 and not self.wait
        assert self.r[8:]==self.before[0][8:]
        assert self.pr==self.before[1] and self.r[0]==self.result
        assert self.sr&~1==self.before[2]&~1
        assert (self.fpscr,self.fr,self.gbr)==self.before[3:]
        assert self.original_ccr==self.owner_ccr
        assert self.ccr_writes==([0x908,self.owner_ccr|0x808] if self.eligible else [])
        return self


def audit(path,self_test=True):
    image=read_elf(path)
    address,body,owner=symbol_bytes(image,'_kui_cold_load_call3',2)
    assert owner[1]==1 and owner[2]&7==6
    cases=0
    for seed in range(3):
        for mask in range(16):
            for ccr in (0,0x100,1,4,0x20,0x80,0x101,0x105,0x109,0x909,0xffffffff):
                for mmu in (0,1):
                    for result in (0,3):
                        ScopeCPU(image,ccr,mmu,CALLBACK,result,mask,seed).run();cases+=1
    entries=(0x8c400000,0xac400001,0xac000000,0xacffffff,0xabfffffe,0xad000000,
             0x0c400000,0xcc400000,0xec400000,0x00400000)
    for entry in entries:
        ScopeCPU(image,0,0,entry,0,15,1).run();cases+=1
    mutations=[]
    for old,new in ((0x908,0x909),(0x808,0x809),(0xdfffffff,0xffffffff),
                    (0xfffffeff,0xfffffffe),(0x0c000000,0x0b000000),
                    (0xa0000000,0xe0000000)):
        changed=bytearray(body);at=changed.index(struct.pack('<I',old))
        struct.pack_into('<I',changed,at,new);mutations.append(bytes(changed))
    rejected=0
    if self_test:
        for changed in mutations:
            try:
                for ccr in (0,0x100,1):
                    for entry in (CALLBACK,0xec400000,0xabfffffe):
                        ScopeCPU(image,ccr,0,entry,3,15,1,changed).run()
            except (AssertionError,KeyError,struct.error):rejected+=1
            else:raise AssertionError('Unsafe cold scope mutation accepted')
    return dict(schema='kui.cold-load-scope.v1',status='PASS',elf_sha256=hashlib.sha256(image[0]).hexdigest(),
        scope_sha256=hashlib.sha256(body).hexdigest(),scope_bytes=len(body),actual_SH_cases=cases,
        mutation_rejections=rejected,private_frame_bytes=16,operand_cache_enabled=False,
        callback_enters_P1=True,unknown_profiles_unchanged=True,caller_CCR_restored=True,
        no_worker_or_game_RAM_reserved=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--elf',type=Path,required=True);p.add_argument('--report',type=Path)
    a=p.parse_args();result=audit(a.elf);rendered=json.dumps(result,indent=2)+'\n'
    if a.report:a.report.write_text(rendered)
    print(rendered,end='')
