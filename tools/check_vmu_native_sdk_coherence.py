#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute cold SDK coherency gates, retaining distinct cached/RAM views.

This proves the compiled gate's ordering and original-prefix state, not console
timing. Original startup files are optional, externally supplied and never
embedded. The whole-game cache-aware replay is a separate integration check.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_vmu_native_stage_stack import Machine,canonical
from check_vmu_native_worker_gateway import MASK,read_elf,symbol_bytes

GATE_UNIFORM='_kui_vmu_native_sdk_coherence_uniform_gate'
GATE_VARIABLE='_kui_vmu_native_sdk_coherence_variable_gate'
STATE='_kui_vmu_native_sdk_coherence_state'
TERMINAL='_kui_vmu_native_runtime_fault_hook'
READY=0x4e434f31
MDST=0xa05f6c18


class Gate(Machine):
    def __init__(self,image,kind,seed=0,mask=15,outcome='ok',stack=0x8c2ff000,*,body=None,metadata=None):
        name=(GATE_UNIFORM,GATE_VARIABLE)[kind]
        super().__init__(image,name,seed,mask,stack,body=body)
        self.pc=self.base+0x20000000
        self.kind,self.outcome=kind,outcome
        self.initial_count=seed
        self.state,size,typ,owner=image[4][STATE]
        section=image[3][owner]
        assert size==48 and typ==1 and self.state&0xff000000==0xac000000
        assert section[1]==8 and section[2]&7==3 and section[3]<=self.state<self.state+48<=section[3]+section[5]
        # Original raw manager aliases are part of prefix semantics.
        self.lock,self.manager_cell,self.manager_initial,self.initializer=(
            metadata if metadata is not None else
            (0x8c1dc538,0x8c32d6e4 if not kind else 0x0c32d6e4,
             0x8c32c5e8 if not kind else 0x0c32c5e8,0x8c1da8fc if not kind else 0x8c2d9748))
        self.lock=(self.lock&0x1fffffff)|0x80000000
        self.flag=(self.lock+4)|0xa0000000
        self.resume=self.initializer+12
        self.terminal=image[4][TERMINAL][0]
        values=[self.lock,self.manager_cell,self.manager_initial,self.resume,0,0,0,
                READY if outcome!='ready' else 0,self.flag,kind^(outcome=='kind'),self.initializer,seed]
        self.mem={self.state+4*i:v for i,v in enumerate(values)}
        self.ram_pointer=self.flag-0x20000000  # Stale original P1 flag pointer in RAM.
        self.cached_pointer=self.flag^(4 if outcome=='pointer' else 0)
        self.ram_flag=0x80 if outcome=='p2held' else 0
        self.cached_flag=0x80 if outcome=='p1held' else 0
        self.mdst=1 if outcome=='dma' else 0
        self.shared_lock_line=(self.lock&~31)==((self.lock+4)&~31)
        self.cached_pointer_valid=self.cached_flag_valid=True
        self.sr=(self.sr&~0xf0)|(mask<<4)
        self.initial=(self.r.copy(),self.pr,self.mach,self.macl,self.gbr,self.sr,self.fpscr,self.fr.copy())
        self.events=[];self.owner_writes=[];self.owner_reads=[];self.executed=set();self.purges=0

    def code(self,address,size):
        assert address&0xe0000000==0xa0000000,'cold gate fetched cached code or literal'
        return super().code(address-0x20000000,size)

    def read(self,address):
        assert not address&3,hex(address)
        if self.state<=address<self.state+48:
            self.events.append(('private_read',address));return self.mem[address]
        if address==MDST:self.events.append(('mdst',address));return self.mdst
        if address==self.lock:
            assert ('p2flag',self.flag) in self.events and self.ram_flag==0
            self.events.append(('p1pointer',address));return self.cached_pointer
        if address==self.lock|0xa0000000:
            assert self.purges==2
            self.events.append(('p2pointer',address));return self.ram_pointer
        return struct.unpack('<I',self.code(address,4))[0]

    def read_byte(self,address):
        if address==self.flag:
            assert ('mdst',MDST) in self.events
            self.events.append(('p2flag',address));return self.ram_flag
        assert address==self.lock+4,hex(address)
        assert ('p2flag',self.flag) in self.events and self.ram_flag==0
        self.events.append(('p1flag',address));return self.cached_flag

    def write(self,address,value):
        assert not address&3
        if self.state<=address<self.state+48:
            self.mem[address]=value&MASK;self.events.append(('private_write',address));return
        assert self.purges==2,'owner-stack access before complete coherence handoff'
        assert self.start_sp-(8 if not self.kind else 24)<=address<self.start_sp
        if not self.owner_writes:
            assert self.r[2:4]==self.initial[0][2:4] and self.sr==self.initial[5], 'owner prefix begins before inherited R2/R3/SR restoration'
        self.mem[canonical(address)]=value&MASK;self.owner_writes.append((address,value&MASK))

    def plain(self,word,pc):
        self.executed.add(pc)
        n,m=word>>8&15,word>>4&15
        if word&0xf00f==0x6000:
            value=self.read_byte(self.r[m]);self.r[n]=(value-256 if value&128 else value)&MASK;return
        if word&0xf00f==0x600c:self.r[n]=self.r[m]&255;return
        if word&0xf0ff==0x00a3:
            assert pc&0xe0000000==0xa0000000
            assert self.purges<2 and self.r[n]==self.lock+4*self.purges
            assert self.ram_flag==self.cached_flag==0 and self.cached_pointer==self.flag
            assert ('p1flag',self.lock+4) in self.events and ('p1pointer',self.lock) in self.events
            self.events.append(('purge',self.r[n]))
            # Actual write-back copies zero only after proving both copies
            # unlocked. It never repairs an already-held P2 flag.
            if self.purges==0:
                self.ram_pointer=self.cached_pointer;self.cached_pointer_valid=False
                if self.shared_lock_line:
                    self.ram_flag=self.cached_flag;self.cached_flag_valid=False
            elif self.cached_flag_valid:
                assert not self.shared_lock_line
                self.ram_flag=self.cached_flag;self.cached_flag_valid=False
            self.purges+=1
            if self.purges==2:
                if self.outcome=='postpointer':self.ram_pointer^=4
                if self.outcome=='postflag':self.ram_flag=0x80
            return
        super().plain(word,pc)

    def run(self):
        # Retain Machine's bounded branch/delay-slot execution; stop at the
        # original continuation or before entering the old terminal gateway.
        original_call=self.call
        def stop(target):
            if target in (self.resume,self.terminal):self.stop_pc=target
            return original_call(target)
        self.call=stop
        return super().run()

    def check(self):
        self.run()
        error=1 if (self.initial[5]>>4)&15!=15 else {
            'ready':2,'kind':3,'dma':4,'p2held':5,'p1held':6,'pointer':7,'postpointer':8,'postflag':9}.get(self.outcome,0)
        if error:
            assert self.pc==self.terminal and self.r[4]==0x20000|error
            address,value,expected={
                1:(0,self.initial[5],0xf0),2:(self.state+28,0,READY),
                3:(self.state+36,self.kind^1,self.kind),4:(MDST,1,0),
                5:(self.flag,0x80,0),6:(self.lock+4,0x80,0),
                7:(self.lock,self.flag^4,self.flag),
                8:(self.lock|0xa0000000,self.flag^4,self.flag),9:(self.flag,0x80,0)}[error]
            assert self.r[5:8]==[address,value,expected],('fault context',error,self.r[5:8],[address,value,expected])
            assert self.owner_writes==[]
            assert self.purges==(2 if error in (8,9) else 0)
            if error<=5:assert not any(e[0].startswith('p1') for e in self.events)
            return self
        assert self.pc==self.resume and self.purges==2
        assert [e[1] for e in self.events if e[0]=='purge']==[self.lock,self.lock+4]
        assert not self.cached_pointer_valid and not self.cached_flag_valid
        r,pr,mach,macl,gbr,sr,fpscr,fr=self.initial
        expected=r.copy();pushed=[r[i] for i in ((14,13) if not self.kind else (14,13,12,11,10,9))]
        expected[15]-=4*len(pushed)
        if not self.kind:expected[14],expected[3],expected[2],expected[0]=self.manager_cell,self.manager_initial,self.manager_initial,0xfc4
        else:expected[0]=self.resume  # Dead before the real initializer's first R0 use.
        assert self.r==expected,[(i,hex(a),hex(b)) for i,(a,b) in enumerate(zip(self.r,expected)) if a!=b]
        assert (self.pr,self.mach,self.macl,self.gbr,self.sr,self.fpscr,self.fr)==(pr,mach,macl,gbr,sr,fpscr,fr)
        assert self.owner_writes==[(self.start_sp-4*(i+1),v) for i,v in enumerate(pushed)]
        assert self.mem[self.state+44]==self.initial_count+1
        return self


def cases(image,*,body=None,name=None,compact=False):
    checked=[]
    for kind in (0,1):
        if name is not None and name!=(GATE_UNIFORM,GATE_VARIABLE)[kind]:continue
        for lock in (0x8c1dc538,0x8c1dc53c):
            metadata=(lock,0x8c32d6e4 if not kind else 0x0c32d6e4,
                      0x8c32c5e8 if not kind else 0x0c32c5e8,0x8c1da8fc if not kind else 0x8c2d9748)
            for seed in ((0,1) if compact else range(8)):
                for stack in ((0x8c2ff000,) if compact else (0x0c2ff000,0x8c2ff000,0xac2ff000)):
                    checked.append(Gate(image,kind,seed,15,stack=stack,body=body,metadata=metadata).check())
                for outcome in ('ready','kind','dma','p2held','p1held','pointer','postpointer','postflag'):
                    checked.append(Gate(image,kind,seed,15,outcome,body=body,metadata=metadata).check())
            for mask in range(15):checked.append(Gate(image,kind,3,mask,body=body,metadata=metadata).check())
    return checked


def original_prefix_cases(image,startups):
    count=0;records=[];extended=0
    for name,path in (startups or {}).items():
        raw=Path(path).read_bytes()
        cache=(Path(__file__).resolve().parents[1]/'tests/fixtures/native-cache'/f'{name}.nvr').read_bytes()
        d=struct.unpack_from('<18I',cache,64);l=struct.unpack_from('<7I',cache,168)
        assert len(raw)==d[3] and hashlib.sha256(raw).digest()==cache[136:168]
        kind=l[6];init=l[2]
        assert init==d[8]+(0x11c if kind else 0)
        prefix=struct.unpack_from('<6H',raw,init-d[4])
        if kind:
            assert prefix==(0x2fe6,0x2fd6,0x2fc6,0x2fb6,0x2fa6,0x2f96)
            following=struct.unpack_from('<7H',raw,init-d[4]+12)
            assert following[:2]==(0x4f22,0x7ff4) and following[2]&0xff00==0xd300
            assert following[3]==0xeb1f and following[4]&0xff00==0xde00 and following[5]==0x6c73 and following[6]&0xff00==0x9000
        else:
            assert prefix[0]==0x2fe6 and prefix[1]&0xff00==0xde00 and prefix[2]&0xff00==0xd300
            assert prefix[3]&0xff00==0x9000 and prefix[4:]==(0x6233,0x2fd6)
        for seed in range(8):
            actual=Gate(image,kind,seed,metadata=(d[11],d[9],d[10],init))
            # The wrapper's patched literal places the P2 gate in target R3;
            # the unmodified wrapper placed the original initializer there.
            actual.r[3]=actual.base+0x20000000
            actual.initial=(actual.r.copy(),*actual.initial[1:])
            actual.check()
            reference=Machine(image,(GATE_UNIFORM,GATE_VARIABLE)[kind],seed,15,actual.start_sp)
            reference.base=d[4];reference.blob=raw;reference.pc=init;reference.stop_pc=init+12
            reference.r,reference.pr,reference.mach,reference.macl,reference.gbr,reference.sr,reference.fpscr,reference.fr=(
                actual.initial[0].copy(),*actual.initial[1:7],actual.initial[7].copy())
            reference.r[3]=init
            reference.run()
            assert all(actual.r[i]==reference.r[i] for i in range(1,16) if not(kind and i==3))
            assert kind or actual.r[0]==reference.r[0]
            assert (actual.pr,actual.mach,actual.macl,actual.gbr,actual.sr,actual.fpscr,actual.fr)==(
                reference.pr,reference.mach,reference.macl,reference.gbr,reference.sr,reference.fpscr,reference.fr)
            assert all(reference.mem[canonical(a)]==v for a,v in actual.owner_writes)
            if kind:
                continuation=Machine(image,GATE_VARIABLE,seed,15,actual.start_sp)
                continuation.base=d[4];continuation.blob=raw;continuation.pc=init+12;continuation.stop_pc=init+26
                continuation.r=actual.r.copy()
                continuation.pr,continuation.mach,continuation.macl,continuation.gbr,continuation.sr,continuation.fpscr,continuation.fr=(
                    actual.pr,actual.mach,actual.macl,actual.gbr,actual.sr,actual.fpscr,actual.fr.copy())
                continuation.mem={canonical(a):v for a,v in actual.owner_writes}
                continuation.run();reference.stop_pc=init+26;reference.run()
                assert continuation.r==reference.r,'incoming R0/R3 survived explicit48 initializer definitions'
                assert continuation.mem==reference.mem
                assert (continuation.pr,continuation.sr)==(reference.pr,reference.sr)
                extended+=1
            count+=1
        records.append({'name':name,'sha256':hashlib.sha256(raw).hexdigest(),'kind':kind,'initializer':hex(init)})
    return count,records,extended


def audit(image,self_test=False,startups=None):
    checked=cases(image);mutations=0
    if self_test:
        for name in (GATE_UNIFORM,GATE_VARIABLE):
            base,body,_=symbol_bytes(image,name,2)
            executed={pc-0x20000000 for c in checked if c.base==base for pc in c.executed}
            for at in sorted(executed):
                if not base<=at<base+len(body):continue
                offset=at-base
                if body[offset:offset+2]==b'\x09\x00':continue
                changed=body[:offset]+b'\x09\x00'+body[offset+2:]
                try:cases(image,body=changed,name=name,compact=True)
                except (AssertionError,ValueError,KeyError,struct.error):mutations+=1
                else:raise AssertionError('undetected coherence instruction mutation '+hex(at))
            # Data literals are executable authority too: reject changed
            # private-state, MMIO, failure-target and comparison bindings.
            values=[(4,image[4][STATE][0]),(4,READY),(4,MDST),(4,0xa0000000),
                    (4,0x20000),(4,image[4][TERMINAL][0]),(2,0xf0)]
            if name==GATE_UNIFORM:values.append((2,0xfc4))
            for width,value in values:
                before=struct.pack('<I' if width==4 else '<H',value)
                assert body.count(before)==1,('ambiguous coherence literal',hex(value))
                at=body.index(before);changed=bytearray(body);changed[at]^=1
                try:cases(image,body=bytes(changed),name=name,compact=True)
                except (AssertionError,ValueError,KeyError,struct.error):mutations+=1
                else:raise AssertionError('undetected coherence literal mutation '+hex(value))
    original_count,originals,extended=original_prefix_cases(image,startups)
    return {'schema':'kui.native.sdk-cold-coherence.v1','instruction_cases':len(checked),
        'mutation_rejections':mutations,'owner_stack_accesses_before_purge':0,
        'other_operand_reads_before_purge_are_p2':True,
        'deliberate_cached_checks':['P1 flag equals zero','P1 pointer equals normalized P2 flag'],
        'purge_before_first_tas':True,'uniform_replay_stack_bytes':8,'variable_replay_stack_bytes':24,
        'ordered_purge_operands':['lock_cell','lock_cell+4'],'same_and_separate_dirty_lines_checked':True,
        'no_private_stack_use':True,'forced_lock_writes':0,'original_prefix_cases':original_count,
        'variable_extended_liveness_cases':extended,'call_target_register_r3_dead':True,
        'original_startups':originals,'hardware_freeze_reproduced':False}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('worker',type=Path);p.add_argument('--self-test',action='store_true')
    for name in ('evolution1','ps2','skies','sa2','grandia2'):p.add_argument('--'+name,type=Path)
    a=p.parse_args();supplied={n:getattr(a,n) for n in ('evolution1','ps2','skies','sa2','grandia2') if getattr(a,n)}
    print(json.dumps(audit(read_elf(a.worker),a.self_test,supplied),indent=2))
