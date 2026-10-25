#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked structural SDK cold binder with a writeback SH model.

The cold publisher and every branch, store, alias readback and OCBP execute
from the supplied SH ELF. Only memcpy/memcmp and the two independently tested
pure admission helpers are typed C ABI substitutes. This proves publication
and exact/fallback routing, not the helper's structural decoder or hardware
performance. The older exact-owner proof and its immutable pins stay separate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from check_vmu_native_sdk_sci_route import (BinderStop,ColdBinderCPU,COOKIE,GUARD,
    MASK,OWNER_CONSTANTS,SDK_VBR,assert_c_abi,require)
from check_vmu_native_worker_gateway import read_elf,symbol_bytes

STRUCTURAL='__kui_native_sdk_sci_structural_'
EXACT='_kui_native_sdk_sci_route_admit'
DISCOVER='_kui_native_sdk_sci_route_discover'
LOW=0x8c004000
STACK_TOP=0x8c007d00
STAGE=0x8ce00000
LEASE_BYTES=0x60000
VBR_SPAN=0x660
ABSOLUTE_ONE=(1,0,0,0xfff1)


class StructuralColdCPU(ColdBinderCPU):
    """The existing linked SH/cache machine with both typed admission ABIs."""
    def __init__(self,stage,resident,*,vbr=SDK_VBR,previous=None,common=None,
                 exact=1,discover=1,**kwargs):
        super().__init__(stage,resident,**kwargs)
        self.api[stage[4][DISCOVER][0]]=DISCOVER
        self.targets=set(self.api)
        self.vbr=vbr
        self.control['vbr']=vbr
        self.eri_slot=(vbr+0x25c)&MASK
        self.rxi_slot=(vbr+0x260)&MASK
        self.previous=self.owner_constants['default'] if previous is None else previous
        self.common=self.owner_constants['common'] if common is None else common
        self.admit_result=exact
        self.discover_result=discover
        self.admission_sr=None
        self.captured_sr=None
        self.plan_out=None
        self.helper_calls=[]
        self.callback_readbacks=[]
        self.flush_events=[]
        self.cookie_store_checked=False
        # Invalid VBR cases must be rejected before any table dereference.
        # Do not initialize arbitrary pointer arguments merely to test them.
        if 0x8c000000<=vbr<=0x8cfff900 and not vbr&255:
            self.put32(self.eri_slot,self.previous)
            self.put32(self.rxi_slot,self.previous)
            self.put32(vbr+0x258,0x17a5c3e9)
            self.put32(vbr+0x264,0x9ec35a71)
        self.initial=self.snapshot()
        self.writes=[]

    def read(self,address,count=4):
        value=super().read(address,count)
        if hasattr(self,'callback_readbacks') and address in (
                self.eri_slot|0x20000000,self.rxi_slot|0x20000000):
            self.callback_readbacks.append((self.executing,address,value))
        return value

    def write(self,address,value,count=4):
        if hasattr(self,'cookie_store_checked') and address==self.region+0x530 and value==COOKIE:
            callbacks={at:value for _,at,value in self.callback_readbacks}
            retained={at:value for _,at,value in self.readbacks}
            assert callbacks=={self.eri_slot|0x20000000:self.callback,
                               self.rxi_slot|0x20000000:self.callback}
            assert retained[(self.region+0x534)|0x20000000]==self.previous
            assert retained[(self.region+0x538)|0x20000000]==self.vbr
            # These reads inspect physical RAM directly; they do not add
            # synthetic P2 readback evidence to the linked instruction trace.
            for at,expected in ((self.eri_slot,self.callback),(self.rxi_slot,self.callback),
                                (self.region+0x534,self.previous),(self.region+0x538,self.vbr)):
                actual=int.from_bytes(self.ram_bytes(self.offset(at,4),4),'little')
                assert actual==expected,('cookie before completed publication',hex(at))
            self.cookie_store_checked=True
        super().write(address,value,count)

    def plain(self,op,pc):
        if op&0xf0ff==0x0002:self.captured_sr=self.sr # actual STC SR,Rn
        if op&0xf0ff==0x00a3:self.flush_events.append((pc,self.r[op>>8&15]))
        return super().plain(op,pc)

    def callee(self):
        name=self.api[self.pc]
        if name not in (EXACT,DISCOVER):
            return super().callee()
        self.apis.append(name)
        self.helper_calls.append(name)
        continuation=self.pr
        assert self.r[4:7]==[0x8c010000,self.owner_constants['bytes'],0x8c010000]
        if name==EXACT:
            assert self.admission_sr is None,'exact admission called more than once'
            assert self.r[7]==self.stage[4]['_native_driver'][0]+72
            assert bytes(self.read(self.r[7]+i,1) for i in range(32)).hex()==self.owner_constants['digest']
            args=[self.read(self.r[15]+4*i) for i in range(6)]
            vbr,sr,callback,reader,context,out=args
            self.admission_sr=sr
            assert sr==self.captured_sr and sr&~1==self.sr&~1
            result=self.admit_result
        else:
            assert self.helper_calls==[EXACT,DISCOVER]
            assert self.admit_result==0,'structural discovery follows only exact result zero'
            args=[self.read(self.r[15]+4*i) for i in range(5)]
            vbr=self.r[7]
            sr,callback,reader,context,out=args
            assert sr==self.admission_sr,'fallback receives the same captured SR'
            result=self.discover_result
        assert (vbr,callback,reader,context)==(
            self.vbr,self.callback,self.stage[4]['_native_read'][0],0)
        assert not out&3 and self.r[15]+4*len(args)<=out and out+32<=self.initial[0][15]
        if self.plan_out is not None:assert out==self.plan_out
        self.plan_out=out
        plan=(1,32,self.vbr,self.common,self.eri_slot,self.rxi_slot,self.callback,self.previous)
        if result!=1:plan=(0,)*8
        elif hasattr(self,'plan_mutation'):
            word,value=self.plan_mutation
            plan=list(plan);plan[word]=value
        for i,value in enumerate(plan):self.write(out+4*i,value)
        # Deliberately clobber every volatile integer register and PR/T.
        # Callee-saved registers, banks and protected controls stay unchanged.
        for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd+len(self.apis))&MASK
        self.mach^=0xaaaaaaaa;self.macl^=0x55555555
        self.r[0]=result&MASK;self.sr^=1;self.pr^=0x13572468;self.pc=continuation

    def publication_writes(self):
        watched=(self.eri_slot,self.rxi_slot,self.region+0x534,self.region+0x538,self.region+0x530)
        return [address for _,address,_,_ in self.writes if address in watched]

    def run_bind(self):
        self.run_symbol('_native_sdk_sci_route_bind')
        assert self.ready and self.cookie_store_checked
        assert_c_abi(self,self.initial)
        assert self.helper_calls==([EXACT,DISCOVER] if self.admit_result==0 else [EXACT])
        values=((self.eri_slot,self.callback),(self.rxi_slot,self.callback),
                (self.region+0x534,self.previous),(self.region+0x538,self.vbr),
                (self.region+0x530,COOKIE),(self.region+0x53c,0xa5c39e17),
                (self.vbr+0x258,0x17a5c3e9),(self.vbr+0x264,0x9ec35a71))
        # Capture actual binder readback evidence before post-run assertions.
        callbacks={address for _,address,_ in self.callback_readbacks}
        retained={address for _,address,_ in self.readbacks}
        assert callbacks=={self.eri_slot|0x20000000,self.rxi_slot|0x20000000}
        assert {((self.region+offset)|0x20000000) for offset in (0x530,0x534,0x538)}<=retained
        for address,expected in values:assert self.read(address|0x20000000)==expected
        assert self.publication_writes()==[
            self.region+0x534,self.region+0x538,self.eri_slot,self.rxi_slot,self.region+0x530]
        # A later cache eviction cannot resurrect the pre-publication cookie
        # or corrupt adjacent words from a partially maintained cache line.
        for at,(line,dirty) in tuple(self.cache.items()):
            if dirty:self.ram.update((at+i,b) for i,b in enumerate(line))
        self.cache.clear()
        for address,expected in values:assert self.read(address|0x20000000)==expected
        return self


def expect_refusal(cpu,detail,*,helpers=None,label='context'):
    try:cpu.run_bind()
    except BinderStop as stop:
        assert (stop.phase,stop.detail)==(10,detail),(label,stop)
        assert not cpu.flushes and not cpu.ready and not cpu.publication_writes(),label
        if helpers is not None:assert cpu.helper_calls==helpers,(label,cpu.helper_calls)
    else:raise ValueError('structural cold binder accepted malformed '+label)


def descriptor_contract(resident):
    """Keep the unchanged immutable 20-byte entry+36 descriptor explicit."""
    callback=resident[4]['_kui_retail_sdk_sci_entry'][0]
    descriptor,body,section=symbol_bytes(resident,'_kui_native_sdk_sci_route_exports',1)
    cell,count,kind,owner=resident[4]['_kui_retail_async_region_storage']
    require(not section[2]&1 and body==struct.pack('<5I',0x4b535231,1,20,callback,cell),
            'structural cold descriptor differs from unchanged SDK runtime ABI')
    cell_section=resident[3][owner]
    require(count==4 and kind==1 and cell_section[1]==8 and cell_section[2]&7==3 and
            resident[4]['__retail_resident_binary_end'][0]<=cell<=GUARD-4,
            'structural cold descriptor cell is not owned low BSS')
    entry,word,entry_section=symbol_bytes(resident,'_kui_native_sdk_sci_route_entry',1)
    require(entry==resident[1][4]+36 and not entry_section[2]&1 and
            word==struct.pack('<I',descriptor),'structural entry+36 descriptor binding differs')
    return dict(bytes=20,entry_offset=36,immutable=True,region_storage=hex(cell),callback=hex(callback))


def check(stage,resident,self_test=False):
    require(__debug__,'structural cold instruction model requires Python assertions')
    for image,role in ((stage,'stage'),(resident,'resident')):
        require(image[4].get(STRUCTURAL+role)==ABSOLUTE_ONE,
                'structural cold proof requires the absolute-one '+role+' marker')
    require(stage[4].get('__kui_native_sdk_sci_multi_owner_stage')==ABSOLUTE_ONE,
            'structural cold stage lacks its unchanged exact-owner companion')
    for name in (EXACT,DISCOVER,'_native_sdk_sci_route_bind'):
        address,body,section=symbol_bytes(stage,name,2)
        require(section[2]&4 and STAGE<=address<address+len(body)<=0x8cfe0000,
                'structural cold function escapes the temporary stage: '+name)
    descriptor=descriptor_contract(resident)
    stage_end=stage[4]['__retail_stage_bss_end'][0]
    require(STAGE<stage_end<=0x8cfe0000,'structural cold stage extent differs')
    exact_positive=fallback_positive=negative_context=negative_plan=negative_address=0
    routing_cases=mutations=0
    exact_by_owner={}
    # Both original identities retain their exact success path. Vary the
    # physical/P1/P2 payload argument, stream idle/open state, and cache mode.
    for owner in OWNER_CONSTANTS:
        count=0
        for cache in (False,True):
            for payload in (0x0c400000,0x8c400000,0xac400000):
                for stream in (0,1):
                    StructuralColdCPU(stage,resident,cache=cache,payload=payload,stream=stream,
                                      owner_name=owner,discover=-1).run_bind()
                    exact_positive+=1;count+=1
        exact_by_owner[owner]=count
    # Relocated tables, including the dynamic bound endpoints and every
    # nonoverlapping side of the lease/low/stage reservations. U0 is retained
    # raw for common/default callback pointers while table accesses remain P1.
    relocated=tuple(dict.fromkeys((0x8c000000,0x8c003900,STACK_TOP,0x8c02a000,
        0x8c3ff900,0x8c460000,0x8cdff900,(stage_end+255)&~255,0x8cfff900)))
    for cache in (False,True):
        for vbr in relocated:
            for alias in (0,0x80000000):
                StructuralColdCPU(stage,resident,cache=cache,vbr=vbr,exact=0,
                    previous=0x0c120800|alias,common=0x0c120b00|alias).run_bind()
                fallback_positive+=1
        for edge in ('code_end','limit_minus4'):
            c=StructuralColdCPU(stage,resident,cache=cache,vbr=0x8c02a000,exact=0)
            c.patch_descriptor(4,c.low_end if edge=='code_end' else GUARD-4)
            c.run_bind();fallback_positive+=1
        # Neither -1 nor any unexpected exact result is an unsupported owner.
        for exact,discover,detail,helpers in ((-1,1,7,[EXACT]),(2,1,7,[EXACT]),
                (0,0,6,[EXACT,DISCOVER]),(0,-1,7,[EXACT,DISCOVER]),
                (0,2,7,[EXACT,DISCOVER])):
            expect_refusal(StructuralColdCPU(stage,resident,cache=cache,exact=exact,discover=discover),
                           detail,helpers=helpers,label='admission result routing')
            routing_cases+=1
        # Descriptor and retained-state checks precede either pure helper.
        cases=(('short_blob',1),('short_descriptor',1),('descriptor_alignment',2),
            ('descriptor_before_code',2),('descriptor_beyond_blob',2),
            ('bad_magic',3),('bad_version',3),('bad_size',3),('bad_callback',3),
            ('callback_before_code',3),('callback_end',3),('callback_p2',3),
            ('before_code_end',3),('at_limit',3),('misaligned_cell',3),('p2_cell',3),
            ('standard_limit',3),('live_mismatch',3),('cell_mismatch',4),
            ('control_status',4),('control_lease',4),('uninitialized',5),
            ('pending',5),('executing',5),('active',5),('in_irq',5),('hooked',5),
            ('release_armed',5),('release_depth',5),('loan',5),('stream_busy',5),
            ('port_fault',5),('opened_mismatch',5),('preexisting_cookie',5),
            ('preexisting_default',5),('preexisting_vbr',5))
        for case,detail in cases:
            c=StructuralColdCPU(stage,resident,cache=cache)
            descriptor_pointer=c.blob+36
            if case=='short_blob':c.put32(stage[4]['_resident_bytes'][0],39)
            elif case=='short_descriptor':c.put32(stage[4]['_resident_bytes'][0],19)
            elif case=='descriptor_alignment':c.put32(descriptor_pointer,c.descriptor+1)
            elif case=='descriptor_before_code':c.put32(descriptor_pointer,LOW+36)
            elif case=='descriptor_beyond_blob':c.put32(descriptor_pointer,c.low_end-16)
            elif case=='bad_magic':c.patch_descriptor(0,0)
            elif case=='bad_version':c.patch_descriptor(1,2)
            elif case=='bad_size':c.patch_descriptor(2,24)
            elif case=='bad_callback':c.patch_descriptor(3,c.callback|1)
            elif case=='callback_before_code':c.patch_descriptor(3,LOW+38)
            elif case=='callback_end':c.patch_descriptor(3,c.low_end)
            elif case=='callback_p2':c.patch_descriptor(3,c.callback|0x20000000)
            elif case=='before_code_end':c.patch_descriptor(4,c.low_end-4)
            elif case=='at_limit':c.patch_descriptor(4,GUARD)
            elif case=='misaligned_cell':c.patch_descriptor(4,c.cell+1)
            elif case=='p2_cell':c.patch_descriptor(4,c.cell|0x20000000)
            elif case=='standard_limit':c.put32(stage[4]['_resident_limit'][0],0x8c007800)
            elif case=='live_mismatch':c.patch_descriptor(1,2,immutable=False)
            elif case=='cell_mismatch':c.put32(c.cell,c.region+32)
            elif case=='control_status':c.put32(resident[4]['_kui_vmu_observe_boot_control'][0]+8,3)
            elif case=='control_lease':c.put32(resident[4]['_kui_vmu_observe_boot_control'][0]+12,0x0c500000)
            else:
                offsets={'uninitialized':0x378,'pending':0x370,'executing':0x374,
                    'active':0x2bc,'in_irq':0x2cc,'hooked':0x2d0,'release_armed':0x248,
                    'release_depth':0x24c,'loan':0x2f4,'stream_busy':0x470,
                    'port_fault':0x4ec,'opened_mismatch':0x2c4,'preexisting_cookie':0x530,
                    'preexisting_default':0x534,'preexisting_vbr':0x538}
                c.put_bytes(c.region+offsets[case],bytes((1,)) if case=='port_fault' else
                    struct.pack('<I',0 if case=='uninitialized' else 2 if case=='stream_busy' else 1))
            expect_refusal(c,detail,helpers=[],label=case);negative_context+=1
        # Canonical-P1/alignment/full-span guards run before both helpers.
        addresses=(0,0x0c02a000,0xac02a000,0x8c02a001,0x8c02a080,0x8bffff00,
            0x8cfffa00,0x8d000000,0x8c003a00,LOW,GUARD&~255,
            0x8c007c00,0x8c3ffa00,0x8c3fff00,0x8c400000,0x8c45ff00,
            0x8cdffa00,STAGE,(stage_end-1)&~255)
        for vbr in addresses:
            expect_refusal(StructuralColdCPU(stage,resident,cache=cache,vbr=vbr,exact=0),
                           7,helpers=[],label='VBR '+hex(vbr));negative_address+=1
        # The caller checks every shared field it subsequently trusts for
        # publication; common/default identity is the pure helper's contract.
        for exact in (0,1):
            for word,value in ((0,0),(0,2),(1,0),(1,28),(2,SDK_VBR+0x100),
                    (4,SDK_VBR+0x260),(5,SDK_VBR+0x264),(6,0),(6,0xac007458)):
                c=StructuralColdCPU(stage,resident,cache=cache,exact=exact)
                c.plan_mutation=(word,value)
                expect_refusal(c,7,helpers=[EXACT,DISCOVER] if exact==0 else [EXACT],
                               label='shared plan word '+str(word));negative_plan+=1
    if self_test:
        address,body,section=symbol_bytes(stage,'_native_sdk_sci_route_bind',2)
        ocbp=[at for at in range(0,len(body),2)
              if struct.unpack_from('<H',body,at)[0]&0xf0ff==0x00a3]
        require(len(ocbp)==4,'structural cold publication OCBP count differs')
        for exact in (0,1):
            reference=StructuralColdCPU(stage,resident,exact=exact,
                vbr=0x8c02a000 if exact==0 else SDK_VBR).run_bind()
            require(len(reference.flush_events)==4,'structural cold executed OCBP count differs')
            require([target&0x1fffffe0 for _,target in reference.flush_events]==[
                (reference.region+0x534)&0x1fffffe0,reference.eri_slot&0x1fffffe0,
                reference.rxi_slot&0x1fffffe0,(reference.region+0x530)&0x1fffffe0],
                'structural cold publication flush order differs')
            cookie_pc=reference.flush_events[-1][0]
            for at in ocbp:
                changed=bytearray(stage[0]);start=section[4]+address-section[3]
                struct.pack_into('<H',changed,start+at,0x0009)
                c=StructuralColdCPU((bytes(changed),*stage[1:]),resident,exact=exact,
                    vbr=0x8c02a000 if exact==0 else SDK_VBR)
                cookie=address+at==cookie_pc
                try:c.run_bind()
                except BinderStop as stop:
                    assert (stop.phase,stop.detail)==(10,9 if cookie else 8)
                    assert not c.ready
                    assert (c.region+0x530 in c.publication_writes())==cookie
                    mutations+=1
                else:raise ValueError('structural cold binder accepted an omitted cache publication')
    return dict(schema='kui.native.sdk-sci.structural-cold.audit.v1',enabled=True,
        actual_linked_cold_binder_executed=True,structural_profile=True,
        artifact_sha256={'stage':hashlib.sha256(stage[0]).hexdigest(),
                         'resident':hashlib.sha256(resident[0]).hexdigest()},
        immutable_runtime_descriptor=descriptor,shared_plan_bytes=32,
        exact_positive_cases=exact_positive,exact_positive_cases_by_owner=exact_by_owner,
        structural_fallback_positive_cases=fallback_positive,
        relocated_vbrs=[hex(vbr) for vbr in relocated],raw_u0_default_and_common_preserved=True,
        admission_result_routing_cases=routing_cases,only_exact_zero_attempts_discovery=True,
        exact_minus_one_never_falls_back=True,malformed_descriptor_or_context_refusals=negative_context,
        malformed_vbr_or_overlap_refusals=negative_address,malformed_shared_plan_refusals=negative_plan,
        omitted_ocbp_mutation_rejections=mutations,dynamic_vbr_canonical_p1_aligned256=True,
        dynamic_vbr_bounds=['0x8c000000','0x8cfff900'],live_vbr_span_bytes=VBR_SPAN,
        lease_bytes_excluded=LEASE_BYTES,lease_header_bytes_excluded=32,
        low_resident_and_service_stack_excluded=True,temporary_stage_excluded=True,
        stage_end=hex(stage_end),selected_async_bound_checked=True,
        p1_writeback_cache_and_p2_ram_distinct=True,full_cacheline_writeback_and_sentinels_preserved=True,
        original_default_and_vbr_published_first=True,both_callbacks_read_back_before_cookie=True,
        cookie_published_last=True,callee_saved_integer_bank_and_control_state_preserved=True,
        pure_admission_call='typed ABI substitutes; exact and structural pure helpers separately host-tested',
        memcpy_memcmp='faithful bounded ABI substitutes',console_performance_verified=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--resident',type=Path,required=True)
    parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args()
    print(json.dumps(check(read_elf(args.stage),read_elf(args.resident),args.self_test),indent=2,sort_keys=True))


if __name__=='__main__':main()
