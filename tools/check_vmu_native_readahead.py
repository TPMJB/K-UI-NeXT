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
LOW_PINS={
    '_deliver_work':(456,'73ed6d63b6e67b122d11d0185a009d2c28f81face7e99aeda639660b6eb61aeb'),
    '_deliver':(60,'d32fd5a483cdffee78867c2239be72d693e1d1cc46f62e9b0ae2cda2f5e3eb46'),
}
WORKER_PINS={
    '_reader_readahead_exports':(160,'7408f9b61bad644e7a5c20dc0766138fb36f4ee24a8ef3cd751e93d4e316489e'),
    '_reader_readahead_visit':(56,'2af85f7d6a6a88765d1e8cf645b45683e794c2bf08ebca7a5c1bb6666a78186a'),
    '_reader_cooperative_service':(32,'d5c8c4caf5c8bf445a7f0d5ab500ee6f1e7ef46cbdfdf2c46070c0058b683ea4'),
}
LOAN_PIN=(744,'a06ecbd795204545b4df1ca6f5efc3883a0a81b4e0391b088a12388203ff376c')
GATEWAY_MEMORY={}

def require(value,message):
    if not value:raise ValueError(message)

def sha(value):return hashlib.sha256(value).hexdigest()

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
    require(CODE_PIN is not None and (len(body),sha(body))==CODE_PIN,
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
    at,count,kind,owner=resident[4]['_kui_vmu_observe_sd_lease']
    sec=resident[3][owner]
    require(kind==2 and sec[1]==1 and sec[2]&7==6 and at==sec[3] and
            count==752 and sec[5]==LOAN_PIN[0] and LOW<=at<at+sec[5]<=GUARD,
            'read-ahead QUIESCE loan boundary shape changed')
    require(resident[4]['__vmu_sd_lease_begin'][0]==at and
            resident[4]['__vmu_sd_lease_end'][0]==at+sec[5],
            'read-ahead QUIESCE loan section bounds changed')
    body=resident[0][sec[4]:sec[4]+sec[5]]
    require(sha(body)==LOAN_PIN[1],'read-ahead QUIESCE loan instructions changed')
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
    require(context[1]>=76 and not any(initialized_at(aux,context[0],context[1])),
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
        context=hex(context[0]),context_bytes=context[1],private_stack_bottom=hex(STACK_BOTTOM),
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
    def __init__(self,resident,worker,payload,event,variant):
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
        if variant=='context_high':self.write(self.aux_header+20,self.aux_header+4096-72)
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

def worker_bindings(resident,worker):
    pins={}
    for name,(count,digest) in WORKER_PINS.items():
        address,body,section=symbol_bytes(worker,name,2)
        require((len(body),sha(body))==(count,digest) and section[2]&7==6,
                'read-ahead worker binding instructions changed: '+name)
        pins[name]=(address,count,digest)
    cases=0
    variants=('valid','magic','version','bytes','image_low','image_high','entry_low','entry_high',
              'bound_entry','bound_context','context_low','context_high')
    for payload in (0x0c400000,0x8c400000,0xac400000,0x0cfa0000):
        for event in (2,6):
            for variant in variants:
                c=WorkerCPU(resident,worker,payload,event,variant)
                c.run_symbol('_reader_readahead_visit')
                assert c.calls==int(variant=='valid')
                before=c.initial
                assert c.r[8:]==before[0][8:] and c.other==before[1] and c.pr==before[2]
                assert c.control==before[10] and c.r[15]==before[0][15]
                cases+=1
    c=WorkerCPU(resident,worker,0x8c400000,2,'valid')
    c.run_symbol('_reader_cooperative_service');assert c.calls==1
    require(len(c.durable_reads)==2,'worker heartbeat must check both durable writer busy fields')
    for address in c.durable_reads:
        c=WorkerCPU(resident,worker,0x8c400000,2,'valid');c.write(address,1)
        c.run_symbol('_reader_cooperative_service');assert c.calls==0;cases+=1
    return dict(actual_worker_binding_cases=cases,immutable_function_pins=pins,
        all_three_payload_aliases=True,highest_owned_lease_checked=True,
        malformed_headers_and_contexts_refused=True,both_durable_writer_busy_fields_refused=True,
        heartbeat_and_capture_start_arguments_checked=True,
        c_auxiliary_target='ABI substitute; complete auxiliary gateway proved separately')

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


def private_stack(directory,resident):
    aux_reports=list((directory/'ra').rglob('*.ci'))
    low_reports=list(directory.glob('resident-sci.elf.ltrans*.ci'))
    if not low_reports:low_reports=list((directory/'resident/lto').glob('*.ci'))
    require(aux_reports and low_reports,'read-ahead stack proof requires actual auxiliary and LTO callgraphs')
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
        roots=roots,typed_low_indirect_sites=len(sites),missing_or_recursive_callbacks_rejected=True)

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
    geometry=image_contract(directory,aux,resident)
    code_at,code=pinned_code(aux)
    insns=instruction_audit('native-read-ahead',disassemble(directory/'native-ra-aux.elf'))
    pinned_functions(resident,LOW_PINS,'read-ahead low binding')
    require(LOW_PINS,'read-ahead low reviewed functions are not pinned')
    loan_pin=pinned_loan_boundary(resident)
    loan=loan_cases(resident)
    gateway=gateway_cases(resident,aux);stack=private_stack(directory,resident)
    bindings=worker_bindings(resident,worker)
    from check_vmu_native_readahead_stage import check_stage
    cold=check_stage(stage,resident,aux,self_test=self_test)
    mutations=bindings_mutations=0
    if self_test:
        for section in aux[3]:
            if section[1]!=1 or not section[2]&2 or section[2]&1 or not code_at<=section[3]<code_at+len(code):continue
            for i in range(section[5]):
                changed=bytearray(aux[0]);changed[section[4]+i]^=1
                try:pinned_code((bytes(changed),*aux[1:]))
                except ValueError:mutations+=1
                else:raise ValueError('read-ahead auditor accepted changed immutable code/data')
        bindings_mutations+=byte_mutations(resident,[(n,2) for n in LOW_PINS],
            lambda image:pinned_functions(image,LOW_PINS,'read-ahead low binding'))
        bindings_mutations+=byte_mutations(resident,[('_kui_native_ra_reader_ops',1),('_kui_native_ra_reader_ops_entry',1)],typed_ops)
        bindings_mutations+=byte_mutations(worker,[(n,2) for n in WORKER_PINS],
            lambda image:pinned_functions(image,WORKER_PINS,'read-ahead worker binding'))
        section=resident[3][resident[4]['_kui_vmu_observe_sd_lease'][3]]
        for i in range(section[5]):
            changed=bytearray(resident[0]);changed[section[4]+i]^=1
            try:pinned_loan_boundary((bytes(changed),*resident[1:]))
            except ValueError:bindings_mutations+=1
            else:raise ValueError('read-ahead auditor accepted a changed QUIESCE boundary')
    return dict(enabled=True,schema='kui.native.read-ahead.audit.v1',paired_markers=True,
        artifact_sha256=dict(resident=sha(resident[0]),worker=sha(worker[0]),
            stage=sha(stage[0]),auxiliary=sha(aux[0])),
        immutable_code_pin={'address':hex(code_at),'bytes':len(code),'sha256':sha(code)},
        immutable_low_pins=LOW_PINS,loan_quiesce_pin=loan_pin,loan_quiesce=loan,
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
