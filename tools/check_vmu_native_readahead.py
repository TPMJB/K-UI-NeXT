#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the isolated native read-ahead image, typed bindings and stack switch.

The actual SH gateway is executed with both register banks and every IMASK.
Its C payload is an ABI adversary here; the real cache/producer is exercised
by separate sanitizer-backed host tests. This is not console performance proof.
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
from check_vmu_native_sdk_sci_route import SDKCPU,RETURN,MASK,initialized_at
from check_vmu_native_background import CALLBACKS,ESCAPES,disassemble
from check_retail_instructions import audit as instruction_audit
from vmu_native_relocations import generate,physical,validate_loads
import check_retail_stack as graph

LOW,GUARD=0x8c004000,0x8c007ba0
AUX,SPAN=0x8c421000,0x7000
STACK_BOTTOM,STACK_TOP=0xac427000,0xac428000
CANARY=0x4b524147
MARK='__kui_native_readahead_'
# Reviewed pins are deliberately separate from the existing SCI1 profile.
# Populated only after inspection of the new linked profile, never at runtime.
CODE_PIN=(5120,'ad53f64817c9b65869bbe21e8d28a09ba98d75567709a6099bf51dd8551e2bd9')
DIAGNOSTIC_CODE_PIN=(6016,'d355c1383364a52f8f3d1b82dad6359ea695b0e15287b1c0cd9a4ecba231db92')
REPAIR_DIAGNOSTIC_CODE_PIN=(6016,'46c8f355865297918df512b6053328fc88579d32fbb19b3576f9b5b9fc85d1e9')
PIO_CODE_PIN=(5920,'e7973080e08364c228ef9ecade3ab09e9ab3fe5b6c2ce3ade2ef2879e4a218cd')
PIO_PAIR_CODE_PIN=(6144,'13f3a6ee0841966d919723d68dff8909cc1326e450d63fe9d9a6f8e7f445f246')
PIO_PAIR_MARK='__kui_native_readahead_pio_pair_'
PIO_MARK='__kui_native_readahead_pio_'
REPAIR_DIAGNOSTIC_MARKER='__kui_native_readahead_repair_diagnostic_auxiliary'
DIAGNOSTIC_MARK='__kui_native_readahead_diagnostic_'
LOW_PINS={
    '_deliver_work':(456,'73ed6d63b6e67b122d11d0185a009d2c28f81face7e99aeda639660b6eb61aeb'),
    '_deliver':(60,'d32fd5a483cdffee78867c2239be72d693e1d1cc46f62e9b0ae2cda2f5e3eb46'),
}
WORKER_PINS={
    '_reader_readahead_exports':(160,'7408f9b61bad644e7a5c20dc0766138fb36f4ee24a8ef3cd751e93d4e316489e'),
    '_reader_readahead_visit':(56,'2af85f7d6a6a88765d1e8cf645b45683e794c2bf08ebca7a5c1bb6666a78186a'),
    '_reader_cooperative_service':(32,'d5c8c4caf5c8bf445a7f0d5ab500ee6f1e7ef46cbdfdf2c46070c0058b683ea4'),
}
DIAGNOSTIC_WORKER_PINS={
    '_reader_readahead_exports':(160,'5b5248bfa68cadfd8dad5777f91dd6b4625588954539294f7bb91a0dc02f484a'),
    '_reader_readahead_visit':(56,'2af85f7d6a6a88765d1e8cf645b45683e794c2bf08ebca7a5c1bb6666a78186a'),
    '_reader_cooperative_service':(32,'774e6ef572a539374bec18f4954cf99399096f12899db0ea67cc0460c209391b'),
    '_reader_readahead_failure':(172,'908c6b98fec8dc12551a0c8ab889609b30478435bc26597b909299c5abec6da4'),
}
PIO_WORKER_PINS={
    '_reader_readahead_exports':(160,'6c4c1a2f4cc98ff6457e0decac9ee1e4477235b990cb7aae75726f473ffe13e2'),
    '_reader_readahead_visit':(56,'2af85f7d6a6a88765d1e8cf645b45683e794c2bf08ebca7a5c1bb6666a78186a'),
    '_reader_cooperative_service':(32,'86b3e40e60ef13acabffe1461b97f7e0e7dda7a38248f486d08ab7c536a53a0f'),
    '_reader_readahead_failure':(172,'cbbb1917d4795bc8eed2ab4568939f6f98c7aa7b11d0bfb5bd4c43e7ed7d1382'),
}
PIO_PAIR_WORKER_PINS={
    '_reader_readahead_exports':(160,'522dbac9b85fe2b51cee66c1223eefe02bf701a6594992abcc49183641f3fc89'),
    '_reader_readahead_visit':(56,'2af85f7d6a6a88765d1e8cf645b45683e794c2bf08ebca7a5c1bb6666a78186a'),
    '_reader_cooperative_service':(32,'b6dade21be0807a8ac8e608abe02b70d90f2f47faa086e6396ebdad6e4cc6ca0'),
    '_reader_readahead_failure':(172,'e704e2fefce22e839fb62a24a10e35c949bb2c8615f84be30367e830df8c27e2'),
}
STRUCTURAL_LOW_PINS={
    '_deliver_work':(456,'7ffc9bfda8c4328c9e92f4cead9270c8e97916e609dc49b367ccdaa4d1feda4c'),
    '_deliver':(60,'d32fd5a483cdffee78867c2239be72d693e1d1cc46f62e9b0ae2cda2f5e3eb46'),
}
DUAL_WORKER_PINS={
    '_reader_readahead_exports':(200,'04a1748a2f5f36a1e79fa2a791a5e16a9f90c6ac632f13bd797abf48ce58545c'),
    '_reader_readahead_visit':(56,'c44759200e43937665b2987281e995b4f39675f1e05180371aad4078cc02dbf3'),
    '_reader_cooperative_service':(32,'68d4a9acf31e3ce844a7ee27cac22620252b3c0ae09afba8c2dc0463f8496227'),
    '_reader_readahead_failure':(172,'4509901703ae22c1ac0124f3077e06dc3c3466e60bebff2234e066af50d3833f'),
}
# Failure-only controller/lifetime diagnostics retain their own complete
# instruction/literal map. The published dual profile above stays exact.
DUAL_WITNESS_WORKER_PINS={
    '_reader_readahead_exports':(200,'6a753b4f52fd9aad30c334241ea8c0de92cefcac3c3a85009af4645b260d67ef'),
    '_reader_readahead_visit':(56,'c44759200e43937665b2987281e995b4f39675f1e05180371aad4078cc02dbf3'),
    '_reader_cooperative_service':(32,'9a04c43d4b1fc25d6b832a808b55f5bd28df0e70fd24134aa060af0bcb7c4e68'),
    '_reader_readahead_failure':(172,'bf4f6464be4b0e92b896a6f3604db5c16dc8c0582638b7454e5181521a9f2532'),
}
STRUCTURAL_LOAN_PIN=(748,'ab219f0dc9ad6dfbe01be09b3c042d63992ead50a68245ddbb9ba6116b0e352b')
LOAN_PIN=(744,'a06ecbd795204545b4df1ca6f5efc3883a0a81b4e0391b088a12388203ff376c')
GATEWAY_MEMORY={}

# Exact whole bodies reviewed for the paired owned-timer/owner-stack profile.
GAME_STARTUP_WORKER_PINS={
    '_reader_readahead_exports':(200, '13ca433d369112a4b3184b17ec96e025a76e142e537d65c8d3bba2c6e9ce4d5e'),
    '_reader_readahead_visit':(56, 'c44759200e43937665b2987281e995b4f39675f1e05180371aad4078cc02dbf3'),
    '_reader_cooperative_service':(32, '932a8c6f2a8ffa5ffb5ead01a2a1fbfb8093d39f4c1836deabc9403ef027f505'),
    '_reader_readahead_failure':(172, 'c24b42e15c0664a0c86315fb7928f5e4113dc2cfa3d6da1a8e106822328e9acb'),
}

def require(value,message):
    if not value:raise ValueError(message)

def sha(value):return hashlib.sha256(value).hexdigest()

def structural_profile(resident):
    marker=resident[4].get('__kui_native_sdk_sci_structural_resident')
    if marker is not None:
        require(marker==(1,0,0,0xfff1) and
                resident[4].get('__kui_native_sdk_sci_route_resident')==(1,0,0,0xfff1),
                'read-ahead structural low profile marker differs')
    return marker is not None


def diagnostic_profile(aux):
    mark=aux[4].get(DIAGNOSTIC_MARK+'auxiliary')
    if mark is not None:
        require(mark==(1,0,0,0xfff1),'read-ahead diagnostic auxiliary marker differs')
    return mark is not None

def public_bytes(aux):return 656 if diagnostic_profile(aux) else 76

def repair_diagnostic_profile(aux):
    mark=aux[4].get(REPAIR_DIAGNOSTIC_MARKER)
    if mark is not None:
        require(mark==(1,0,0,0xfff1) and diagnostic_profile(aux),
                'read-ahead repair diagnostic marker differs or lacks its diagnostic profile')
    return mark is not None

def pio_profile(aux):
    mark=aux[4].get(PIO_MARK+'auxiliary')
    if mark is not None:
        require(mark==(1,0,0,0xfff1) and repair_diagnostic_profile(aux),
                'read-ahead PIO marker differs or lacks its diagnostic profile')
    return mark is not None

def pio_pair_profile(aux):
    mark=aux[4].get(PIO_PAIR_MARK+'auxiliary')
    if mark is not None:
        require(mark==(1,0,0,0xfff1) and pio_profile(aux),
                'read-ahead PIO pair marker differs or lacks its PIO profile')
    return mark is not None

def code_payload(aux):
    parts=[]
    for sec in aux[3]:
        if sec[1]==1 and sec[2]&2 and not sec[2]&1 and 0x8c422000<=sec[3]<0x8c426000:
            parts.append((sec[3],aux[0][sec[4]:sec[4]+sec[5]]))
    require(parts,'read-ahead code is missing')
    parts.sort();begin=parts[0][0];end=max(a+len(b) for a,b in parts)
    require(0x8c422000<=begin<end<=0x8c426000,'read-ahead code escapes its16KiB budget')
    body=bytearray(end-begin)
    for at,data in parts:body[at-begin:at-begin+len(data)]=data
    return begin,bytes(body)

def pinned_code(aux):
    address,body=code_payload(aux)
    selected=PIO_PAIR_CODE_PIN if pio_pair_profile(aux) else PIO_CODE_PIN if pio_profile(aux) else \
        REPAIR_DIAGNOSTIC_CODE_PIN if repair_diagnostic_profile(aux) else \
        DIAGNOSTIC_CODE_PIN if diagnostic_profile(aux) else CODE_PIN
    require(selected is not None and (len(body),sha(body))==selected,
            'read-ahead immutable code pin changed')
    return address,body

def pinned_functions(image,pins,label):
    for name,(count,digest) in pins.items():
        _,body,_=symbol_bytes(image,name,2)
        require((len(body),sha(body))==(count,digest),label+' instructions changed: '+name)

def pinned_loan_boundary(resident):
    # LTO's function size includes eight bytes after its dedicated section.
    # Pin the exact executable section and its complete symbol shape instead
    # of relaxing symbol_bytes for this or any earlier profile.
    selected=STRUCTURAL_LOAN_PIN if structural_profile(resident) else LOAN_PIN
    require(selected is not None,'structural QUIESCE boundary has no reviewed pin')
    at,count,kind,owner=resident[4]['_kui_vmu_observe_sd_lease']
    sec=resident[3][owner]
    require(kind==2 and sec[1]==1 and sec[2]&7==6 and at==sec[3] and
            count==selected[0]+8 and sec[5]==selected[0] and LOW<=at<at+sec[5]<=GUARD,
            'read-ahead QUIESCE loan boundary shape changed')
    require(resident[4]['__vmu_sd_lease_begin'][0]==at and
            resident[4]['__vmu_sd_lease_end'][0]==at+sec[5],
            'read-ahead QUIESCE loan section bounds changed')
    body=resident[0][sec[4]:sec[4]+sec[5]]
    require(sha(body)==selected[1],'read-ahead QUIESCE loan instructions changed')
    return dict(address=hex(at),bytes=len(body),sha256=sha(body))

def byte_mutations(image,names,probe):
    rejected=0
    for name,kind in names:
        address,body,section=symbol_bytes(image,name,kind)
        offset=section[4]+address-section[3]
        for i in range(len(body)):
            changed=bytearray(image[0]);changed[offset+i]^=1
            try:probe((bytes(changed),*image[1:]))
            except ValueError:rejected+=1
            else:raise ValueError('read-ahead auditor accepted a changed binding: '+name)
    return rejected

def typed_ops(resident):
    address,wire,section=symbol_bytes(resident,'_kui_native_ra_reader_ops',1)
    require(len(wire)==52 and section[2]&7==2,'read-ahead operations must be immutable52B data')
    words=struct.unpack('<13I',wire)
    require(words[:3]==(0x4b52414f,1,52),'read-ahead operation descriptor identity differs')
    pointer=resident[4]['_kui_retail_async_region_storage']
    require(words[3]==pointer[0] and pointer[1:3]==(4,1) and
            resident[3][pointer[3]][1]==8 and LOW<=pointer[0]<GUARD,
            'read-ahead region cell differs from the owned low BSS cell')
    names=('_deliver_work','_hook','_unhook','_kui_sci_stream_fetch','_kui_sci_stream_poll',
           '_kui_sci_stream_take','_kui_sci_stream_stop','_kui_sci_stream_busy','_kui_retail_cursor_feed')
    require(words[4:]==tuple(resident[4][name][0] for name in names),'read-ahead low function bindings differ')
    for name in names:
        at,body,sec=symbol_bytes(resident,name,2)
        require(sec[2]&6==6 and LOW+44<=at<at+len(body)<=GUARD,'read-ahead low operation is not owned code')
    entry,pointer_bytes,sec=symbol_bytes(resident,'_kui_native_ra_reader_ops_entry',1)
    require(entry==LOW+40 and len(pointer_bytes)==4 and not sec[2]&1 and
            pointer_bytes==struct.pack('<I',address),'read-ahead entry+40 does not bind its typed descriptor')
    return dict(address=address,bytes=52,region_cell=pointer[0],functions=dict(zip(names,words[4:])))

def image_contract(directory,aux,resident):
    binary=(directory/'native-ra-aux.bin').read_bytes();wire=(directory/'native-ra-aux.reloc').read_bytes()
    require(generate(aux[0],binary,AUX,SPAN)==wire,'read-ahead relocation table differs from exact linked extraction')
    loads=validate_loads(aux[0],binary,AUX,SPAN)
    address,header,sec=symbol_bytes(aux,'_kui_native_readahead_aux_exports',1)
    context=aux[4]['_kui_native_readahead_aux_context']
    visit=aux[4]['_kui_native_readahead_aux_visit'][0]
    init=aux[4]['_kui_native_readahead_aux_init'][0]
    require(address==AUX and len(header)==64 and sec[2]&7==2,'read-ahead export header is not immutable64B at its exact base')
    expected=(0x4b524131,1,64,visit,init,context[0],AUX+len(binary),STACK_BOTTOM,STACK_TOP,STACK_TOP,0,0,0,0,0,0)
    require(header==struct.pack('<16I',*expected),'read-ahead exports/context/code/stack bindings differ')
    require(context[2]==1 and not context[0]&31 and 0xac421040<=context[0]<
            context[0]+context[1]<=0xac422000 and aux[3][context[3]][2]&7==3,
            'read-ahead complete mutable context escapes its uncached owned page')
    require(context[1]==(904 if diagnostic_profile(aux) else 172) and
            not any(initialized_at(aux,context[0],context[1])),
            'read-ahead context is not initially zero, including busy and statistics')
    require(AUX+0x1000<=visit<AUX+len(binary) and AUX+0x1000<=init<AUX+len(binary),
            'read-ahead entries escape initialized P1 code')
    for name,(at,count,kind,owner) in aux[4].items():
        if not count or owner>=len(aux[3]):continue
        sec=aux[3][owner]
        if sec[2]&3==3:require(at&0xe0000000==0xa0000000,'mutable read-ahead object uses cached alias: '+name)
    ops=typed_ops(resident)
    return dict(binary_sha256=sha(binary),binary_bytes=len(binary),relocation_sha256=sha(wire),
        relocation_records=(len(wire)-72)//8,link_base=hex(AUX),relocation_span=SPAN,
        context=hex(context[0]),context_bytes=context[1],public_bytes=public_bytes(aux),
        private_stack_bottom=hex(STACK_BOTTOM),
        private_stack_top=hex(STACK_TOP),cache_data_bytes=0x30000,lease_bytes=0x60000,
        worker_map_region_offsets_unchanged=True,typed_low_ops=ops,physical_loads=loads)

class GatewayCPU(SDKCPU):
    def __init__(self,resident,aux,seed,mask,bank,blocked,event,result,mode):
        super().__init__(resident,seed,mask,bank,blocked)
        self.symbols=dict(resident[4],**aux[4]);self.aux=aux
        key=(id(resident[0]),id(aux[0]))
        if key not in GATEWAY_MEMORY:
            memory=bytearray(self.memory)
            for kind,offset,address,_,count,_,_,_ in aux[2]:
                if kind==1 and count:
                    at=self.offset(address,count);memory[at:at+count]=aux[0][offset:offset+count]
            GATEWAY_MEMORY[key]=bytes(memory)
        self.memory=GATEWAY_MEMORY[key]
        self.context=aux[4]['_kui_native_readahead_aux_context'][0]
        self.dispatch=aux[4]['_kui_native_readahead_aux_dispatch'][0]
        self.targets={self.dispatch};self.calls=0;self.event=event;self.result=result;self.mode=mode
        self.r[4:8]=[event,0x1357,0x8c420400,0]
        self.write(self.context,1 if mode=='busy' else 0)
        self.write(self.context+64,0x1234)
        for at in (STACK_BOTTOM,STACK_TOP-16):
            for i in range(4):self.write(at+4*i,CANARY)
        if mode=='bottom':self.write(STACK_BOTTOM,0)
        if mode=='top':self.write(STACK_TOP-16,0)
        self.initial=self.snapshot();self.arguments=self.r[4:8].copy();self.writes=[]

    def callee(self):
        self.calls+=1;continuation=self.pr
        assert self.r[15]==STACK_TOP-32 and self.sr&0x100000f0==0x100000f0
        assert self.r[4:8]==self.arguments and self.read(self.context)==1
        if self.mode=='corrupt_bottom':self.write(STACK_BOTTOM,0)
        if self.mode=='corrupt_top':self.write(STACK_TOP-16,0)
        for i in range(8):self.r[i]=(0xa357913b+i*0x13579+self.calls)&MASK
        self.r[0]=self.result;self.sr^=1;self.pr^=0x13579135
        self.mach^=0x13579bdf;self.macl^=0xfdb97531;self.pc=continuation

def gateway_cases(resident,aux):
    cases=0
    for mask in range(16):
        for bank in range(2):
            for blocked in (False,True):
                for event in (1,2,3,4,5,6,0xffffffff):
                    for mode in ('ok','busy','bottom','top','corrupt_bottom','corrupt_top'):
                        for result in (0,1,0xffffffff):
                            c=GatewayCPU(resident,aux,cases,mask,bank,blocked,event,result,mode)
                            before=c.snapshot();c.run_symbol('_kui_native_readahead_aux_visit')
                            r,other,pr,sr,_,_,gbr,fpscr,fpul,fr,controls=before
                            assert c.r[8:]==r[8:] and c.other==other and c.pr==pr and c.sr==sr
                            assert (c.gbr,c.fpscr,c.fpul,c.fr,c.control)==(gbr,fpscr,fpul,fr,controls)
                            reject=mode in ('busy','bottom','top')
                            assert c.calls==int(not reject)
                            fault=mode!='ok'
                            expected=((1 if event==1 else 0) if reject else
                                      (0 if event==3 else 1)) if fault else result
                            assert c.r[0]==expected,(event,mode,hex(c.r[0]),hex(expected))
                            assert c.read(c.context)==(1 if mode=='busy' else 0)
                            assert c.read(c.context+64)==0x1234+int(fault)
                            for _,at,_,_ in c.writes:
                                assert (r[15]-20<=at<r[15] or c.context<=at<c.context+76 or
                                        at in (STACK_BOTTOM,STACK_TOP-16)),'gateway wrote outside owned frames'
                            if reject:assert all(not r[15]-20<=at<r[15] for _,at,_,_ in c.writes)
                            cases+=1
    return dict(actual_linked_gateway_cases=cases,all16_imasks=True,both_integer_banks=True,
        both_bl_states=True,owner_sr_pr_sp_and_callee_saved_preserved=True,
        private_stack_switch_before_c=True,busy_and_guard_checks_before_stack_writes=True,
        parked_owner_stack_bytes=20,auxiliary_stack_prefix_bytes=0,
        c_payload_model='ABI-adversarial substitute; actual producer separately host-tested')

class WorkerCPU(SDKCPU):
    def __init__(self,resident,worker,payload,event,variant,public=76):
        super().__init__(resident,seed=9,mask=15,bank=1)
        self.symbols=dict(resident[4],**worker[4]);self.worker=worker
        key=(id(resident[0]),id(worker[0]))
        if key not in GATEWAY_MEMORY:
            memory=bytearray(self.memory)
            for kind,offset,address,_,count,_,_,_ in worker[2]:
                if kind==1 and count:
                    at=self.offset(address,count);memory[at:at+count]=worker[0][offset:offset+count]
            GATEWAY_MEMORY[key]=bytes(memory)
        self.memory=GATEWAY_MEMORY[key]
        lease=(payload&0x1fffffff)|0x80000000
        self.region=lease+0x20400;self.aux_header=(lease+0x21000)|0x20000000
        self.context=(lease+0x21040)|0x20000000;self.entry=lease+0x22000
        # The reviewed32-bit worker configuration places lease.payload at
        # offset220. The pinned getter binds this exact field and address.
        self.write(worker[4]['_config'][0]+220,payload)
        if '__kui_native_dual_readers_worker' in worker[4]:
            # Actual SH console layout is independently compared for both
            # engines by test_native_dual_readers.py. The X getter requires
            # this exact retained map and completed SDK publication first.
            self.write(self.region+0x27c,lease+0x20000)
            self.write(lease+0x20000+48,1)
            self.write(lease+0x20000+44,1)
            self.write(self.region+0x530,0x53434931)
        values=(0x4b524131,1,64,self.entry,self.entry+2,self.context,
                lease+0x24000,(lease+0x27000)|0x20000000,(lease+0x28000)|0x20000000,
                (lease+0x28000)|0x20000000,0,0,0,0,0,0)
        for i,value in enumerate(values):self.write(self.aux_header+4*i,value)
        self.write(self.region+0x500,self.entry);self.write(self.region+0x504,self.context)
        if variant=='magic':self.write(self.aux_header,0)
        if variant=='version':self.write(self.aux_header+4,2)
        if variant=='bytes':self.write(self.aux_header+8,60)
        if variant=='image_low':self.write(self.aux_header+24,lease+0x22000)
        if variant=='image_high':self.write(self.aux_header+24,lease+0x26004)
        if variant=='entry_low':self.write(self.aux_header+12,lease+0x21ffe)
        if variant=='entry_high':self.write(self.aux_header+12,lease+0x24000)
        if variant=='bound_entry':self.write(self.region+0x500,0)
        if variant=='bound_context':self.write(self.region+0x504,0)
        if variant=='context_low':self.write(self.aux_header+20,self.aux_header+32)
        if variant in ('context_at_public_limit','context_high'):
            self.context=self.aux_header+4096-public+(4 if variant=='context_high' else 0)
            self.write(self.aux_header+20,self.context)
            self.write(self.region+0x504,self.context)
        self.targets={self.entry};self.event=event;self.calls=0
        self.r[4]=event;self.r[15]=0xac11ffe0
        self.initial=self.snapshot();self.durable=worker[4]['_durable_card'];self.durable_reads=set()
        self.writes=[]

    def read(self,address,count=4):
        if hasattr(self,'durable') and self.durable[0]<=address<self.durable[0]+self.durable[1]:
            self.durable_reads.add(address)
        return super().read(address,count)

    def plain(self,op,pc):
        if op>>8==0xc8:self.flag(not(self.r[0]&(op&255)));return
        super().plain(op,pc)

    def callee(self):
        assert self.r[4:8]==[self.event,0,self.region,0]
        self.calls+=1;continuation=self.pr
        for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd)&MASK
        # This target represents the complete auxiliary gateway, whose own
        # instruction proof establishes PR preservation around its C body.
        self.r[0]=1;self.sr^=1;self.pc=continuation

def worker_bindings(resident,worker,*,diagnostic=False,pio=False,pio_pair=False,dual=False,witness=False):
    pins={}
    from check_native_game_startup_profile import game_startup_profile
    owned_timers=game_startup_profile(worker,resident)
    require(not owned_timers or witness and dual, 'Owned timer read-ahead pins require dual witness profile')
    selected=GAME_STARTUP_WORKER_PINS if owned_timers else DUAL_WITNESS_WORKER_PINS if witness else DUAL_WORKER_PINS if dual else PIO_PAIR_WORKER_PINS if pio_pair else PIO_WORKER_PINS if pio else \
        DIAGNOSTIC_WORKER_PINS if diagnostic else WORKER_PINS
    from check_native_followup_profile import worker_pins as followup_worker_pins
    selected=followup_worker_pins(worker,'read_ahead',selected)
    require(selected,'read-ahead reviewed worker pins are missing')
    public=656 if diagnostic else 76
    for name,(count,digest) in selected.items():
        address,body,section=symbol_bytes(worker,name,2)
        require((len(body),sha(body))==(count,digest) and section[2]&7==6,
                'read-ahead worker binding instructions changed: '+name)
        pins[name]=(address,count,digest)
    cases=0
    variants=('valid','magic','version','bytes','image_low','image_high','entry_low','entry_high',
              'bound_entry','bound_context','context_low','context_at_public_limit','context_high')
    for payload in (0x0c400000,0x8c400000,0xac400000,0x0cfa0000):
        for event in (2,6):
            for variant in variants:
                c=WorkerCPU(resident,worker,payload,event,variant,public)
                c.run_symbol('_reader_readahead_visit')
                assert c.calls==int(variant in ('valid','context_at_public_limit'))
                before=c.initial
                assert c.r[8:]==before[0][8:] and c.other==before[1] and c.pr==before[2]
                assert c.control==before[10] and c.r[15]==before[0][15]
                cases+=1
    c=WorkerCPU(resident,worker,0x8c400000,2,'valid',public)
    c.run_symbol('_reader_cooperative_service');assert c.calls==1
    require(len(c.durable_reads)==2,'worker heartbeat must check both durable writer busy fields')
    for address in c.durable_reads:
        c=WorkerCPU(resident,worker,0x8c400000,2,'valid',public);c.write(address,1)
        c.run_symbol('_reader_cooperative_service');assert c.calls==0;cases+=1
    return dict(actual_worker_binding_cases=cases,immutable_function_pins=pins,
        all_three_payload_aliases=True,highest_owned_lease_checked=True,
        malformed_headers_and_contexts_refused=True,both_durable_writer_busy_fields_refused=True,
        heartbeat_and_capture_start_arguments_checked=True,complete_public_context_bytes=public,
        exact_public_boundary_and_one_word_overflow_checked=True,
        c_auxiliary_target='ABI substitute; complete auxiliary gateway proved separately')

def worker_diagnostic_display(worker):
    indices=bytes((0,1,2,3,4,5,8,9,10,11,12,13,14,15,16,17,18,6,7,
                   19,20,21,22,23,27,28,29,30,31,24,25,26,35,36,32,33,34))
    for name,expected in (('_field.2',indices),('_count.1',bytes((5,5,5,4,5,5,5,3)))):
        _,body,sec=symbol_bytes(worker,name,1)
        require(sec[2]&7==2 and body==expected,'diagnostic report field/count table changed')
    _,body,sec=symbol_bytes(worker,'_label.0',1)
    require(sec[2]&7==2 and len(body)==28,'diagnostic report row labels are not a typed immutable7-pointer table')
    labels=('BLK BASE COUNT END OP','LOAN RX R0 R1 KEPT','ARRIVED LOST PRE STATE POS',
            'PRE SAR DAR TCR CHCR DMAOR','POST SAR DAR TCR CHCR DMAOR',
            'PRE SCI PINS TMU POST STATE POS','POST SCI PINS TMU')
    from check_native_followup_profile import raw_owner_profile
    if raw_owner_profile(worker):
        labels=(*labels[:2],'ARRIVED LOST STATE POS',*labels[3:])
    for pointer,label in zip(struct.unpack('<7I',body),labels):
        owners=[s for s in worker[3] if s[1]==1 and s[2]&7==2 and
                s[3]<=pointer<=s[3]+s[5]-len(label)-1]
        require(len(owners)==1,'diagnostic row label escapes immutable worker bytes')
        require(initialized_at(worker,pointer,len(label)+1)==label.encode()+b'\0',
                'diagnostic row label text changed')
    return dict(all37_snapshot_words_exactly_once=True,immutable_byte_index_table=True,
        row_word_counts=[5,5,5,4,5,5,5,3],typed_immutable_label_pointers=7,
        actual_display_execution_modelled=False)

class LoanCPU(SDKCPU):
    """Actual low QUIESCE slice after separately checked outer admission.

    The pinned complete lease section closes incoming control flow. This
    slice executes every demand/loan/active predicate through the actual
    auxiliary call and its success/refusal branch; the complete auxiliary
    gateway is the independently proved typed ABI target.
    """
    TARGET=0x8c422000
    def __init__(self,resident,bank,blocked,variant,result):
        super().__init__(resident,seed=17,mask=15,bank=bank,blocked=blocked)
        self.lease_entry=resident[4]['_kui_vmu_observe_sd_lease'][0]
        self.r[15]=0xac11ffe0
        self.region=0x8c420400
        self.write(self.pointer,self.region);self.write(self.active,0)
        for offset in (0x24c,0x2bc,0x2cc,0x2f4,0x370,0x374):self.write(self.region+offset,0)
        self.write(self.region+0x378,1)
        self.write(self.region+0x500,0 if variant=='unbound' else self.TARGET)
        offsets={'uninitialized':0x378,'pending':0x370,'executing':0x374,
                 'active_demand':0x2bc,'in_irq':0x2cc,'released_handler':0x24c,
                 'loan_active':0x2f4}
        if variant in offsets:self.write(self.region+offsets[variant],0 if variant=='uninitialized' else 1)
        if variant=='hook_active':self.write(self.active,1)
        self.targets={self.TARGET};self.calls=0;self.result=result
        self.initial=self.snapshot();self.writes=[]

    def callee(self):
        assert self.r[4:8]==[3,0,self.region,0]
        assert self.sr&0xf0==0xf0 and self.read(self.active)==1
        assert not any(self.read(self.region+at) for at in (0x370,0x374,0x2bc,0x2cc,0x24c,0x2f4))
        self.calls+=1;continuation=self.pr
        for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd)&MASK
        self.r[0]=self.result;self.sr^=1;self.pc=continuation


def loan_cases(resident):
    require(__debug__,'read-ahead loan instruction proof requires Python assertions')
    base=resident[4]['_kui_vmu_observe_sd_lease'][0]
    cases=mask_refusals=0
    variants=('unbound','bound','uninitialized','pending','executing','active_demand',
              'in_irq','released_handler','loan_active','hook_active')
    for bank in range(2):
        for blocked in (False,True):
            for variant in variants:
                for result in (0,1,0xffffffff):
                    c=LoanCPU(resident,bank,blocked,variant,result)
                    accepted=variant=='unbound' or (variant=='bound' and result!=0)
                    # Fixed offsets belong to the separately pinned complete
                    # executable lease section, never a permissive search.
                    stop=base+(0xc2 if accepted else 0x82)
                    c.run_to(base+0x52,stop)
                    assert c.calls==int(variant=='bound')
                    assert c.read(c.active)==(1 if accepted or variant=='hook_active' else 0)
                    assert c.r[15]==c.initial[0][15] and c.other==c.initial[1]
                    assert c.control==c.initial[10] and c.sr&~1==c.initial[3]&~1
                    assert not any(c.region<=address<c.region+0x760 for _,address,_,_ in c.writes)
                    assert all(address==c.active for _,address,_,_ in c.writes)
                    cases+=1
            for mask in range(15):
                c=SDKCPU(resident,seed=mask,mask=mask,bank=bank,blocked=blocked)
                c.write(c.active,0);before=c.snapshot();c.writes=[]
                c.run_symbol('_kui_vmu_observe_sd_lease')
                assert c.r[0]==MASK and c.r[8:]==before[0][8:]
                assert c.other==before[1] and c.pr==before[2] and c.sr&~1==before[3]&~1
                assert c.control==before[10] and c.read(c.active)==0
                assert all(before[0][15]-68<=address<before[0][15] for _,address,_,_ in c.writes)
                mask_refusals+=1
    return dict(actual_linked_quiesce_boundary_cases=cases,outer_non_imask15_refusals=mask_refusals,
        exact_section_offset_entry='0x52',typed_auxiliary_event_arg_region_reserved_checked=True,
        real_pending_demand_never_quiesced=True,refused_quiesce_clears_only_owned_low_lock=True,
        preexisting_active_low_lock_preserved=True,null_binding_is_quiesce_success=True,
        success_rejoins_before_stream_busy_check=True,
        scope='Actual post-admission low slice; complete pinned section and real C integration tests cover outer ownership/transport',
        auxiliary_target='Typed gateway ABI substitute; actual auxiliary gateway proved independently')


HW_READS=((0xffa00010,4),(0xffa00014,4),(0xffa00018,4),(0xffa0001c,4),
          (0xffa00040,4),(0xffe00000,1),(0xffe00004,1),(0xffe00008,1),
          (0xffe00010,1),(0xffe0001c,1),(0xffc00004,1),(0xff800030,2),
          (0xffd8000c,4))

class SnapshotCPU(GatewayCPU):
    """Run the real diagnostic C helpers with strict read-only MMIO.

    No transport functions are substituted: these helpers must only inspect
    the listed registers and write their owned context/private stack.
    """
    def __init__(self,resident,aux,seed,bank=0,blocked=False):
        super().__init__(resident,aux,seed,15,bank,blocked,2,0,'ok')
        self.targets=set();self.hw_reads=[];self.hw_seed=seed
        self.r[15]=STACK_TOP-32;self.region=0x8c420400
        for at in range(0,904,4):self.write(self.context+at,0)
        for at in range(0,0x760,4):self.write(self.region+at,0)
        self.writes=[]

    def run_to(self,entry,stop):
        # The shared cold replay has the same exact SH branch/delay-slot
        # semantics with a bounded120,000-instruction budget. The tiny SDK
        # callback budget cannot cover two actual byte-wise memset loops.
        from check_vmu_native_readahead_stage import ReadAheadBinderCPU
        ReadAheadBinderCPU.run_to(self,entry,stop)

    def hardware_value(self,address,count):
        return ((address^0xa59c3147)+(self.hw_seed+1)*0x13579b)&((1<<(8*count))-1)

    def plain(self,op,pc):
        if op>>8==0x80:
            self.write(self.r[op>>4&15]+(op&15),self.r[0],1)
            return
        if op&0xf00f==0x2000:
            self.write(self.r[op>>8&15],self.r[op>>4&15],1)
            return
        if op&0xf0ff==0x4010:
            n=op>>8&15;self.r[n]=(self.r[n]-1)&MASK;self.flag(self.r[n]==0)
            return
        if op>>8 in (0x84,0x85):
            count=1 if op>>8==0x84 else 2
            value=self.read(self.r[op>>4&15]+(op&15)*count,count);bits=count*8
            self.r[0]=(value-(1<<bits) if value&(1<<(bits-1)) else value)&MASK
            return
        if op&0xf00f==0x400d:
            n,m=op>>8&15,op>>4&15;shift=self.r[m];value=self.r[n]
            self.r[n]=((value<< (shift&31))&MASK) if not shift&0x80000000 else \
                (value>>(((-shift)&31) or 32))
            return
        if op&0xf00f==0x600a:
            n,m=op>>8&15,op>>4&15;source=self.r[m];borrow=self.sr&1
            self.r[n]=(-source-borrow)&MASK;self.flag(bool(source or borrow))
            return
        if op&0xf0ff in (0x4018,0x4028):
            n=op>>8&15;self.r[n]=(self.r[n]<<(8 if op&255==0x18 else 16))&MASK
            return
        if op&0xf00f==0x6008:
            n,m=op>>8&15,op>>4&15;value=self.r[m]
            self.r[n]=(value&0xffff0000)|((value&255)<<8)|((value>>8)&255)
            return
        if op&0xf00f in (0x600c,0x600d):
            n,m=op>>8&15,op>>4&15
            self.r[n]=self.r[m]&(0xff if op&15==12 else 0xffff)
            return
        if op&0xf00f in (0x6000,0x6001):
            # SH MOV.B/MOV.W @Rm,Rn sign-extend to the full integer word.
            n,m=op>>8&15,op>>4&15;count=1 if op&15==0 else 2
            value=self.read(self.r[m],count);bits=8*count
            self.r[n]=(value-(1<<bits) if value&(1<<(bits-1)) else value)&MASK
            return
        super().plain(op,pc)

    def read(self,address,count=4):
        if address&0xe0000000==0xe0000000:
            require((address,count) in HW_READS,'diagnostic read unexpected hardware register/width')
            self.hw_reads.append((address,count))
            return self.hardware_value(address,count)
        return super().read(address,count)

    def write(self,address,value,count=4):
        require(address&0xe0000000!=0xe0000000,'diagnostic attempted a hardware write')
        super().write(address,value,count)

    def hardware_words(self):
        values=[self.hardware_value(a,n) for a,n in HW_READS]
        return values[:5]+[sum(values[5+i]<<(8*i) for i in range(4)),
            values[9]|values[10]<<8|values[11]<<16,values[12]]

    def body(self,name,args,*,hardware=True):
        self.r[4:4+len(args)]=args
        before=self.snapshot();self.writes=[];self.hw_reads=[]
        self.run_symbol(name)
        require(self.hw_reads==(list(HW_READS) if hardware else []),
                'diagnostic hardware reads differ from reviewed sequence')
        assert self.r[8:]==before[0][8:] and self.other==before[1] and self.pr==before[2]
        assert self.sr&~1==before[3]&~1 and self.control==before[10]
        assert (self.gbr,self.fpscr,self.fpul,self.fr)==before[6:10]
        for _,at,_,size in self.writes:
            require(self.context<=at<=self.context+904-size or
                    STACK_BOTTOM+16<=at<=STACK_TOP-32-size,
                    'diagnostic wrote outside complete owned context/private stack')
        return self


def diagnostic_snapshots(resident,aux):
    if not diagnostic_profile(aux):return {'enabled':False}
    cases=0
    for bank in range(2):
        for blocked in (False,True):
            for seed in (0,7,0xffff):
                c=SnapshotCPU(resident,aux,seed,bank,blocked)
                observation=c.context+752
                c.body('_snapshot_hw',[observation+76])
                assert [c.read(observation+76+4*i) for i in range(8)]==c.hardware_words()
                cases+=1
                for i in range(37):c.write(observation+4*i,0xabc00000+i)
                for at in (712,716,720,740,748):c.write(c.context+at,1)
                for at,value in ((692,0x123456),(700,17),(704,0x223456),(900,2)):
                    c.write(c.context+at,value)
                operation=0x1d0000+seed
                c.body('_snapshot_pre',[4,c.region,operation])
                expected=[4,0xabc00001,2,496,0,0,0,0,0x123456,17,0x223456,operation,
                          0,0,0,0,0,0,0]+c.hardware_words()+[0xabc00000+i for i in range(27,37)]
                assert [c.read(observation+4*i) for i in range(37)]==expected
                cases+=1
                for stage,result in ((1,0),(4,6),(6,7),(7,8),(0xffffffff,0xffffffff)):
                    c.write(observation,stage);c.write(c.context+76,0)
                    histogram=[c.read(c.context+80+4*i) for i in range(56)]
                    c.body('_snapshot_failure',[c.region,result])
                    expected[0]=stage;expected[1]=result
                    expected[27:35]=c.hardware_words();expected[35:37]=[0,0]
                    first=[c.read(c.context+360+4*i) for i in range(37)]
                    last=[c.read(c.context+508+4*i) for i in range(37)]
                    assert first==last==expected and c.read(c.context+76)==1
                    if stage<7 and result<8:
                        assert c.read(c.context+80+4*(stage*8+result))==1
                        histogram[stage*8+result]+=1
                    assert [c.read(c.context+80+4*i) for i in range(56)]==histogram
                    c.write(observation+44,operation+1)
                    c.body('_snapshot_failure',[c.region,result])
                    assert [c.read(c.context+360+4*i) for i in range(37)]==first
                    expected[11]=operation+1
                    assert [c.read(c.context+508+4*i) for i in range(37)]==expected
                    assert c.read(c.context+76)==2
                    if stage<7 and result<8:
                        assert c.read(c.context+80+4*(stage*8+result))==2
                        histogram[stage*8+result]+=1
                    assert [c.read(c.context+80+4*i) for i in range(56)]==histogram
                    if stage<7 and result<8:
                        c.write(c.context+80+4*(stage*8+result),0)
                    c.write(observation+44,operation);expected[11]=operation
                    cases+=2
                # The actual capture-start branch clears the full appended
                # statistics/diagnostic prefix, retaining cache/policy state.
                for at in range(0,904,4):c.write(c.context+at,0xa5000000+at)
                c.write(c.context,1);c.write(c.context+708,1)
                c.write(c.context+664,c.region)
                c.write(c.context+668,resident[4]['_kui_native_ra_reader_ops'][0])
                saved=[c.read(c.context+at) for at in range(656,904,4)]
                c.body('_kui_native_readahead_aux_dispatch',[6,0,c.region,0],hardware=False)
                assert c.r[0]==1 and c.read(c.context)==1
                assert not any(c.read(c.context+at) for at in range(4,656,4))
                assert [c.read(c.context+at) for at in range(656,904,4)]==saved
                cases+=1
    repair=repair_diagnostic_profile(aux);repair_cases=0
    if repair:
        for bank in range(2):
            for blocked in (False,True):
                for loan,unrepaired in ((0,0),(1,1),(2,2),(0xabcd1234,255)):
                    c=SnapshotCPU(resident,aux,3,bank,blocked)
                    c.write(c.region+0x2f4,loan)
                    c.write(c.region+0x4ac,unrepaired,1)
                    packed=(loan&0xffff)|(unrepaired<<16)
                    c.body('_snapshot_pre',[4,c.region,0x123456])
                    assert c.read(c.context+752+48)==packed
                    c.body('_snapshot_failure',[c.region,6])
                    assert c.read(c.context+360+48)==c.read(c.context+508+48)==packed
                    assert c.read(c.region+0x2f4)==loan and c.read(c.region+0x4ac,1)==unrepaired
                    repair_cases+=2
    cases+=repair_cases
    truth=snapshot_instruction_truth(resident,aux)
    return dict(enabled=True,schema='kui.native.read-ahead.diagnostics.audit.v1',
        actual_linked_snapshot_cases=cases,instruction_truth_cases=truth,
        repair_counter_packed=repair,actual_linked_repair_packed_word_cases=repair_cases,
        public_bytes=656,complete_context_bytes=904,diagnostics_offset=76,
        observation_offset=752,first_offset=360,last_offset=508,record_bytes=148,
        exact_read_only_hardware_sequence=[dict(address=hex(a),bytes=n) for a,n in HW_READS],
        no_rdr_read_or_hardware_write=True,complete_context_and_private_stack_writes_only=True,
        first_record_preserved_last_record_updated=True,out_of_range_histogram_indices_refused=True,
        operation_lba_argument_checked=True,actual_capture_start_clears_complete_public_prefix_only=True,
        actual_diagnostic_c_executed=True)


def snapshot_instruction_truth(resident,aux):
    c=SnapshotCPU(resident,aux,0);cases=0
    for source,borrow,result,flag in ((0,0,0,0),(0,1,MASK,1),(1,0,MASK,1),
            (1,1,MASK-1,1),(0x7fffffff,0,0x80000001,1),
            (0x7fffffff,1,0x80000000,1),(0x80000000,0,0x80000000,1),
            (0x80000000,1,0x7fffffff,1),(MASK,0,1,1),(MASK,1,0,1)):
        c.r[12]=source;c.sr=(c.sr&~1)|borrow;c.plain(0x6dca,0)
        assert c.r[13]==result and c.sr&1==flag;cases+=1
    for shift,result in ((0,0x80000001),(1,2),(31,0x80000000),(32,0x80000001),
            (MASK,0x40000000),(0xffffffe1,1),(0xffffffe0,0),
            (0xffffffdf,0x40000000),(0x80000000,0),(0x7fffffff,0x80000000)):
        for flag in (0,1):
            c.r[12]=shift;c.r[13]=0x80000001;c.sr=(c.sr&~1)|flag;c.plain(0x4dcd,0)
            assert c.r[13]==result and c.sr&1==flag;cases+=1
    for count,values in ((1,range(256)),(2,(0,1,0x7fff,0x8000,0xff80,0xffff))):
        for value in values:
            c.write(c.context,value,count);c.r[12]=c.context;sr=c.sr
            c.plain(0x6dc0+(count-1),0)
            expected=int.from_bytes(value.to_bytes(count,'little'),'little',signed=True)&MASK
            assert c.r[13]==expected and c.sr==sr;cases+=1
    for source,byte,word,swap in ((0x1234abcd,0xcd,0xabcd,0x1234cdab),
            (MASK,255,65535,MASK),(0x80000000,0,0,0x80000000)):
        c.r[12]=source;sr=c.sr
        for op,expected in ((0x6dcc,byte),(0x6dcd,word),(0x6dc8,swap)):
            c.plain(op,0);assert c.r[13]==expected and c.sr==sr;cases+=1
    return cases


def closed_callgraph(text):
    if not text.lstrip().startswith('graph: {'):return False
    depth=0;quoted=escaped=closed=False
    for char in text:
        if closed:
            if not char.isspace():return False
        elif quoted:
            if escaped:escaped=False
            elif char=='\\':escaped=True
            elif char=='"':quoted=False
        elif char=='"':quoted=True
        elif char=='{':depth+=1
        elif char=='}':
            depth-=1
            if depth<0:return False
            if depth==0:closed=True
    return closed and not quoted and depth==0


def private_stack(directory,resident,*,self_test=False):
    aux_reports=list((directory/'ra').rglob('*.ci'))
    low_reports=list(directory.glob('resident-sci.elf.ltrans*.ci'))
    if not low_reports:low_reports=list((directory/'resident/lto').glob('*.ci'))
    require(aux_reports and low_reports,'read-ahead stack proof requires actual auxiliary and LTO callgraphs')
    closure_mutations=0
    for report in aux_reports+low_reports:
        text=report.read_text()
        require(closed_callgraph(text),'incomplete emitted read-ahead stack graph: '+str(report))
        if self_test:
            for invalid in (text[:text.rfind('}')],text+'node: {}'):
                require(not closed_callgraph(invalid),'read-ahead stack proof accepts an incomplete graph')
                closure_mutations+=1
    af,ac=graph.load(aux_reports,qualified=True,scope='aux')
    lf,lc=graph.load(low_reports,qualified=True,scope='low')
    frames={**af,**lf};calls={**ac,**lc};visited=set();sites=set();memo={}
    low_names=typed_ops(resident)['functions']
    def resolve(name,scope):
        options=[n for n in frames if n.startswith(scope+':') and graph.base(n)==graph.base(name)]
        require(options,'unowned read-ahead callback/frame '+scope+':'+name)
        return options
    def depth(name,path=()):
        require(name not in path,'recursive auxiliary call graph: '+' -> '.join(path+(name,)))
        if name in memo:return memo[name]
        if name not in frames:
            if graph.base(name) in ('__udivsi3','___udivsi3'):return 4,[name+'(linked low library:4)']
            raise ValueError('missing emitted auxiliary stack frame '+name)
        visited.add(name);best=(0,[])
        scope=name.split(':',1)[0]
        for target,label in calls.get(name,[]):
            if target=='__indirect_call':
                if scope=='aux':targets=[n for op in low_names for n in resolve(op.removeprefix('_'),'low')]
                else:
                    key=graph.base(name),label.split(':',1)[0]
                    if key in ESCAPES:
                        sites.add((name,label+' [separately audited nonreturning worker/ROM boundary]'))
                        continue
                    require(key in CALLBACKS,'unadmitted low callback on auxiliary stack: '+name+' '+label)
                    targets=[n for op in CALLBACKS[key] for n in resolve(op,'low')]
                sites.add((name,label))
            else:
                targets=[target] if target in frames else resolve(target,scope)
            for callee in targets:
                v=depth(callee,path+(name,))
                if v[0]>best[0]:best=v
        memo[name]=(frames[name]+best[0],[name+'('+str(frames[name])+')']+best[1]);return memo[name]
    roots={}
    for name in ('kui_native_readahead_aux_init','kui_native_readahead_aux_dispatch'):
        options=resolve(name,'aux');require(len(options)==1,'duplicate auxiliary root')
        count,path=depth(options[0]);roots[name]=dict(bytes=count,path=path)
    worst=max(r['bytes'] for r in roots.values());total=worst+4+64+16+32
    require(total<=4096,'read-ahead private stack exceeds existing4KiB guards/topgap/margin')
    return dict(available_bytes=4096,worst_c_bytes=worst,library_margin_bytes=4,
        explicit_margin_bytes=64,guard_bytes=16,top_gap_bytes=32,total_bytes=total,
        roots=roots,typed_low_indirect_sites=len(sites),missing_or_recursive_callbacks_rejected=True,
        all_emitted_callgraphs_closed=True,callgraph_closure_mutation_rejections=closure_mutations)

def check(directory,*,self_test=False):
    directory=Path(directory)
    resident=read_elf(directory/'resident-sci.elf');stage=read_elf(directory/'stage.elf')
    worker=read_elf(directory/'native-worker.elf')
    mark=resident[4].get(MARK+'resident')
    companions=[stage[4].get(MARK+'stage'),worker[4].get(MARK+'worker')]
    if not mark:
        require(not any(companions),'read-ahead stage/worker without resident')
        return {'enabled':False}
    require(__debug__,'read-ahead instruction model requires Python assertions')
    require(mark==companions[0]==companions[1]==(1,0,0,0xfff1),'read-ahead paired marker mismatch')
    aux=read_elf(directory/'native-ra-aux.elf')
    require(aux[4].get(MARK+'auxiliary')==(1,0,0,0xfff1),'read-ahead auxiliary marker mismatch')
    diagnostic=diagnostic_profile(aux)
    pio=pio_profile(aux)
    pio_pair=pio_pair_profile(aux)
    diagnostic_worker=worker[4].get(DIAGNOSTIC_MARK+'worker')
    require(bool(diagnostic_worker)==diagnostic and (not diagnostic or
            diagnostic_worker==(1,0,0,0xfff1)),'read-ahead diagnostic worker/auxiliary markers differ')
    pio_worker=worker[4].get(PIO_MARK+'worker')
    require(bool(pio_worker)==pio and (not pio or pio_worker==(1,0,0,0xfff1)),
            'read-ahead PIO worker/auxiliary markers differ')
    pair_worker=worker[4].get(PIO_PAIR_MARK+'worker')
    require(bool(pair_worker)==pio_pair and (not pio_pair or pair_worker==(1,0,0,0xfff1)),
            'read-ahead PIO pair worker/auxiliary markers differ')
    structural=structural_profile(resident)
    dual_worker=worker[4].get('__kui_native_dual_readers_worker')
    dual_stage=stage[4].get('__kui_native_dual_readers_stage')
    dual=dual_worker is not None or dual_stage is not None
    if dual:
        require(dual_worker==dual_stage==(1,0,0,0xfff1) and structural and pio_pair and
                stage[4].get('__kui_native_sdk_sci_structural_stage')==(1,0,0,0xfff1),
                'dual-reader stage/worker require paired structural PIO architecture')
    from check_native_controller_failure_witness import witness_profile,check as check_witness
    witness=witness_profile(worker)
    require(not witness or dual,'controller witness profile requires the complete dual architecture')
    witness_execution=check_witness(directory,self_test=self_test) if witness else {'enabled':False}
    low_pins=STRUCTURAL_LOW_PINS if structural else LOW_PINS
    require(low_pins,'read-ahead low reviewed functions are not pinned')
    geometry=image_contract(directory,aux,resident)
    code_at,code=pinned_code(aux)
    insns=instruction_audit('native-read-ahead',disassemble(directory/'native-ra-aux.elf'))
    pinned_functions(resident,low_pins,'read-ahead low binding')
    require(low_pins,'read-ahead low reviewed functions are not pinned')
    loan_pin=pinned_loan_boundary(resident)
    loan=loan_cases(resident)
    snapshots=diagnostic_snapshots(resident,aux)
    producer={'enabled':False}
    if pio:
        from check_vmu_native_readahead_pio import pio_cases
        producer=pio_cases(resident,aux,self_test=self_test)
    gateway=gateway_cases(resident,aux);stack=private_stack(directory,resident,self_test=self_test)
    bindings=worker_bindings(resident,worker,diagnostic=diagnostic,pio=pio,pio_pair=pio_pair,dual=dual,witness=witness)
    display=worker_diagnostic_display(worker) if diagnostic else {'enabled':False}
    from check_vmu_native_readahead_stage import check_stage
    cold=check_stage(stage,resident,aux,self_test=self_test,worker=worker)
    dual_execution={'enabled':False}
    if dual:
        from check_native_dual_readers import check as check_dual
        dual_execution=check_dual(directory,self_test=self_test,cold_binder_proof=cold,
                                  witness_proof=witness_execution)
    mutations=bindings_mutations=0
    if self_test:
        for section in aux[3]:
            if section[1]!=1 or not section[2]&2 or section[2]&1 or not code_at<=section[3]<code_at+len(code):continue
            for i in range(section[5]):
                changed=bytearray(aux[0]);changed[section[4]+i]^=1
                try:pinned_code((bytes(changed),*aux[1:]))
                except ValueError:mutations+=1
                else:raise ValueError('read-ahead auditor accepted changed immutable code/data')
        bindings_mutations+=byte_mutations(resident,[(n,2) for n in low_pins],
            lambda image:pinned_functions(image,low_pins,'read-ahead low binding'))
        bindings_mutations+=byte_mutations(resident,[('_kui_native_ra_reader_ops',1),('_kui_native_ra_reader_ops_entry',1)],typed_ops)
        from check_native_game_startup_profile import game_startup_profile
        worker_pins=GAME_STARTUP_WORKER_PINS if game_startup_profile(worker,resident) else DUAL_WITNESS_WORKER_PINS if witness else DUAL_WORKER_PINS if dual else PIO_PAIR_WORKER_PINS if pio_pair else PIO_WORKER_PINS if pio else \
            DIAGNOSTIC_WORKER_PINS if diagnostic else WORKER_PINS
        from check_native_followup_profile import worker_pins as followup_worker_pins
        worker_pins=followup_worker_pins(worker,'read_ahead',worker_pins)
        bindings_mutations+=byte_mutations(worker,[(n,2) for n in worker_pins],
            lambda image:pinned_functions(image,worker_pins,'read-ahead worker binding'))
        if diagnostic:
            bindings_mutations+=byte_mutations(worker,[(n,1) for n in ('_field.2','_count.1','_label.0')],
                worker_diagnostic_display)
        section=resident[3][resident[4]['_kui_vmu_observe_sd_lease'][3]]
        for i in range(section[5]):
            changed=bytearray(resident[0]);changed[section[4]+i]^=1
            try:pinned_loan_boundary((bytes(changed),*resident[1:]))
            except ValueError:bindings_mutations+=1
            else:raise ValueError('read-ahead auditor accepted a changed QUIESCE boundary')
    return dict(enabled=True,schema='kui.native.read-ahead.audit.v1',paired_markers=True,
        diagnostic_capture=diagnostic,repair_diagnostic_capture=repair_diagnostic_profile(aux),
        pio_candidate=pio,pio_pair_candidate=pio_pair,pio_producer=producer,
        structural_sdk_route=structural,dual_reader_candidate=dual,dual_reader=dual_execution,
        controller_failure_witness=witness_execution,
        artifact_sha256=dict(resident=sha(resident[0]),worker=sha(worker[0]),
            stage=sha(stage[0]),auxiliary=sha(aux[0])),
        immutable_code_pin={'address':hex(code_at),'bytes':len(code),'sha256':sha(code)},
        immutable_low_pins=low_pins,loan_quiesce_pin=loan_pin,loan_quiesce=loan,
        diagnostics=snapshots,diagnostic_display=display,
        geometry=geometry,gateway=gateway,private_stack=stack,
        cold_binder=cold,worker_binding=bindings,linked_integer_instructions=insns,
        code_byte_mutation_rejections=mutations,typed_binding_mutation_rejections=bindings_mutations,
        console_boot_verified=False,console_performance_verified=False)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
    p.add_argument('--self-test',action='store_true');a=p.parse_args()
    try:r=check(a.directory,self_test=a.self_test)
    except (ValueError,AssertionError,KeyError,OSError,struct.error) as e:p.exit(2,'Read-ahead audit refused: '+str(e)+'\n')
    print(json.dumps(r,indent=2))

if __name__=='__main__':main()
