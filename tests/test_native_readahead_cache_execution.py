#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute actual linked SH cache copies; count transfers, never console time.

The existing bounded integer model executes the compiled cache functions with
no substituted callees. Differential bytes, exact access widths and the final
validity publication are checked for aligned/unaligned and overlapping sources.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from profile_vmu_native_release_sh import ReleaseSH,STACK,MASK

CACHE,DATA,SOURCE=0xac600000,0xac640000,0xac680000
BLOCKS,BLOCK_BYTES=384,512


class CacheSH(ReleaseSH):
    def __init__(self,path):
        super().__init__(path)
        self.source_range=(0,0)
        self.source_reads=[]

    def read(self,address,count=4):
        low,high=self.source_range
        physical=address&0x1fffffff
        if self.active and low<=physical<high:
            assert physical+count<=high
            self.source_reads.append((physical,count))
        return super().read(address,count)

    def plain(self,op,pc):
        n,m=op>>8&15,op>>4&15
        if op&0xf0ff==0x4018:  # SHLL8 Rn, exact integer shift.
            self.r[n]=(self.r[n]<<8)&MASK
        elif op&0xf00f==0x2004:  # MOV.B Rm,@-Rn, retain the old byte fallback.
            self.r[n]=(self.r[n]-1)&MASK
            self.write(self.r[n],self.r[m],1)
        else:
            return super().plain(op,pc)
        self.executing=pc
        self.instructions+=1


def run_case(path,*,wrapped=False,delta=None,alignment=0,word_copy=True):
    cpu=CacheSH(path)
    head,count=(383,2) if wrapped else (1,1)
    slot=(head+count)%BLOCKS
    destination=DATA+slot*BLOCK_BYTES
    source=SOURCE+alignment if delta is None else destination+delta
    cpu.store_block(CACHE,struct.pack('<5I',DATA,100,head,count,1000))
    cpu.store_block(DATA,bytes((i*37^(i>>3)^0x65)&255 for i in range(BLOCKS*BLOCK_BYTES)))
    if delta is None:
        cpu.store_block(source,bytes((i*29^(i>>2)^alignment)&255 for i in range(BLOCK_BYTES)))
    before=cpu.block(DATA,BLOCKS*BLOCK_BYTES)
    expected=bytearray(before)
    expected[slot*BLOCK_BYTES:(slot+1)*BLOCK_BYTES]=cpu.block(source,BLOCK_BYTES)
    cpu.source_range=(source&0x1fffffff,(source&0x1fffffff)+BLOCK_BYTES)
    cpu.writes=[]
    measured=cpu.call('_kui_native_ra_cache_append',[CACHE,100+count,source])
    assert measured['result']==1
    assert cpu.block(DATA,len(expected))==expected
    assert cpu.block(CACHE,20)==struct.pack('<5I',DATA,100,head,count+1,1000)
    copies=[];publications=[]
    for index,(_,address,value,width) in enumerate(cpu.writes):
        physical=address&0x1fffffff
        if destination&0x1fffffff<=physical<(destination&0x1fffffff)+BLOCK_BYTES:
            copies.append((index,physical,width))
        elif physical==(CACHE&0x1fffffff)+12:
            assert width==4 and value==count+1
            publications.append(index)
        else:
            assert STACK-256<=address<STACK,hex(address)
    width=4 if word_copy and not(source&3) else 1
    assert len(copies)==BLOCK_BYTES//width and all(w==width for _,_,w in copies)
    assert len(cpu.source_reads)==BLOCK_BYTES//width and all(w==width for _,w in cpu.source_reads)
    covered=[byte for _,address,w in copies for byte in range(address,address+w)]
    start=destination&0x1fffffff
    assert sorted(covered)==list(range(start,start+BLOCK_BYTES))
    assert len(publications)==1 and max(i for i,_,_ in copies)<publications[0]
    return dict(wrapped=wrapped,delta=delta,alignment=alignment,
        source_reads=len(cpu.source_reads),destination_writes=len(copies),access_bytes=width,
        instructions=measured['instructions'],publication_after_complete_copy=True)


def exercise(path,word_copy):
    cases=[]
    for wrapped in (False,True):
        for alignment in range(4):
            cases.append(run_case(path,wrapped=wrapped,alignment=alignment,word_copy=word_copy))
        for delta in (-4,-1,0,1,4):
            cases.append(run_case(path,wrapped=wrapped,delta=delta,word_copy=word_copy))
    return cases


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf',type=Path)
    parser.add_argument('--baseline',type=Path)
    parser.add_argument('--json',type=Path)
    args=parser.parse_args()
    digest=hashlib.sha256(args.elf.read_bytes()).hexdigest()
    result=dict(status='passed',elf_sha256=digest,cases=exercise(args.elf,True),console_timing=False)
    assert hashlib.sha256(args.elf.read_bytes()).hexdigest()==digest,'ELF changed during execution proof'
    if args.baseline:
        digest=hashlib.sha256(args.baseline.read_bytes()).hexdigest()
        result['baseline_elf_sha256']=digest
        result['baseline_cases']=exercise(args.baseline,False)
        assert hashlib.sha256(args.baseline.read_bytes()).hexdigest()==digest,'Baseline changed during proof'
    if args.json:
        args.json.write_text(json.dumps(result,indent=2)+'\n')
    print('Linked SH cache copy:',len(result['cases']),
        'cases; aligned128 word loads/stores, unaligned512 byte fallback, exact overlap/wrap bytes and final publication')
    if args.baseline:
        old,new=result['baseline_cases'][0],result['cases'][0]
        print('Aligned source/destination accesses:',old['source_reads'],'+',old['destination_writes'],
            '->',new['source_reads'],'+',new['destination_writes'],'; instructions',old['instructions'],'->',new['instructions'],
            '(not console cycles)')


if __name__=='__main__':main()
