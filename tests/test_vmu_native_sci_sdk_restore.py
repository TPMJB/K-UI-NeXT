#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay lost VBR ownership with original SDK instructions and native SH.

Supply the exact external Evolution2 startup and existing timeline/priority7
builds. No retail executable bytes are embedded here. The complete linked
native vector/IRQ entry executes with both GPR banks and hardware exception
acceptance. Its C payload is an ABI-adversarial handled-interrupt substitute.
The original SDK VBR save/restore and integer callback dispatcher execute from
the independently hashed owner image; its full FPU prologue is explicitly
outside this model. Reaching SDK DEFAULT establishes bypass of the reader,
not a console hang or performance attribution. The actual default helper
returns without acknowledging the SCI, so hardware acceptance can repeat
when the abstracted full SDK epilogue restores the interrupted SR.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from test_vmu_native_heap_execution import SH,MASK,signed
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from check_vmu_native_irq_rearm import Rearm,linked_bytes
from sh4_delay_slots import validate_delay_slot

LOAD=0x8c010000
SOURCE_BYTES=4057056
SOURCE_SHA='16b45fcc84992a60d5a64bf64bf22b699b27306b95f177ba3d9730ca380069ae'
PROGRAM_BYTES=4024288
PROGRAM_SHA='52e00d2b3e9db00016b1f6d782cf8a2141617f3d00bd628200de757fd2e11605'
SDK_VBR=0x8c00f400
SDK_DEFAULT=0x8c1add0c
SDK_DISPATCH=0x8c1ae0d8
SDK_EVENT=0x8c1ae1d4
SDK_DEFAULT_CALL=0x8c1add4c
SDK_DEFAULT_RETURN=0x8c1add52
SDK_DEFAULT_HELPER=0x8c1d89c8
SDK_HELPER_SHA='168f979fa76740185eae09fa2c2632a3b1d62e18c832b89b27a0a84b18dc1057'
REGION=0x8c420400
STACK=0x8c00ee00
RESUME=0x8c010220
INTEVT=0xff000028
IPRB=0xffd00008
GUARD=0x8c007ba0
VECTOR_ENTRY_SHA='368379bbecde2039e1d710d04c58a2a1cc86ec4c5386036ddd18dc8358952c52'
SDK_BODIES=((0x8c1ae030,0x19c,'a65e4ed8f8fdae94a9afe30b38759b51f3292b0a3bef12a9512a2a3247dfe98a'),
            (0x8c1adec8,0x168,'cc73775f1bf292bd1d710db96250842a6b5a6de68eb068b5aa523c216e67e435'))
SDK_SAVE_RESTORE=((0x8c1ae05e,0x8c1ae18c),(0x8c1aded2,0x8c1adff6))


def require(test,message):
    if not test:raise ValueError(message)


def owner_image(path):
    source=Path(path).read_bytes()
    require(len(source)==SOURCE_BYTES and hashlib.sha256(source).hexdigest()==SOURCE_SHA,
            'owner startup differs from the exact externally supplied revision')
    image=source[:PROGRAM_BYTES]
    require(hashlib.sha256(image).hexdigest()==PROGRAM_SHA,'owner program-prefix identity changed')
    for address,count,digest in SDK_BODIES:
        require(hashlib.sha256(image[address-LOAD:address-LOAD+count]).hexdigest()==digest,
                'original SDK common ISR body changed')
    for save,restore in SDK_SAVE_RESTORE:
        require(struct.unpack_from('<H',image,save-LOAD)[0]==0x4f23 and
                struct.unpack_from('<H',image,restore-LOAD)[0]==0x4f27,
                'SDK witness must save and restore the actual VBR stack word')
    helper=image[SDK_DEFAULT_HELPER-LOAD:0x8c1d8c00-LOAD]
    require(len(helper)==568 and hashlib.sha256(helper).hexdigest()==SDK_HELPER_SHA,
            'SDK default integer helper changed')
    require(struct.unpack_from('<I',image,0x8c1ade00-LOAD)[0]==SDK_DEFAULT_HELPER and
            struct.unpack_from('<HHH',image,SDK_DEFAULT_CALL-LOAD)==(0xd02c,0x400b,0x0009),
            'SDK default call boundary no longer invokes the exact helper')
    return image


class InterruptCPU(SH):
    """Integer SH plus hardware acceptance and explicit bank transitions."""
    def __init__(self,resident,owner,seed,bank,mask,blocked=False):
        super().__init__(owner)
        for kind,offset,address,_,count,_,_,_ in resident[2]:
            if kind==1 and count:self.store_block(address,resident[0][offset:offset+count])
        rng=random.Random(seed)
        self.r=[rng.getrandbits(32) for _ in range(16)]
        self.r[15]=STACK
        self.other=[rng.getrandbits(32) for _ in range(8)]
        self.sr=0x40000100|(bank<<29)|(mask<<4)|int(seed&1)|(int(blocked)<<28)
        self.pr=rng.getrandbits(32)&~1
        self.mach,self.macl,self.gbr=(rng.getrandbits(32) for _ in range(3))
        self.fpscr=rng.getrandbits(32);self.fpul=rng.getrandbits(32)
        self.fr=[rng.getrandbits(32) for _ in range(32)]
        self.control=dict(vbr=REGION-0x100,ssr=0x60000000,spc=0x8c222200,
                          sgr=0x8c0a0000,dbr=rng.getrandbits(32))
        self.symbols=resident[4];self.iprb=0x5a0f;self.event=0
        self.calls=0;self.accepted=[];self.callee_return=0
        self.sci_pending=True;self.sci_acknowledgements=0
        self.original=(self.r.copy(),self.other.copy(),self.pr,self.mach,self.macl,self.gbr,
                       self.sr,self.fpscr,self.fpul,self.fr.copy())
        pointer=self.symbols['_kui_retail_async_region_storage'][0]
        self.write(pointer,REGION)
        self.write(GUARD,0x4b554947)
        _,template=linked_bytes(resident,'_kui_retail_vector_interrupt',44)
        self.store_block(REGION+0x500,template)
        self.targets={self.symbols['_kui_retail_async_irq'][0]}

    def set_sr(self,value):
        old=bool(self.sr&0x40000000 and self.sr&0x20000000)
        new=bool(value&0x40000000 and value&0x20000000)
        if old!=new:self.r[:8],self.other=self.other,self.r[:8]
        self.sr=value&MASK

    def read(self,address,count=4):
        if address==IPRB:
            assert count==2;return self.iprb
        if address==INTEVT:
            assert count==4;return self.event
        return super().read(address,count)

    def write(self,address,value,count=4):
        if address==IPRB:
            assert count==2;self.iprb=value&0xffff;return
        super().write(address,value,count)

    def plain(self,op,pc):
        n,m=op>>8&15,op>>4&15
        controls={0x0022:'vbr',0x0032:'ssr',0x0042:'spc',0x003a:'sgr',0x00fa:'dbr'}
        loads={0x402e:'vbr',0x403e:'ssr',0x404e:'spc',0x40fa:'dbr'}
        if op&0xf0ff in controls:self.r[n]=self.control[controls[op&0xf0ff]];return
        if op&0xf0ff in loads:self.control[loads[op&0xf0ff]]=self.r[n];return
        if op&0xf0ff==0x400e:self.set_sr(self.r[n]);return
        if op&0xf0ff==0x4023:
            self.r[n]-=4;self.write(self.r[n],self.control['vbr']);return
        if op&0xf0ff==0x4027:
            self.control['vbr']=self.read(self.r[n]);self.r[n]+=4;return
        if op&0xf0ff in (0x4002,0x4012):
            self.r[n]-=4;self.write(self.r[n],self.mach if op&0xf0ff==0x4002 else self.macl);return
        if op&0xf0ff in (0x4006,0x4016):
            value=self.read(self.r[n]);self.r[n]+=4
            if op&0xf0ff==0x4006:self.mach=value
            else:self.macl=value
            return
        if op&0xf00f==0x000e:self.r[n]=self.read((self.r[0]+self.r[m])&MASK);return
        if op&0xf00f==0x200a:self.r[n]^=self.r[m];return
        if op&0xf0ff==0x4018:self.r[n]=(self.r[n]<<8)&MASK;return
        if op&0xf0ff==0x4021:
            self.flag(self.r[n]&1);self.r[n]=(signed(self.r[n],32)>>1)&MASK;return
        if op>>8==0xc7:self.r[0]=((pc+4)&~3)+(op&255)*4;return
        if op>>8==0x88:self.flag(self.r[0]==(signed(op&255,8)&MASK));return
        super().plain(op,pc)

    def accept(self,event):
        level=(self.iprb>>4)&15
        if self.sr&0x10000000 or level<=((self.sr>>4)&15):return False
        self.event=event
        self.control.update(ssr=self.sr,spc=self.pc,sgr=self.r[15])
        self.set_sr(self.sr|0x70000000)
        self.pc=self.control['vbr']+0x600
        self.accepted.append(self.pc)
        return True

    def callee(self):
        self.calls+=1
        assert self.sr&0x70000000==0x70000000 and self.r[15]==0x8c007cf0-44
        continuation=self.pr
        for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd+self.calls)&MASK
        self.r[0]=self.callee_return
        self.mach^=0xaaaaaaaa;self.macl^=0x55555555;self.pr^=0x13572468
        self.sr^=1;self.pc=continuation

    def run_to(self,entry,stop):
        self.pc=entry
        for _ in range(1000):
            if self.pc==stop:return
            pc=self.pc;op=self.read(pc,2);n=op>>8&15
            target=None;delay=False
            if op>>12 in (0xa,0xb):
                target=pc+4+2*signed(op&4095,12);delay=True
                if op>>12==0xb:self.pr=pc+4
            elif op&0xf0ff in (0x400b,0x402b):
                target=self.r[n];delay=True
                if op&0xf0ff==0x400b:self.pr=pc+4
            elif op==0xb:target=self.pr;delay=True
            elif op==0x2b:target=self.control['spc'];delay=True
            elif op>>8 in (0x89,0x8b,0x8d,0x8f):
                taken=bool(self.sr&1)==(op>>8 in (0x89,0x8d))
                delay=op>>8 in (0x8d,0x8f)
                target=pc+4+2*signed(op&255,8) if taken else pc+(4 if delay else 2)
            if target is None:self.plain(op,pc);self.pc=pc+2;continue
            if delay:
                slot=self.read(pc+2,2);validate_delay_slot(slot,pc+2);self.plain(slot,pc+2)
            if op==0x2b:self.set_sr(self.control['ssr'])
            self.pc=target&MASK
            if self.pc in self.targets:self.callee()
        raise AssertionError('SDK/native interrupt replay exceeded its instruction bound')


def native_entry_cases(resident,owner):
    # Pin the complete entry plus literals independently of the C substitute.
    _,entry=linked_bytes(resident,'_kui_retail_irq_entry',112)
    require(hashlib.sha256(entry).hexdigest()==VECTOR_ENTRY_SHA,
            'linked native IRQ entry/literal binding changed')
    cases=0
    for seed in range(8):
        for bank in (0,1):
            for mask in range(16):
                for blocked in (False,True):
                    for event in (0x4e0,0x500):
                        cpu=InterruptCPU(resident,owner,seed,bank,mask,blocked)
                        cpu.iprb=0x5a7f;cpu.pc=RESUME
                        admitted=not blocked and mask<7
                        assert cpu.accept(event)==admitted
                        if admitted:
                            assert cpu.pc==REGION+0x500
                            cpu.run_to(cpu.pc,RESUME)
                            assert cpu.calls==1
                            assert (cpu.r,cpu.other,cpu.pr,cpu.mach,cpu.macl,cpu.gbr,cpu.sr,
                                    cpu.fpscr,cpu.fpul,cpu.fr)==cpu.original
                            assert (cpu.control['ssr'],cpu.control['spc'],cpu.control['sgr'])==(
                                cpu.original[6],RESUME,STACK)
                        else:assert cpu.calls==0 and cpu.pc==RESUME
                        cases+=1
    return cases


def lost_route_cases(residents,owner):
    cases=accepted=repeated=0
    for level,resident in residents.items():
        for seed in range(4):
            for bank in (0,1):
                for save,restore in SDK_SAVE_RESTORE:
                    for mask in range(16):
                        for blocked in (False,True):
                            for event in (0x4e0,0x500):
                                cpu=InterruptCPU(resident,owner,seed,bank,mask,blocked)
                                cpu.control['vbr']=SDK_VBR
                                # Actual SDK stack save predates the GD call.
                                cpu.plain(cpu.read(save,2),save)
                                saved_sp=cpu.r[15]
                                # Execute the actual linked C hook at its normal
                                # serialized GD precondition, preserving its
                                # complete owner/vector bookkeeping afterwards.
                                hook=Rearm(resident,seed,15,False,False,0)
                                hook.vbr=SDK_VBR;hook.old=(SDK_VBR,hook.iprb,hook.mem.copy())
                                hook.mem[REGION+0x240]=SDK_VBR
                                hook.run().check()
                                cpu.control['vbr']=hook.vbr;cpu.iprb=hook.iprb
                                for address,value in hook.mem.items():cpu.write(address,value)
                                assert cpu.control['vbr']==REGION-0x100
                                assert cpu.read(REGION+0x2d0)==1
                                # The SDK frame restores its earlier VBR after
                                # that valid hook installed the reader.
                                cpu.r[15]=saved_sp
                                cpu.plain(cpu.read(restore,2),restore)
                                assert cpu.control['vbr']==SDK_VBR
                                assert cpu.read(REGION+0x2d0)==1
                                cpu.pc=RESUME
                                admitted=not blocked and mask<level
                                assert cpu.accept(event)==admitted
                                if admitted:
                                    assert cpu.pc==SDK_VBR+0x600 and cpu.calls==0
                                    # Common SDK ISR/FPU prologue is outside
                                    # this model. Its actual integer dispatch
                                    # begins with accepted SR in R0 and the
                                    # captured raw INTEVT in the SDK event cell.
                                    cpu.write(SDK_EVENT,event)
                                    slot=SDK_VBR+0x200+((event-0x200)>>3)
                                    cpu.write(slot,SDK_DEFAULT)
                                    cpu.r[0]=cpu.sr
                                    cpu.run_to(SDK_DISPATCH,SDK_DEFAULT)
                                    assert cpu.r[4]==event and cpu.r[14]==SDK_DEFAULT
                                    assert cpu.calls==0 and cpu.control['vbr']==SDK_VBR
                                    assert cpu.read(REGION+0x2d0)==1
                                    # The default's surrounding FPU/register
                                    # context setup is deliberately abstracted.
                                    # Execute its actual literal-load/JSR/NOP
                                    # boundary and the complete actual integer
                                    # helper, including its RTS delay slot.
                                    before_sp=cpu.r[15]
                                    before_writes=len(cpu.writes)
                                    cpu.run_to(SDK_DEFAULT_CALL,SDK_DEFAULT_RETURN)
                                    assert cpu.r[15]==before_sp
                                    assert cpu.sci_pending and cpu.sci_acknowledgements==0
                                    assert cpu.calls==0 and cpu.control['vbr']==SDK_VBR
                                    assert all(before_sp-44<=address<before_sp
                                               for _,address,_,_ in cpu.writes[before_writes:])
                                    # A still-pending peripheral can be accepted
                                    # again after the full SDK epilogue restores
                                    # SR. Its FPU/bank unwind is an explicit
                                    # boundary assumption, not executed here.
                                    for _ in range(3):
                                        cpu.set_sr(cpu.original[6]);cpu.pc=RESUME
                                        assert cpu.accept(event)
                                        cpu.write(SDK_EVENT,event);cpu.r[0]=cpu.sr
                                        cpu.run_to(SDK_DISPATCH,SDK_DEFAULT)
                                        assert cpu.r[4]==event and cpu.r[14]==SDK_DEFAULT
                                        cpu.run_to(SDK_DEFAULT_CALL,SDK_DEFAULT_RETURN)
                                        assert cpu.sci_pending and cpu.sci_acknowledgements==0
                                        assert cpu.calls==0;repeated+=1
                                    accepted+=1
                                cases+=1
    return cases,accepted,repeated


def check(priority1,priority7,startup):
    require(__debug__,'interrupt model requires assertions; run without Python -O')
    owner=owner_image(startup)
    residents={1:read_elf(Path(priority1)/'resident-sci.elf'),
               7:read_elf(Path(priority7)/'resident-sci.elf')}
    for level,image in residents.items():
        marker=image[4].get('__kui_native_sci_priority7_resident')
        require(bool(marker)==(level==7),'priority build pair has the wrong selected SCI level')
    native=native_entry_cases(residents[7],owner)
    routes,accepted,repeated=lost_route_cases(residents,owner)
    return dict(native_vector_and_irq_entry_cases=native,sdk_restore_route_cases=routes,
        accepted_sdk_default_cases=accepted,repeated_pending_sdk_dispatches=repeated,
        actual_default_integer_helper_executed=True,sci_pending_after_default_helper=True,
        sdk_full_epilogue_abstracted=True,both_gpr_banks_and_all_imasks=True,
        original_sdk_bodies_sha_checked=True,linked_native_hook_executed=True,
        hardware_acceptance_and_exception_registers_modelled=True,
        lost_vbr_while_engine_hooked_reproduced=True,
        captured_mask6_priority1_masked_priority7_wrong_sdk_route=True,
        native_c_payload_model='ABI-adversarial handled interrupt substitute',
        sdk_fpu_prologue_executed=False,console_cause_confirmed=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--priority1-build',type=Path,default=ROOT/'build/native-timeline-final')
    parser.add_argument('--priority7-build',type=Path,default=ROOT/'build/native-priority7-final')
    parser.add_argument('--startup',type=Path,default=os.environ.get('KUI_NATIVE_EVOLUTION2_STARTUP') or os.environ.get('KUI_NATIVE_EVOLUTION2'))
    args=parser.parse_args()
    if not args.startup:parser.error('supply the external exact Evolution2 startup with --startup or KUI_NATIVE_EVOLUTION2_STARTUP')
    try:result=check(args.priority1_build,args.priority7_build,args.startup)
    except (OSError,ValueError,AssertionError,KeyError,struct.error) as error:
        parser.exit(2,'Native SCI SDK-restore test refused: '+str(error)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
