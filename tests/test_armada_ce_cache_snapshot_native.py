#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the authored SH leaf's actual instructions in a bounded RAM model.

The decoder supports only instructions emitted by this leaf. Unknown
instructions, writes outside the shared job/private stack, P1 data reads,
unexpected hardware addresses and unbounded execution are test failures.
This is an instruction/ABI/coherence contract test, not a cycle model.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

EH=struct.Struct('<16sHHIIIIIHHHHHH')
SH=struct.Struct('<IIIIIIIIII')
SYM=struct.Struct('<IIIBBH')
BASE=0xacfcc000
JOB=0xacfcf000
SP=0xacfcfe00
# Stop immediately after RTS reaches a cached caller. The leaf's entire
# cache-array window and delay slot must finish through its P2 address first.
STOP=0x8cfca300
CCR=0xff00001c
U32=0xffffffff


def signed(value,bits):
    return value-(1<<bits) if value&(1<<(bits-1)) else value


def load_elf(path):
    raw=Path(path).read_bytes();h=EH.unpack_from(raw)
    sections=[SH.unpack_from(raw,h[6]+i*SH.size) for i in range(h[12])]
    names_section=sections[h[13]]
    names=raw[names_section[4]:names_section[4]+names_section[5]]
    sections_by_name={names[s[0]:names.find(b'\0',s[0])].decode():s for s in sections}
    symbols={}
    for s in sections:
        if s[1]!=2:continue
        strings=sections[s[6]];strings=raw[strings[4]:strings[4]+strings[5]]
        for at in range(s[4],s[4]+s[5],SYM.size):
            n,value,size,info,other,index=SYM.unpack_from(raw,at)
            label=strings[n:strings.find(b'\0',n)].decode()
            symbols[label]=(value,size)
    start,size=symbols['_kui_armada_ce_cache_snapshot']
    section=next(s for s in sections if s[2]&2 and s[3]<=start and start+size<=s[3]+s[5])
    at=section[4]+start-section[3]
    return raw[at:at+size],start,symbols


class Machine:
    def __init__(self,body,base=None,job=None,va=0x08035100,p1=0x8cf95100,
                 page=0x0cf9517d,mask=255,ccr=0x105,sr=0x700000f0):
        base=BASE if base is None else base;job=JOB if job is None else job
        self.body=body;self.base=base;self.job=job
        self.r=[(0x13570000+i*0x1021)&U32 for i in range(16)]
        self.r[4]=job;self.r[15]=SP;self.original=self.r.copy()
        self.pr=STOP;self.sr=sr;self.original_sr=sr;self.pc=base;self.pending=None
        self.memory={job:va,job+4:p1,job+8:page,job+12:mask,CCR:ccr}
        for i in range(8):self.memory[job+16+4*i]=0xa5a5a5a5
        for i in range(3):self.memory[job+48+4*i]=0
        self.tag=(p1&0xfe0)|0xf4000000
        self.data=(p1&0xfe0)|0xf5000000
        self.ram=p1|0x20000000
        self.allowed_data=set()
        for color in range(4):
            self.memory[self.tag+4096*color]=0
            for i in range(8):
                at=self.data+4096*color+4*i;self.memory[at]=0x22220000+i
                self.allowed_data.add(at)
        for i in range(8):
            self.memory[self.ram+4*i]=0x11110000+i
            self.allowed_data.add(self.ram+4*i)
        self.visits=Counter();self.mutations={};self.reads=[];self.writes=[]
        self.steps=0;self.max_stack=0;self.last_array=-100;self.trace=[]

    def read(self,address):
        assert not(address&3),('unaligned read',hex(address))
        self.visits[address]+=1
        if (address,self.visits[address]) in self.mutations:
            self.memory[address]=self.mutations[address,self.visits[address]]
        if self.base<=address and address+4<=self.base+len(self.body):
            return struct.unpack_from('<I',self.body,address-self.base)[0]
        allowed=(self.job<=address<self.job+60 or SP-40<=address<SP or address==CCR or
                 address in self.allowed_data or address in {self.tag+i*4096 for i in range(4)})
        assert allowed,('unexpected read',hex(address))
        assert address&0xe0000000!=0x80000000,('P1 data read',hex(address))
        self.reads.append(address)
        if address&0xff000000 in (0xf4000000,0xf5000000):self.last_array=self.steps
        return self.memory.get(address,0)

    def write(self,address,value):
        assert not(address&3),('unaligned write',hex(address))
        assert self.job+16<=address<self.job+60 or SP-40<=address<SP,('outside private output',hex(address))
        self.memory[address]=value&U32;self.writes.append(address)

    def t(self,value):self.sr=(self.sr&~1)|int(bool(value))

    def run(self):
        while self.pc!=STOP:
            assert self.steps<5000,'leaf did not terminate'
            pc=self.pc;assert self.base<=pc<self.base+len(self.body) and not(pc&1)
            op=struct.unpack_from('<H',self.body,pc-self.base)[0]
            self.steps+=1;self.trace.append((pc,op));n=(op>>8)&15;m=(op>>4)&15
            prior=self.pending;self.pending=None;next_pc=pc+2
            if op==0x0009:pass
            elif op==0x000b:
                assert prior is None;self.pending=self.pr
            elif op&0xf000==0xa000:
                assert prior is None;self.pending=(pc+4+2*signed(op&4095,12))&U32
            elif op&0xff00 in (0x8900,0x8b00):
                assert prior is None
                if bool(self.sr&1)==(op&0xff00==0x8900):next_pc=pc+4+2*signed(op&255,8)
            elif op&0xf000==0xe000:self.r[n]=signed(op&255,8)&U32
            elif op&0xf000==0x7000:self.r[n]=(self.r[n]+signed(op&255,8))&U32
            elif op&0xf000==0xd000:self.r[n]=self.read(((pc+4)&~3)+4*(op&255))
            elif op&0xff00==0xc800:self.t(not(self.r[0]&(op&255)))
            elif op&0xff00==0xc900:self.r[0]&=op&255
            elif op&0xff00==0x8800:self.t(self.r[0]==(signed(op&255,8)&U32))
            elif op&0xf0ff==0x0002:self.r[n]=self.sr
            elif op&0xf0ff==0x4011:self.t(not(self.r[n]&0x80000000))
            elif op&0xf0ff==0x4010:
                self.r[n]=(self.r[n]-1)&U32;self.t(self.r[n]==0)
            elif op&0xf0ff==0x4001:
                self.t(self.r[n]&1);self.r[n]>>=1
            elif op&0xf0ff==0x4019:self.r[n]>>=8
            elif op&0xf00f==0x6003:self.r[n]=self.r[m]
            elif op&0xf00f==0x6002:self.r[n]=self.read(self.r[m])
            elif op&0xf00f==0x6006:
                self.r[n]=self.read(self.r[m]);self.r[m]=(self.r[m]+4)&U32
            elif op&0xf00f==0x2002:self.write(self.r[n],self.r[m])
            elif op&0xf00f==0x2006:
                self.r[n]=(self.r[n]-4)&U32;self.write(self.r[n],self.r[m])
            elif op&0xf000==0x5000:self.r[n]=self.read(self.r[m]+4*(op&15))
            elif op&0xf000==0x1000:self.write(self.r[n]+4*(op&15),self.r[m])
            elif op&0xf00f==0x3000:self.t(self.r[n]==self.r[m])
            elif op&0xf00f==0x3002:self.t(self.r[n]>=self.r[m])
            elif op&0xf00f==0x3006:self.t(self.r[n]>self.r[m])
            elif op&0xf00f==0x300c:self.r[n]=(self.r[n]+self.r[m])&U32
            elif op&0xf00f==0x2008:self.t(not(self.r[n]&self.r[m]))
            elif op&0xf00f==0x2009:self.r[n]&=self.r[m]
            elif op&0xf00f==0x200a:self.r[n]^=self.r[m]
            elif op&0xf00f==0x200b:self.r[n]|=self.r[m]
            else:raise AssertionError(('unsupported instruction',hex(pc),hex(op)))
            self.max_stack=max(self.max_stack,SP-self.r[15]);self.pc=prior if prior is not None else next_pc
        assert self.r[8:16]==self.original[8:16],'callee register/SP changed'
        assert self.sr&~1==self.original_sr&~1,'SR mode/mask changed'
        assert self.steps-self.last_array>=8,'array-to-cached return fence too short'
        return self.r[0]


def exercise(body,write_layout=False):
    checks=0
    def check(condition):
        nonlocal checks
        checks+=1;assert condition
    for mask in range(1,256):
        for cached in (False,True):
            m=Machine(body,mask=mask);color=(m.memory[JOB]>>12)&3
            if cached:m.memory[m.tag+4096*color]=(m.memory[JOB+4]&0x1ffffc00)|3
            check(m.run()==0);check(m.max_stack==40)
            for i in range(8):check(m.memory[JOB+16+4*i]==((0x22220000 if cached else 0x11110000)+i if mask&(1<<i) else 0xa5a5a5a5))
            data_reads=sum(address in m.allowed_data for address in m.reads)
            check(data_reads==2*mask.bit_count())
    for color in range(4):
        for dirty_color in range(4):
            m=Machine(body,va=0x08000100+color*4096)
            m.memory[m.tag+dirty_color*4096]=(m.memory[JOB+4]&0x1ffffc00)|3
            check(m.run()==(0 if dirty_color==color else 1))
    for fields in ({'mask':0},{'mask':256},{'mask':U32},{'va':0x88035100},{'va':0x08035104},
                   {'va':0x08035120},{'p1':0xacf95100},{'p1':0x8c00ffe0},{'p1':0x8d000000},
                   {'p1':0x8cfca000,'va':0x08035000,'page':0x0cfca17d},
                   {'page':0x0cf9517c},{'page':0x0cf9417d},{'sr':0x700000e0}):
        m=Machine(body,**fields);check(m.run()==3)
        check(not any(address&0xff000000 in (0xf4000000,0xf5000000) for address in m.reads))
    for ccr in (0,0x125,0x185):
        m=Machine(body,ccr=ccr);check(m.run()==1)
        check(not any(address&0xff000000 in (0xf4000000,0xf5000000) for address in m.reads))
    for color in range(4):
        m=Machine(body);at=m.tag+color*4096;m.mutations[at,2]=1
        check(m.run()==2);check(m.memory[JOB+48]==at)
    for i in range(8):
        m=Machine(body);at=m.ram+i*4;m.mutations[at,2]=0xdead0000+i
        check(m.run()==2);check(m.memory[JOB+48]==at)
        m=Machine(body);color=(m.memory[JOB]>>12)&3
        m.memory[m.tag+color*4096]=(m.memory[JOB+4]&0x1ffffc00)|3
        at=m.data+color*4096+i*4;m.mutations[at,2]=0xdead0000+i
        check(m.run()==2);check(m.memory[JOB+48]==at)
    # An uncached page may ignore clean cache aliases, but never dirty ones.
    m=Machine(body,page=0x0cf95175);m.memory[m.tag]=0x0cf95001
    check(m.run()==0);check(m.memory[JOB+16]==0x11110000)
    m=Machine(body,page=0x0cf95175);m.memory[m.tag]=0x0cf95003;check(m.run()==1)
    m=Machine(body);m.r[4]+=4;check(m.run()==3);check(not m.writes)
    for p1,va in ((0x8c010000,0x08035000),(0x8cffffe0,0x08035fe0)):
        m=Machine(body,p1=p1,va=va,page=(p1&0x1ffff000)|0x17d);check(m.run()==0)
    private=0x8cfc4000 if write_layout else 0x8cfca000
    for p1,expected in ((private-32,0),(private,3),(0x8cfc8000,3 if write_layout else 0),
                        (0x8cfcffe0,3),(0x8cfd0000,0)):
        va=0x08035000|(p1&0xfff)
        m=Machine(body,p1=p1,va=va,page=(p1&0x1ffff000)|0x17d)
        check(m.run()==expected)
    return checks


def branch_sites(body,target):
    sites=[]
    for at in range(0,len(body),2):
        op=struct.unpack_from('<H',body,at)[0]
        if op&0xff00 in (0x8900,0x8b00) and BASE+at+4+2*signed(op&255,8)==target:
            sites.append(at)
    return sites


def mutation_review(body,symbols,contract,write_layout=False):
    rejected=0
    for at in range(len(body)):
        changed=bytearray(body);changed[at]^=1
        try:contract.check(bytes(changed),JOB,write_layout)
        except ValueError:rejected+=1
        else:raise AssertionError(('accepted native byte mutation',at))
    semantic=[]
    def disable(label,site,configure,expected):
        changed=bytearray(body);struct.pack_into('<H',changed,site,0x0009)
        before=Machine(body);configure(before);assert before.run()==expected
        after=Machine(bytes(changed));configure(after);actual=after.run()
        assert actual!=expected,('guard mutation had no observable test failure',label)
        semantic.append({'guard':label,'expected':expected,'mutated_result':actual,'instruction_offset':site})
    # -Wa,-L preserves these local labels in the standalone test image.
    if '.Lchanged' in symbols:
        dirty=branch_sites(body,symbols['.Ldirty_alias'][0])
        dirty_bf=next(at for at in dirty if struct.unpack_from('<H',body,at)[0]&0xff00==0x8b00)
        def alternate(m):m.memory[m.tag]=(m.memory[JOB+4]&0x1ffffc00)|3
        disable('dirty alternate alias',dirty_bf,alternate,1)
        changed=branch_sites(body,symbols['.Lchanged'][0]);assert len(changed)==2
        def tag_change(m):m.mutations[m.tag,2]=1
        disable('second tag scan',changed[0],tag_change,2)
        def data_change(m):m.mutations[m.ram,2]=0xdead0000
        disable('repeated selected data',changed[1],data_change,2)
        ccr=branch_sites(body,symbols['.Lcache_pre'][0]);assert len(ccr)==1
        def bad_ccr(m):m.memory[CCR]=0
        disable('live CCR profile',ccr[0],bad_ccr,1)
        args=branch_sites(body,symbols['.Largument'][0])
        def bad_sr(m):m.sr=m.original_sr=0x700000e0
        disable('IMASK15 admission',max(args),bad_sr,3)
    return {'complete_byte_mutations_rejected':rejected,'semantic_guard_mutations_detected':semantic}


def main():
    global BASE,JOB
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf',type=Path)
    parser.add_argument('--json',type=Path)
    parser.add_argument('--write-layout',action='store_true')
    args=parser.parse_args();repo=Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='kui-cache-snapshot-') as temporary:
        path=args.elf
        if path is None:
            directory=Path(temporary);obj=directory/'leaf.o';path=directory/'leaf.elf'
            subprocess.run(['sh-elf-gcc','-m4-single','-ml','-Wa,-L','-I'+str(repo/'include'),
                '-DKUI_ARMADA_CE_MAPLE_WRITE_LAYOUT='+str(int(args.write_layout)),
                '-c',str(repo/'src/loader/armada_ce_cache_snapshot.S'),'-o',str(obj)],check=True)
            subprocess.run(['sh-elf-gcc','-nostdlib','-Wl,-Ttext,0xacfcc000','-Wl,-e,_kui_armada_ce_cache_snapshot',
                '-Wl,--defsym,_kui_armada_ce_cache_snapshot_job=0xacfcf000',str(obj),'-o',str(path)],check=True)
        body,base,symbols=load_elf(path)
        if base&0xe0000000!=0xa0000000:
            raise ValueError('Native snapshot instructions/literal pool must execute in P2')
        BASE=base;JOB=symbols['_kui_armada_ce_cache_snapshot_job'][0]
        sys.path.insert(0,str(repo/'tools'))
        import armada_ce_cache_snapshot_contract as contract
        native=contract.check(body,JOB,args.write_layout)
        checks=exercise(body,args.write_layout)
        mutations=mutation_review(body,symbols,contract,args.write_layout)
        report={'instruction_model_checks':checks,'leaf_bytes':len(body),'maximum_stack_bytes':40,
            'shared_job_bytes':60,'unknown_instructions_rejected':True,'private_write_bounds_checked':True,
            'leaf_executes_p2':True,'modeled_return_p1':hex(STOP),
            'callback_free':True,'native_byte_contract':native,'mutations':mutations,
            'scope':'SH instruction/ABI/coherence model; no hardware cycle or performance proof'}
        if args.json:args.json.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2))


if __name__=='__main__':main()
