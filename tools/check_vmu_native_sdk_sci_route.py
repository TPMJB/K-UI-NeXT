#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the opt-in SDK-owned SCI callback route using linked SH instructions.

The SDK's complete interrupted/FPU frame remains its responsibility. This
closed model executes the resident callback, ownership predicate, hook/unhook
and dispatcher; only the deep reader C payload is an ABI-compliant adversary.
It proves routing and state preservation, not console progress or performance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from test_vmu_native_sci_sdk_restore import InterruptCPU,owner_image,SDK_DISPATCH,SDK_EVENT
from test_vmu_native_heap_execution import SH,MASK,signed
from sh4_delay_slots import validate_delay_slot
from check_vmu_native_worker_gateway import read_elf,symbol_bytes

MARKER='__kui_native_sdk_sci_route_'
IPRB=0xffd00008
REGION=0x8c420400
SDK_VBR=0x8c00f400
ERI_SLOT=SDK_VBR+0x25c
RXI_SLOT=SDK_VBR+0x260
DEFAULT=0x8c1add0c
COOKIE=0x53434931
COOKIE_AT=REGION+0x530
DEFAULT_AT=REGION+0x534
OWNER_AT=REGION+0x538
FRAME=REGION+0x240
HOOKED=REGION+0x2d0
REARMS=REGION+0x2e4
LOAN=REGION+0x2f4
GUARD=0x8c007ba0
PRIVATE_TOP=0x8c007cf0
OWNER_SP=0x8c00ed00
RETURN=0x8c008a00
# Reviewed immutable resident function bodies. Relocation/literal bindings are
# checked as part of each complete pin and the typed descriptor below.
PINS={
    '_sdk_sci_ready':(80,'f5f233456192a049dbc3b50e23054c866793ed3e6fb140ca4edc3a47c2b4fca5'),
    '_hook':(104,'2bf6e6a20e9bdb206adeb76838331d548586dfeac634362bb303351b2b57c521'),
    '_unhook':(68,'d73a29319264ea8cbcba29b9825319f21d8befbd7dd8519d0679896fecd2dbe9'),
    '_kui_retail_sdk_sci_dispatch':(124,'282e815b796ea367e6ca53c938e04926ad4a83f9c68fa2b82094a415ef452206'),
    '_kui_retail_sdk_sci_entry':(152,'eb38f74342dafb9934a5f62b8b3cbc5bc55feb14d66fbc7af571b6a19f408116'),
}
STAGE_PINS={
    '_native_sdk_sci_route_bind':(676,'e6f04a443bb4411f32628234d739042edb8793e3bd0d8e2bd61f00f4354988fc'),
    '_kui_native_sdk_sci_route_admit':(800,'78bfef551d56da15abdeed774a368e2418d7ab3e5996b4e9bd522d88dbb67e04'),
}
MEMORY_CACHE={}
MEMORY_REFERENCES={}


def require(test,message):
    if not test:raise ValueError(message)


def initialized_at(image,address,count):
    for section in image[3]:
        if section[1]==1 and section[2]&2 and section[3]<=address<address+count<=section[3]+section[5]:
            at=section[4]+address-section[3]
            return image[0][at:at+count]
    raise ValueError('unowned initialized address '+hex(address))


def pin_bodies(resident):
    found={}
    for name,(count,digest) in PINS.items():
        address,body,section=symbol_bytes(resident,name,2)
        require(section[2]&4 and 0x8c004000<=address<address+len(body)<=0x8c007ba0,
                'SDK SCI code escapes immutable low resident: '+name)
        require(len(body)==count and hashlib.sha256(body).hexdigest()==digest,
                'SDK SCI immutable instruction/literal pin changed: '+name)
        found[name]=(address,len(body),digest)
    require(bool(found),'SDK SCI reviewed function pins are missing')
    callback=resident[4]['_kui_retail_sdk_sci_entry'][0]
    descriptor,wire,section=symbol_bytes(resident,'_kui_native_sdk_sci_route_exports',1)
    pointer,cell_bytes,cell_kind,cell_owner=resident[4]['_kui_retail_async_region_storage']
    cell_section=resident[3][cell_owner]
    require(cell_bytes==4 and cell_kind==1 and cell_section[1]==8 and cell_section[2]&7==3 and
            0x8c004000<=pointer<pointer+4<=GUARD,
            'SDK SCI region pointer is not an owned four-byte low BSS cell')
    require(not section[2]&1 and wire==struct.pack('<5I',0x4b535231,1,20,callback,pointer),
            'SDK SCI descriptor type/constant/target/region binding differs')
    entry,word,entry_section=symbol_bytes(resident,'_kui_native_sdk_sci_route_entry',1)
    require(entry==resident[1][4]+36 and not entry_section[2]&1 and
            word==struct.pack('<I',descriptor),'SDK SCI entry+36 descriptor pointer changed')
    for name in ('_kui_retail_vector_interrupt','_kui_retail_vector_forward',
                 '_kui_retail_irq_entry','_kui_retail_release_100','_kui_retail_release_400',
                 '_kui_retail_release_600','_kui_retail_rehook','_vbr_set'):
        require(name not in resident[4],'SDK route still contains raw replacement-vector entry '+name)
    scalar_control_flow(resident,found)
    return found


def stage_pins(stage):
    result={}
    for name,(count,digest) in STAGE_PINS.items():
        address,body,section=symbol_bytes(stage,name,2)
        require(section[2]&4 and 0x8ce00000<=address<address+len(body)<=0x8cfe0000,
                'SDK SCI cold binder/admission code escapes temporary stage')
        require(len(body)==count and hashlib.sha256(body).hexdigest()==digest,
                'SDK SCI cold binder/admission instruction/literal pin changed: '+name)
        result[name]=(address,count,digest)
    return result


def scalar_control_flow(resident,pins):
    """Follow actual local code edges, excluding literal-pool data words."""
    for name,(base,count,_) in pins.items():
        pending=[base];seen=set()
        while pending:
            pc=pending.pop()
            if pc in seen:continue
            require(base<=pc<base+count and not pc&1,'SDK SCI code edge escapes pinned function')
            seen.add(pc);op=struct.unpack('<H',initialized_at(resident,pc,2))[0]
            require(op>>12!=15 and op&0xf0ff not in
                    (0x405a,0x406a,0x4056,0x4066,0x402e,0x4027) and op!=0x002b,
                    'SDK SCI code writes VBR/FPU state or uses raw RTE')
            delayed=op>>12 in (0xa,0xb) or op&0xf0ff in (0x400b,0x402b) or op==0xb or op>>8 in (0x8d,0x8f)
            if delayed:
                slot=struct.unpack('<H',initialized_at(resident,pc+2,2))[0]
                validate_delay_slot(slot,pc+2)
                require(slot>>12!=15 and slot&0xf0ff not in (0x405a,0x406a,0x4056,0x4066,0x402e,0x4027),
                        'SDK SCI delay slot changes protected control/FPU state')
                seen.add(pc+2)
            if op>>12==0xa:pending.append(pc+4+2*signed(op&4095,12))
            elif op>>12==0xb or op&0xf0ff==0x400b:pending.append(pc+4)
            elif op&0xf0ff==0x402b or op==0xb:pass
            elif op>>8 in (0x89,0x8b,0x8d,0x8f):
                pending.extend((pc+4+2*signed(op&255,8),pc+(4 if delayed else 2)))
            else:pending.append(pc+2)


def assert_c_abi(c,before):
    r,other,pr,sr,_,_,gbr,fpscr,fpul,fr,controls=before
    assert c.r[8:]==r[8:] and c.other==other and c.pr==pr and c.sr&~1==sr&~1
    assert (c.gbr,c.fpscr,c.fpul,c.fr,c.control)==(gbr,fpscr,fpul,fr,controls)


class SDKCPU(InterruptCPU):
    """Actual linked scalar SH with physical aliases and both integer banks."""
    def __init__(self,resident,seed=0,mask=15,bank=0,blocked=False,owner=b''):
        key=(id(resident[0]),id(owner))
        if key not in MEMORY_CACHE:
            backing=SH(owner)
            for kind,offset,address,_,count,_,_,_ in resident[2]:
                if kind==1 and count:backing.store_block(address,resident[0][offset:offset+count])
            MEMORY_CACHE[key]=bytes(backing.memory)
            MEMORY_REFERENCES[key]=(resident[0],owner)
        self.memory=MEMORY_CACHE[key];self.overlay={};self.writes=[]
        self.pc=self.executing=self.instructions=0
        rng=random.Random(seed)
        self.r=[rng.getrandbits(32) for _ in range(16)];self.r[15]=OWNER_SP
        self.other=[rng.getrandbits(32) for _ in range(8)]
        self.sr=0x40000100|(bank<<29)|(mask<<4)|(int(blocked)<<28)|(seed&1)
        self.pr=RETURN;self.mach=rng.getrandbits(32);self.macl=rng.getrandbits(32)
        self.gbr=rng.getrandbits(32);self.fpscr=rng.getrandbits(32);self.fpul=rng.getrandbits(32)
        self.fr=[rng.getrandbits(32) for _ in range(32)]
        self.control=dict(vbr=SDK_VBR,ssr=rng.getrandbits(32),spc=rng.getrandbits(32)&~1,
                          sgr=rng.getrandbits(32)&~3,dbr=rng.getrandbits(32))
        self.symbols=resident[4];self.iprb=0x5a1f;self.event=0x4e0
        self.calls=0;self.accepted=[];self.callee_return=0;self.targets=set()
        self.pointer=self.symbols['_kui_retail_async_region_storage'][0]
        self.active=self.symbols['_kui_retail_hook_active'][0]
        self.fault=self.symbols['_kui_retail_hook_fault'][0]
        self.callback=self.symbols['_kui_retail_sdk_sci_entry'][0]
        self.write(self.pointer,REGION);self.write(self.active,0);self.write(self.fault,0)
        self.write(GUARD,0x4b554947);self.write(COOKIE_AT,COOKIE)
        self.write(DEFAULT_AT,DEFAULT);self.write(OWNER_AT,SDK_VBR)
        self.write(ERI_SLOT,self.callback);self.write(RXI_SLOT,self.callback)
        self.write(LOAN,0);self.write(FRAME+4,0x70);self.write(FRAME+8,0x10)
        self.write(HOOKED,0);self.write(REARMS,0xfffffffe)
        self.r[4]=self.event
        self.iprb_writes=[];self.vbr_writes=[];self.callee_mode='abi';self.corrupt_guard=False
        self.initial=self.snapshot();self.writes=[]

    def snapshot(self):
        return (self.r.copy(),self.other.copy(),self.pr,self.sr,self.mach,self.macl,self.gbr,
                self.fpscr,self.fpul,self.fr.copy(),self.control.copy())

    def plain(self,op,pc):
        if op&0xf0ff==0x0029:self.r[op>>8&15]=self.sr&1;return
        # LDC.L @Rn+,SR must change physical bank selection too.
        if op&0xf0ff==0x4007:
            n=op>>8&15;value=self.read(self.r[n]);self.r[n]=(self.r[n]+4)&MASK
            self.set_sr(value);return
        if op&0xf0ff==0x402e:self.vbr_writes.append(self.r[op>>8&15])
        super().plain(op,pc)

    def read(self,address,count=4):
        if address==IPRB:
            require(count==2,'SCI priority must be a halfword read');return self.iprb
        at=self.offset(address,count)
        return int.from_bytes(bytes(self.overlay.get(at+i,self.memory[at+i]) for i in range(count)),'little')

    def write(self,address,value,count=4):
        if address==IPRB:
            require(count==2,'SCI priority must be a halfword write')
            self.iprb=value&0xffff
            if hasattr(self,'iprb_writes'):self.iprb_writes.append(self.iprb)
            return
        at=self.offset(address,count);value&=(1<<(8*count))-1
        self.writes.append((self.executing,address,value,count))
        for i,b in enumerate(value.to_bytes(count,'little')):self.overlay[at+i]=b

    def callee(self):
        self.calls+=1;continuation=self.pr
        if self.callee_mode=='abi':
            assert self.sr&0x100000f0==0x100000f0 and self.r[15]==PRIVATE_TOP
        if self.corrupt_guard:self.write(GUARD,0xbad0bad0)
        for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd+self.calls)&MASK
        self.r[0]=self.callee_return;self.sr^=1
        self.mach^=0xaaaaaaaa;self.macl^=0x55555555;self.pr^=0x13572468
        self.pc=continuation

    def run_symbol(self,name,stop=RETURN):
        self.run_to(self.symbols[name][0],stop)
        return self


def callback_cases(resident):
    cases=0
    dispatcher=resident[4]['_kui_retail_sdk_sci_dispatch'][0]
    for seed in range(3):
        for mask in range(16):
            for bank in (0,1):
                for blocked in (False,True):
                    for mode in ('handled','forward','busy','badguard','postguard'):
                        c=SDKCPU(resident,seed,mask,bank,blocked)
                        c.targets={dispatcher};c.callee_return=DEFAULT if mode=='forward' else 0
                        if mode=='busy':c.write(c.active,1)
                        if mode=='badguard':c.write(GUARD,0)
                        if mode=='postguard':c.corrupt_guard=True
                        before=c.snapshot();c.writes=[]
                        forward=mode in ('forward','busy','badguard')
                        c.run_symbol('_kui_retail_sdk_sci_entry',DEFAULT if forward else RETURN)
                        r,other,pr,sr,mach,macl,gbr,fpscr,fpul,fr,controls=before
                        assert c.r[8:]==r[8:] and c.other==other and c.r[4]==r[4]
                        assert c.pr==pr and c.sr==sr
                        assert (c.gbr,c.fpscr,c.fpul,c.fr,c.control)==(gbr,fpscr,fpul,fr,controls)
                        assert c.calls==int(mode not in ('busy','badguard'))
                        assert c.read(c.fault)==int(mode in ('badguard','postguard'))
                        assert not c.iprb_writes and not c.vbr_writes
                        assert all((OWNER_SP-16<=address<OWNER_SP or address in (c.fault,GUARD))
                                   for _,address,_,_ in c.writes)
                        if mode in ('busy','badguard'):
                            assert not any(0x8c007ba0<address<PRIVATE_TOP for _,address,_,_ in c.writes)
                        cases+=1
    return cases


def ownership(c,variant):
    if variant=='cold':c.write(COOKIE_AT,0)
    elif variant=='wrongcookie':c.write(COOKIE_AT,COOKIE^1)
    elif variant=='wrongowner':c.write(OWNER_AT,SDK_VBR+0x400)
    elif variant=='vbrchanged':c.control['vbr']=SDK_VBR+0x400
    elif variant=='eri_changed':c.write(ERI_SLOT,DEFAULT)
    elif variant=='rxi_changed':c.write(RXI_SLOT,DEFAULT)
    else:assert variant=='ready'


def ownership_cases(resident):
    hooks=unhooks=dispatches=0
    variants=('ready','cold','wrongcookie','wrongowner','vbrchanged','eri_changed','rxi_changed')
    for mask in range(16):
        for variant in variants:
            for hooked in (0,1):
                for field in range(0,256,16):
                    c=SDKCPU(resident,field,mask);ownership(c,variant)
                    c.write(HOOKED,hooked);c.iprb=(0xa50f&~0xf0)|field
                    old=c.iprb;old_saved=c.read(FRAME+4);old_rearms=c.read(REARMS);last=0x10
                    old_vbr=c.control['vbr'];level=0x10 if variant=='ready' else 0
                    c.writes=[];before=c.snapshot();c.run_symbol('_hook');assert_c_abi(c,before)
                    assert c.control['vbr']==old_vbr and not c.vbr_writes
                    assert c.iprb==(old&~0xf0)|level
                    assert c.iprb_writes==([] if field==level else [(old&~0xf0)|level])
                    assert c.read(HOOKED)==1
                    assert c.read(FRAME+4)==(old_saved if hooked and field==last else field)
                    assert c.read(FRAME+8)==level
                    assert c.read(REARMS)==((old_rearms+int(hooked and field!=last))&MASK)
                    assert all(address in (HOOKED,FRAME+4,FRAME+8,REARMS,IPRB) or OWNER_SP-128<=address<OWNER_SP
                               for _,address,_,_ in c.writes)
                    hooks+=1
                    c=SDKCPU(resident,field,mask);ownership(c,variant)
                    c.write(HOOKED,hooked);c.iprb=(0xa50f&~0xf0)|field
                    old=c.iprb;old_vbr=c.control['vbr'];c.writes=[];before=c.snapshot();c.run_symbol('_unhook');assert_c_abi(c,before)
                    desired=(old&~0xf0)|0x70 if hooked and field==last else old
                    assert c.iprb==desired and c.iprb_writes==([desired] if hooked and field==last else [])
                    assert c.read(HOOKED)==0 and c.read(FRAME+8)==(0 if hooked else 0x10)
                    assert c.control['vbr']==old_vbr and not c.vbr_writes
                    assert all(address in (HOOKED,FRAME+8,IPRB) or OWNER_SP-128<=address<OWNER_SP
                               for _,address,_,_ in c.writes)
                    unhooks+=1
    irq=resident[4]['_kui_retail_async_irq'][0]
    for mask in range(16):
        for variant in variants:
            for event in (0x4e0,0x500,0x300,0x4e1,0):
                for active in (0,1):
                    for loan in (0,1,2,3):
                        for result in (0,1):
                            c=SDKCPU(resident,event,mask);ownership(c,variant)
                            c.r[4]=event;c.write(c.active,active);c.write(LOAN,loan)
                            c.targets={irq};c.callee_mode='deep-reader';c.callee_return=result
                            old_vbr=c.control['vbr'];c.writes=[]
                            before=c.snapshot();c.run_symbol('_kui_retail_sdk_sci_dispatch');assert_c_abi(c,before)
                            owns=variant=='ready' and event in (0x4e0,0x500) and not active and not loan
                            assert c.calls==int(owns)
                            assert c.r[0]==(0 if owns and not result else DEFAULT)
                            assert not c.iprb_writes and not c.vbr_writes and c.control['vbr']==old_vbr
                            assert all(OWNER_SP-128<=address<OWNER_SP for _,address,_,_ in c.writes)
                            dispatches+=1
    # Exercise a live ownership transition, including owner-field changes.
    # Cookie/VBR/table readiness may change while the engine remains hooked;
    # our previous level must never be learned as a new game-owned level.
    transitions=0
    for mask in range(16):
        for field in range(0,256,16):
            for variant in variants[1:]:
                c=SDKCPU(resident,field,mask);c.iprb=0xa50f|field
                c.run_symbol('_hook');assert c.read(FRAME+4)==field
                assert c.read(FRAME+8)==0x10 and c.iprb&0xf0==0x10
                ownership(c,variant)
                c.run_symbol('_hook');assert c.read(FRAME+4)==field
                assert c.read(FRAME+8)==0 and c.iprb&0xf0==0
                c.run_symbol('_unhook');assert c.iprb&0xf0==field
                assert c.read(HOOKED)==c.read(FRAME+8)==0
                # Ready again, then lose a slot while a real reader is live.
                c=SDKCPU(resident,field,mask);c.iprb=0xa50f|field;c.run_symbol('_hook')
                ownership(c,variant);c.targets={irq};c.callee_mode='deep-reader'
                c.r[4]=0x4e0;c.run_symbol('_kui_retail_sdk_sci_dispatch')
                assert c.calls==0 and c.r[0]==DEFAULT
                assert c.iprb&0xf0==0 and c.read(FRAME+4)==field and c.read(FRAME+8)==0
                assert not c.vbr_writes;transitions+=1
                # Foreign/serialized ownership must take precedence over a
                # lost-route repair, including when our SCI field is live.
                for reason in ('active','loan','event'):
                    c=SDKCPU(resident,field,mask);c.iprb=0xa50f|field;c.run_symbol('_hook')
                    ownership(c,variant);c.r[4]=0x300 if reason=='event' else 0x4e0
                    if reason=='active':c.write(c.active,1)
                    if reason=='loan':c.write(LOAN,1)
                    c.targets={irq};c.callee_mode='deep-reader';c.iprb_writes=[];c.writes=[]
                    c.run_symbol('_kui_retail_sdk_sci_dispatch')
                    assert c.calls==0 and c.r[0]==DEFAULT and not c.iprb_writes
                    assert c.read(FRAME+8)==0x10 and c.read(FRAME+4)==field
                    assert all(OWNER_SP-128<=address<OWNER_SP for _,address,_,_ in c.writes)
                    transitions+=1
    return hooks,unhooks,dispatches,transitions


def check_sdk_dispatch(resident,startup):
    """Optional external-owner test: execute the actual SDK integer dispatch."""
    owner=owner_image(startup);cases=0
    target=resident[4]['_kui_retail_sdk_sci_dispatch'][0]
    continuation=0x8c1ae104
    for mask in range(16):
        for bank in (0,1):
            for event in (0x4e0,0x500):
                for handled in (False,True):
                    c=SDKCPU(resident,event,mask,bank,False,owner)
                    c.write(SDK_EVENT,event);c.r[0]=c.sr
                    c.targets={target};c.callee_return=0 if handled else DEFAULT
                    # Execute the real event-table lookup and JSR. Its
                    # surrounding full FPU context setup is not synthesized.
                    c.run_to(SDK_DISPATCH,c.callback)
                    assert c.r[4]==event and c.pr==continuation
                    assert c.control['vbr']==SDK_VBR
                    before=c.snapshot();c.writes=[]
                    c.run_symbol('_kui_retail_sdk_sci_entry',continuation if handled else DEFAULT)
                    r,other,pr,sr,_,_,gbr,fpscr,fpul,fr,control=before
                    assert c.r[8:]==r[8:] and c.other==other and c.r[4]==event
                    assert c.pr==pr and c.sr==sr and c.calls==1
                    assert (c.gbr,c.fpscr,c.fpul,c.fr,c.control)==(gbr,fpscr,fpul,fr,control)
                    assert not c.vbr_writes and not c.iprb_writes;cases+=1
    return dict(actual_sdk_integer_dispatch_to_linked_callback_cases=cases,
        exact_external_owner_source_and_program_sha_checked=True,
        actual_sdk_event_table_lookup_and_jsr_executed=True,
        return_to_sdk_continuation_and_default_forward_checked=True,
        full_sdk_fpu_prologue_and_epilogue_executed=False,
        native_dispatch_c_payload='ABI-adversarial substitute',
        console_performance_verified=False)


def check(resident,*,worker=None,stage=None,self_test=False):
    require(__debug__,'SDK SCI instruction model requires Python assertions')
    marker=resident[4].get(MARKER+'resident')
    callback=resident[4].get(MARKER+'callback')
    paired=worker[4].get(MARKER+'worker') if worker is not None else None
    staged=stage[4].get(MARKER+'stage') if stage is not None else None
    require(bool(marker)==bool(callback),'SDK SCI resident/callback marker mismatch')
    if not marker:
        require(not paired and not staged,'SDK SCI stage/worker marker without resident')
        return {'enabled':False}
    require(worker is not None,'SDK SCI route requires paired worker artifact')
    require(stage is not None,'SDK SCI route requires paired cold stage artifact')
    require(marker==callback==paired==(1,0,0,0xfff1),'SDK SCI paired marker mismatch')
    if stage is not None:require(staged==(1,0,0,0xfff1),'SDK SCI paired stage marker mismatch')
    require('__kui_native_sci_priority7_resident' not in resident[4],
            'SDK SCI route must retain level1 and exclude priority7')
    pins=pin_bodies(resident)
    cold=stage_pins(stage) if stage is not None else {}
    callbacks=callback_cases(resident)
    hooks,unhooks,dispatches,transitions=ownership_cases(resident)
    rejected=0
    if self_test:
        for name,(address,count,_) in pins.items():
            section=resident[3][resident[4][name][3]];start=section[4]+address-section[3]
            for at in range(count):
                altered=bytearray(resident[0]);altered[start+at]^=1
                try:pin_bodies((bytes(altered),*resident[1:]))
                except ValueError:rejected+=1
                else:raise ValueError('SDK SCI audit accepted changed callback/ownership instruction')
        for image,pinned,probe in ((resident,
            {name:(record[0],record[1]) for name,record in {
                name:resident[4][name] for name in ('_kui_native_sdk_sci_route_entry','_kui_native_sdk_sci_route_exports')}.items()},
            pin_bodies),(stage,{name:(record[0],record[1]) for name,record in cold.items()},stage_pins)):
            if image is None:continue
            for name,(address,count) in pinned.items():
                section=image[3][image[4][name][3]];start=section[4]+address-section[3]
                for at in range(count):
                    altered=bytearray(image[0]);altered[start+at]^=1
                    try:probe((bytes(altered),*image[1:]))
                    except ValueError:rejected+=1
                    else:raise ValueError('SDK SCI audit accepted a changed typed binding/cold publisher')
    return dict(enabled=True,paired_sdk_sci_markers=True,immutable_descriptor_bound=True,
        callback_private_stack_prefix_bytes=0,sdk_owner_stack_save_bytes=16,
        cold_binder_and_admission_pins=cold,cold_binder_instruction_modelled=False,
        cold_binder_source_reviewed=True,
        cold_binder_source_sha256=hashlib.sha256((ROOT/'src/loader/vmu_native_sdk_sci_route_stage.inc').read_bytes()).hexdigest(),
        immutable_function_pins=pins,callback_instruction_cases=callbacks,
        hook_ownership_instruction_cases=hooks,unhook_ownership_instruction_cases=unhooks,
        dispatcher_instruction_cases=dispatches,live_route_loss_transition_cases=transitions,mutation_rejections=rejected,
        all_16_sdk_caller_imasks_checked=True,both_gpr_banks_checked=True,
        sdk_original_sr_sp_pr_r4_and_callee_saved_preserved=True,
        busy_low_stack_never_entered=True,bad_guard_forwarded_without_c=True,
        default_callback_tail_forwarded=True,bootstrap_priority_zero=True,retained_priority_one=True,
        vbr_and_both_sdk_slots_rechecked=True,no_vbr_write_or_raw_rte_entry=True,
        no_fpu_state_changes=True,native_deep_reader_c_payload_model='ABI-adversarial substitute',
        sdk_full_fpu_context_model='owned by SDK, outside this bounded model',
        console_cause_confirmed=False,console_performance_verified=False)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path)
    p.add_argument('--self-test',action='store_true')
    p.add_argument('--startup',type=Path,help='optional exact external Evolution2 startup for actual SDK dispatcher replay')
    a=p.parse_args()
    try:
        r,w,s=(read_elf(a.directory/name) for name in ('resident-sci.elf','native-worker.elf','stage.elf'))
        result=check(r,worker=w,stage=s,self_test=a.self_test)
        if a.startup:result['external_sdk_dispatch']=check_sdk_dispatch(r,a.startup)
    except (OSError,ValueError,AssertionError,KeyError,struct.error) as e:
        p.exit(2,'SDK SCI-route audit refused: '+str(e)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
