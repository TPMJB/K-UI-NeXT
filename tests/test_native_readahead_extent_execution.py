#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare actual linked extent validators; count work, never console time.

Both complete functions execute with no substituted callees. The old ELF is
supplied independently; synthetic maps cover physical aliases, retained copies,
fragment order, shared-container boundaries and late overflow/corruption.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from profile_vmu_native_release_sh import ReleaseSH,MASK,STACK
from test_vmu_native_heap_execution import signed
from check_vmu_native_worker_gateway import read_elf,symbol_bytes

FROZEN_EXTENT=(1406,'e6627d94ccfac3bfd5c8433be526f2f575a06c0f2b14329e8d330da3b565792e')

class ExtentSH(ReleaseSH):
    def __init__(self,path):
        super().__init__(path)
        self.map_range=(0,0);self.map_reads=0

    def read(self,address,count=4):
        first,end=self.map_range;physical=address&0x1fffffff
        if self.active and first<=physical<end:
            assert physical+count<=end
            self.map_reads+=1
        return super().read(address,count)

    def plain(self,op,pc):
        n,m=op>>8&15,op>>4&15
        if op in (0x0008,0x0018):  # CLRT / SETT.
            self.flag(op==0x0018)
        elif op&0xf00f in (0x300e,0x300a):  # ADDC / SUBC.
            a,b,t=self.r[n],self.r[m],self.sr&1
            value=a+b+t if op&15==14 else a-b-t
            self.r[n]=value&MASK;self.flag(value>MASK if op&15==14 else value<0)
        elif op&0xf00f in (0x3005,0x300d):  # DMULU.L / DMULS.L.
            a,b=self.r[n],self.r[m]
            if op&15==13:a,b=signed(a,32),signed(b,32)
            value=(a*b)&((1<<64)-1);self.mach,self.macl=value>>32,value&MASK
        elif op&0xf0ff==0x000a:  # STS MACH,Rn.
            self.r[n]=self.mach
        elif op&0xf00f==0x0006:  # MOV.L Rm,@(R0,Rn).
            self.write((self.r[0]+self.r[n])&MASK,self.r[m])
        elif op&0xf00f==0x0004:  # MOV.B Rm,@(R0,Rn).
            self.write((self.r[0]+self.r[n])&MASK,self.r[m],1)
        elif op&0xf00f==0x000c:  # MOV.B @(R0,Rm),Rn.
            self.r[n]=signed(self.read((self.r[0]+self.r[m])&MASK,1),8)&MASK
        elif op&0xf00f==0x200e:  # MULU.W Rm,Rn.
            self.macl=(self.r[n]&65535)*(self.r[m]&65535)
        elif op>>8==0x84:  # MOV.B @(disp,Rm),R0.
            self.r[0]=signed(self.read(self.r[m]+(op&15),1),8)&MASK
        elif op&0xf0ff in (0x4018,0x4019):  # SHLL8 / SHLR8.
            self.r[n]=(self.r[n]<<8)&MASK if op&255==24 else self.r[n]>>8
        elif op&0xf0ff in (0x4011,0x4015):  # CMP/PZ / CMP/PL.
            value=signed(self.r[n],32)
            self.flag(value>=0 if op&255==0x11 else value>0)
        else:
            return super().plain(op,pc)
        self.executing=pc;self.instructions+=1


def manifest(extents,reverse=False):
    raw=bytearray(252+64*12)
    struct.pack_into('<QQQII',raw,0,1000000,1,1000000,1,extents)
    struct.pack_into('<IIHBB',raw,252,45000,45000+extents,1,extents,0x84)
    for i in range(extents):
        lba=1000+(extents-i-1 if reverse else i)*16
        struct.pack_into('<III',raw,264+i*12,i*4,lba,4)
    return raw


def shared_boundary(offset=304):
    raw=manifest(2)
    struct.pack_into('<II',raw,24,2,2)
    struct.pack_into('<IIHBB',raw,252,45000,45001,2,1,4)
    struct.pack_into('<IIHBB',raw,264,45001,45002,3|((offset&255)<<8),1,4|((offset&256)>>5))
    struct.pack_into('<III',raw,276,0,1000,5)
    struct.pack_into('<III',raw,288,0,1004,6)
    return raw


def cases():
    for count in (1,8,32,63):
        for reverse in (False,True):
            query=1000+(count-1)*16
            yield f'fragments-{count}-reverse-{int(reverse)}',manifest(count,reverse),query,query+4
    yield 'shared-boundary',shared_boundary(),1004,1005
    yield 'shared-byte-overlap',shared_boundary(303),1004,0
    valid=manifest(63)
    for field,value in ((24,0),(24,65),(28,64),(264,1),(268,0),(272,0)):
        raw=bytearray(valid);struct.pack_into('<I',raw,field,value)
        yield f'malformed-{field}-{value}',raw,1000,0
    raw=bytearray(valid);struct.pack_into('<I',raw,264+62*12+4,1000)
    yield 'late-overlap',raw,1000,0
    raw=bytearray(valid);struct.pack_into('<QQQ',raw,0,1<<32,1,1<<32)
    struct.pack_into('<I',raw,264+62*12+4,MASK-1)
    yield 'late-end-above-u32',raw,1000,0
    raw=manifest(1);struct.pack_into('<QQQ',raw,0,1<<32,1,1<<32)
    struct.pack_into('<I',raw,268,MASK-4)
    yield 'largest-admitted-end',raw,MASK-1,MASK
    struct.pack_into('<I',raw,268,MASK-3)
    yield 'end-at-two-to-32',raw,MASK-1,0
    struct.pack_into('<I',raw,272,MASK)
    yield 'end-wrap',raw,MASK-1,0
    yield 'no-owner',valid,MASK,0


def execute(path,raw,address,query):
    cpu=ExtentSH(path);cpu.store_block(address,raw)
    # Compare an independently retained byte copy as well as P1/P2 aliases.
    cpu.map_range=(address&0x1fffffff,(address&0x1fffffff)+len(raw))
    before=cpu.block(address,len(raw));cpu.writes=[]
    result=cpu.call('_kui_native_ra_extent_end',[address,query])
    assert cpu.block(address,len(raw))==before
    assert all(STACK-512<=at<STACK for _,at,_,_ in cpu.writes)
    result['map_reads']=cpu.map_reads
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf',type=Path)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--json',type=Path)
    args=parser.parse_args();rows=[]
    digests={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (args.elf,args.baseline)}
    body=symbol_bytes(read_elf(args.baseline),'_kui_native_ra_extent_end',2)[1]
    assert (len(body),hashlib.sha256(body).hexdigest())==FROZEN_EXTENT,'Old independent validator differs'
    for name,raw,query,expected in cases():
        for address in (0x8c690000,0xac690000,0xac6a0000):
            old=execute(args.baseline,raw,address,query)
            new=execute(args.elf,raw,address,query)
            assert old['result']==new['result']==expected,(name,old,new,expected)
            if name.startswith('fragments-63-'):
                assert new['instructions']<old['instructions'] and new['map_reads']<old['map_reads']
            if name.startswith('fragments-1-') or name=='largest-admitted-end':
                assert new['instructions']<=old['instructions'] and new['map_reads']<=old['map_reads']
            rows.append(dict(case=name,map_address=hex(address),baseline=old,candidate=new))
    assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==digest for p,digest in digests.items())
    report=dict(status='passed',elf_sha256=digests,cases=rows,console_timing=False,
        proof='Actual linked functions; all map contents unchanged; only private stack writes; no substituted callees')
    if args.json:args.json.write_text(json.dumps(report,indent=2)+'\n')
    print('Linked SH extent validation:',len(rows),'cases; both map aliases/private copy, shared boundaries, late overlaps and64-bit overflow refusal')
    row=next(r for r in rows if r['case']=='fragments-63-reverse-1' and r['map_address']=='0xac690000')
    print('63-extent reverse map: instructions',row['baseline']['instructions'],'->',row['candidate']['instructions'],
        '; map reads',row['baseline']['map_reads'],'->',row['candidate']['map_reads'],'(not console cycles)')


if __name__=='__main__':main()
