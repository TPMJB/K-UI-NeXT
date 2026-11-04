#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check isolated CE demand observers without weakening the stream bootstrap audit.

The ordinary excerpt audit still pins every original bootstrap guard. This
companion checks the new direct high entry, actual cross-image stack frames,
profile engagement, and executes its compiled unattached path with MMIO and
all non-stack writes forbidden. Profile2's actual clock sampler is separately
executed with read-only device stubs, including a low-counter rollover.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_native_dual_readers import ScalarOps
from check_retail_stack import base,load,check as stack_check
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from check_loader_layout import inspect_elf
from check_armada_ce_excerpt import audit_stream_boot_arm,function_sizes,STAGE,POOL_BEGIN
from test_vmu_native_heap_execution import SH,physical,signed

ENTRY='_kui_armada_ce_demand_event'
TOP=0x8c00dff0
COUNTERS={0xffd8000c:900,0xffd80008:1000,0xffd80010:2,
          0xffd80004:1,0xffc00000:0xe0a,0xff000084:0xc023,
          0xff100004:3,0xff100008:0xfffffff0}


def require(ok,message):
    if not ok:raise ValueError(message)


class ObserverCPU(ScalarOps,SH):
    def __init__(self,image,*,sampler=False,rollover=False,mutation=None,admitted_region=None):
        super().__init__(b'')
        for kind,offset,address,_,count,_,_,_ in image[2]:
            if kind==1 and count:self.store_block(address,image[0][offset:offset+count])
        self.sr=0x400000f0
        self.low=min(s[3] for s in image[3] if s[2]&2 and s[5])
        self.code_end=image[4]['__armada_bind_binary_end'][0]
        self.t=image[4]['_t'][:2]
        self.demand=image[4]['_demand'][:2]
        self.admitted_region=admitted_region
        if admitted_region:
            # Authored positive attachment; the separate unchanged relay
            # proof establishes when actual console attachment is legal.
            self.store_block(self.t[0],struct.pack('<I',1)*(self.t[1]//4))
        self.sampler=sampler;self.rollover=rollover;self.device_reads=[];self.conditions=[]
        self.begin,self.body,_=symbol_bytes(image,ENTRY,2)
        if mutation is not None:self.store_block(mutation[0],struct.pack('<H',mutation[1]))

    def plain(self,opcode,pc):
        if opcode>>8==0x85:  # MOV.W @(disp,Rm),R0, signed16
            self.executing=pc;self.instructions+=1
            self.r[0]=signed(self.read(self.r[(opcode>>4)&15]+(opcode&15)*2,2),16)&0xffffffff
        elif opcode&0xf00f==0x6007:  # NOT Rm,Rn, T unchanged
            self.executing=pc;self.instructions+=1
            self.r[(opcode>>8)&15]=(~self.r[(opcode>>4)&15])&0xffffffff
        elif opcode&0xf00f in (0x000c,0x000d,0x000e):  # MOV.B/W/L @(R0,Rm),Rn
            self.executing=pc;self.instructions+=1;n,m=(opcode>>8)&15,(opcode>>4)&15
            size={12:1,13:2,14:4}[opcode&15]
            value=self.read((self.r[0]+self.r[m])&0xffffffff,size)
            self.r[n]=(signed(value,size*8) if size<4 else value)&0xffffffff
        elif opcode&0xf00f in (0x0004,0x0005,0x0006):  # MOV.B/W/L Rm,@(R0,Rn)
            self.executing=pc;self.instructions+=1;n,m=(opcode>>8)&15,(opcode>>4)&15
            self.write((self.r[0]+self.r[n])&0xffffffff,self.r[m],{4:1,5:2,6:4}[opcode&15])
        elif opcode&0xf0ff in (0x4011,0x4015):  # CMP/PZ and CMP/PL
            self.executing=pc;self.instructions+=1;value=signed(self.r[(opcode>>8)&15],32)
            self.flag(value>=0 if opcode&0xf0ff==0x4011 else value>0)
        elif opcode&0xf00f==0x300a:  # SUBC Rm,Rn
            self.executing=pc;self.instructions+=1;n,m=(opcode>>8)&15,(opcode>>4)&15
            value=self.r[n]-self.r[m]-(self.sr&1)
            self.r[n]=value&0xffffffff;self.flag(value<0)
        elif opcode&0xf00f==0x200a:  # XOR Rm,Rn
            self.executing=pc;self.instructions+=1
            self.r[(opcode>>8)&15]^=self.r[(opcode>>4)&15]
        elif opcode&0xf0ff==0x4010:  # DT Rn
            self.executing=pc;self.instructions+=1;n=(opcode>>8)&15
            self.r[n]=(self.r[n]-1)&0xffffffff;self.flag(self.r[n]==0)
        elif opcode&0xf00f in (0x400c,0x400d):  # SHAD/SHLD, T unchanged
            self.executing=pc;self.instructions+=1
            n,m=(opcode>>8)&15,(opcode>>4)&15;amount=self.r[m]
            if not amount&0x80000000:self.r[n]=(self.r[n]<<(amount&31))&0xffffffff
            else:
                count=((~amount)&31)+1
                value=signed(self.r[n],32) if opcode&15==12 else self.r[n]
                self.r[n]=(value>>count)&0xffffffff
        elif opcode&0xf00f==0x600a:  # NEGC Rm,Rn, T is the unsigned borrow
            self.executing=pc;self.instructions+=1
            source=self.r[(opcode>>4)&15];borrow=self.sr&1
            self.r[(opcode>>8)&15]=(-source-borrow)&0xffffffff
            self.flag(source!=0 or borrow)
        elif opcode&0xf00f in (0x200e,0x200f):  # MULU.W/MULS.W Rm,Rn
            self.executing=pc;self.instructions+=1
            a=self.r[(opcode>>8)&15]&0xffff;b=self.r[(opcode>>4)&15]&0xffff
            if opcode&15==15:a,b=signed(a,16),signed(b,16)
            self.macl=(a*b)&0xffffffff
        elif opcode&0xf00f==0x0007:  # MUL.L Rm,Rn, low32 independent of signedness
            self.executing=pc;self.instructions+=1
            self.macl=(self.r[(opcode>>8)&15]*self.r[(opcode>>4)&15])&0xffffffff
        elif opcode&0xf0ff==0x001a:  # STS MACL,Rn
            self.executing=pc;self.instructions+=1;self.r[(opcode>>8)&15]=self.macl
        elif opcode&0xf0ff in (0x4019,0x4029):  # SHLR8/SHLR16, T unchanged
            self.executing=pc;self.instructions+=1
            self.r[(opcode>>8)&15]>>=8 if opcode&0xf0ff==0x4019 else 16
        elif opcode&0xf0ff==0x0029:  # MOVT Rn
            self.executing=pc;self.instructions+=1;self.r[(opcode>>8)&15]=self.sr&1
        elif opcode>>8==0x88:  # CMP/EQ #imm,R0: sign-extended immediate
            self.executing=pc;self.instructions+=1
            self.flag(self.r[0]==(signed(opcode&255,8)&0xffffffff))
        else:super().plain(opcode,pc)

    def read(self,address,count=4):
        if address in COUNTERS:
            require(self.sampler,'Unattached diagnostic touched a device register')
            self.device_reads.append(address)
            if self.rollover and address==0xff100004:
                return 3 if self.device_reads.count(address)==1 else 4
            if self.rollover and address==0xff100008:
                return 0xfffffff0 if self.device_reads.count(address)==1 else 0x20
            return COUNTERS[address]
        allowed=(self.low<=address<=self.code_end-count or
                 TOP-1024<=address<=TOP-count or
                 self.t[0]<=address<=self.t[0]+self.t[1]-count)
        if self.admitted_region:
            first,size=self.admitted_region
            allowed=allowed or first<=address<=first+size-count or \
                self.demand[0]<=address<=self.demand[0]+self.demand[1]-count
        require(allowed,'Unattached helper read reader/private state before admission')
        value=super().read(address,count)
        if count==2 and self.begin<=address<self.begin+len(self.body) and value>>8 in (0x89,0x8b,0x8d,0x8f):
            self.conditions.append((address,value))
        return value

    def write(self,address,value,count=4):
        allowed=TOP-1024<=address<=TOP-count
        if self.admitted_region:
            allowed=allowed or self.demand[0]<=address<=self.demand[0]+self.demand[1]-count
        require(allowed,'Diagnostic wrote a device/controller/reader before admission')
        super().write(address,value,count)


def reachable(calls,root,target):
    starts=[n for n in calls if base(n)==root]
    require(len(starts)==1,'Missing or duplicated observer caller '+root)
    visited=set()
    def walk(node):
        if base(node)==target:return True
        if node in visited:return False
        visited.add(node)
        for callee,_ in calls.get(node,[]):
            if base(callee)==target:return True
            matches=[n for n in calls if n==callee or base(n)==base(callee)]
            if any(walk(n) for n in matches):return True
        return False
    return walk(starts[0])


def check(directory,profile,*,self_test=False):
    directory=Path(directory)
    high=read_elf(directory/'bind-probe.elf');low=read_elf(directory/'resident-scia.elf')
    stage=read_elf(directory/'stage.elf')
    original_boot=audit_stream_boot_arm(
        inspect_elf(high[0],0x8cfa7000,0x8cfbf000,entry_symbol='_kui_armada_ce_bind_probe'),function_sizes(high[0]),
        inspect_elf(low[0],0x8c008300,0x8c00d800),function_sizes(low[0],kinds=(1,2)),
        inspect_elf(stage[0],STAGE,POOL_BEGIN),function_sizes(stage[0],kinds=(1,2)))
    address,body,section=symbol_bytes(high,ENTRY,2)
    require(section[2]&7==6 and not address&1 and
            0x8cfa7000<=address<address+len(body)<=high[4]['__armada_bind_binary_end'][0],
            'Observer entry is not bounded immutable high code')
    require(low[4].get(ENTRY)==(address,0,0,0xfff1),
            'Low observer call does not import exactly the linked high entry')
    contract_address,contract,contract_section=symbol_bytes(high,'_kui_armada_ce_demand_contract',1)
    require(contract_section[2]&7==2 and len(contract)==16,'Missing immutable observer contract')
    magic,version,mode,identity=struct.unpack('<4I',contract)
    expected_version=3 if profile==3 else 1
    require((magic,version,mode)==(0x444d4544,expected_version,profile) and identity,
            'Observer mode/identity differs from selected test')
    require(high[4]['__armada_bind_bss_end'][0]<=0x8cfbf000,
            'Observer state escaped the admitted high reservation')
    require(low[4]['__retail_resident_bss_end'][0]<=0x8c00d800,
            'Observer overlaps the existing private stack')
    _,calls=load(sorted((directory/'scia/lto').glob('*.ci')),qualified=True)
    require(reachable(calls,'kui_retail_menu_return',ENTRY[1:]),'Terminal observer is not reachable')
    for root in ('kui_retail_resident_dispatch','kui_retail_ce_irq'):
        require(reachable(calls,root,ENTRY[1:])==(profile>=2),
                'Hot observer engagement differs from selected profile: '+root)
    stack=stack_check(directory/'scia',2000,ce=True)
    region=low[4]['_kui_retail_async_region'][0]
    gate_runs=0;first_condition=None
    for event in ((1,2,3,4,5,5|(4<<8),6) if profile==3 else (1,2,3,4,5,5|(4<<8))):
        cpu=ObserverCPU(high)
        result,writes,instructions=cpu.run(address,(event,0,region),TOP)
        require(result==0 and instructions<256,'Unattached observer did not refuse boundedly')
        require(not cpu.device_reads,'Unattached observer sampled a clock')
        require(cpu.conditions,'Missing compiled attachment branch')
        first_condition=cpu.conditions[0];gate_runs+=1
    mutants=0
    if self_test:
        # Invert the actual first conditional on the unattached path. An
        # admitted-looking low region must then reach a denied state access;
        # an invalid-pointer fixture could hide a removed gate.
        at,word=first_condition
        for event in (5,5|(4<<8)):
            cpu=ObserverCPU(high,mutation=(at,word^0x0200))
            try:cpu.run(address,(event,0,region),TOP)
            except (AssertionError,ValueError):mutants+=1
            else:raise ValueError('Removed/inverted compiled attachment gate admitted')
    sampler_runs=0
    if profile>=2:
        sampler,_,_=symbol_bytes(high,'_demand_clock_sample',2)
        for rollover in (False,True):
            cpu=ObserverCPU(high,sampler=True,rollover=rollover)
            result,writes,instructions=cpu.run(sampler,(TOP-512,),TOP)
            got=struct.unpack('<8I',cpu.block(TOP-512,32))
            expected=(900,1000,2,1,0xe0a,0xc023,0x20 if rollover else 0xfffffff0,4 if rollover else 3)
            require(got==expected,'Compiled clock sampler field/order/rollover differs')
            require(instructions<128 and len(cpu.device_reads)==(10 if rollover else 9),
                    'Clock sampler lost its bounded read budget')
            require(all(physical(a)>=physical(TOP-512) and physical(a)+n<=physical(TOP-512)+32
                        or TOP-1024<=a<=TOP-n for _,a,_,n in writes),
                    'Clock sampler wrote outside its result/private stack')
            sampler_runs+=1
    positive=ObserverCPU(high,sampler=profile>=2,admitted_region=low[4]['_kui_retail_async_region'][:2])
    positive.run(address,(1,0x40000000,region),TOP)
    positive.run(address,(2,0,region),TOP)
    pointer,_,_=positive.run(address,(5,0,region),TOP)
    got=struct.unpack('<5I',positive.block(pointer+32,20))
    require(got==(expected_version,profile,identity,int(profile>=2),0),
            'Actual SH observer did not engage its admitted low region/profile')
    if profile==3:
        positive.run(address,(6,0,region),TOP)
        at,size=positive.demand;frozen=positive.block(at,size)
        clocks=len(positive.device_reads)
        for event in (1,2,3,4,6):positive.run(address,(event,0,region),TOP)
        require(positive.block(at,size)==frozen and len(positive.device_reads)==clocks,
                'Frozen linked observer resumed sampling or changed counters')
    return {'schema':'kui.ce-demand-observer.audit.v1','profile':profile,'profile_id':hex(identity),
            'direct_high_entry':hex(address),'contract_address':hex(contract_address),
            'original_bootstrap_guard_proof':original_boot,'unattached_linked_runs':gate_runs,
            'gate_mutants_rejected':mutants,'readonly_sampler_linked_runs':sampler_runs,
            'admitted_linked_profile_engagement':True,
            'frozen_linked_metrics_immutable':profile==3,
            'private_stack':stack,'artifact_sha256':{'controller':hashlib.sha256(high[0]).hexdigest(),
                'resident':hashlib.sha256(low[0]).hexdigest()},
            'timing_limit':'GD/SCI C bodies only; TMU values are modulo lower bounds; unavailable cycles stay untimed'}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    parser.add_argument('--profile',type=int,choices=(1,2,3),required=True)
    parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args()
    print(json.dumps(check(args.directory,args.profile,self_test=args.self_test),indent=2))


if __name__=='__main__':main()
