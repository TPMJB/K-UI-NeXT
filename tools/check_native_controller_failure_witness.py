#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the failure-only native controller and identity witness paths.

Actual linked worker instructions implement every witnessed predicate/store.
The unchanged deep transfer-context validator is a typed boolean boundary;
its earlier 0x200xx witness is explicitly retained on failure. RAM is coherent,
and the model establishes neither hardware timing nor console compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from check_vmu_native_sdk_sci_route import SDKCPU,RETURN,assert_c_abi
from test_vmu_native_heap_execution import MASK,signed
from sh4_delay_slots import validate_delay_slot
from test_native_dual_readers import object_section
from profile_vmu_native_release_sh import ReleaseSH
from check_native_general_lean_profile import worker_profile as lean_worker_profile

WITNESS_MARK='__kui_native_controller_failure_witness_worker'
TIMER_MARK='__kui_native_owned_timer_callbacks_worker'
SCHEMA='kui.native.controller-failure-witness.audit.v1'
PINS={
    '_failure_witness.isra.0':(16,'77c6eb9f5b1c088a76dce8a70932b385dbeeed53466bb8dfd4400408b6ddf30e'),
    '_poll_lease_valid':(168,'8d228d4661c181ddf3d38f44dc6e482077b0bf622f36ccc0f2cc10f5128e9946'),
    '_controller_context_valid':(1372,'a4c4a0960bcd2cb9e143a83c79b63b8a22d8bc77f97c2b8c8bff407f1b26c8b3'),
    '_kui_vmu_native_passive_complete':(3144,'73567edf285f627f641e475d63c0b14fa7b195cedfa7124fd51c6211c108ba31'),
    '_kui_vmu_native_passive_submit':(1420,'4f5296368fdbc9c5af432566142822de0a68e113c524b4b40d054a069141b69a'),
    '_read_value':(180,'2bf8aacc78250e0aabc16c8037ae642b34e52ecf935cddc2b8915ad36b024a4c'),
    '_readonly_rx.constprop.0':(76,'8ae835afcc6563c32e1756bbe4979d5caf8c18cb54be8755beb897429d5300a9'),
    '_stack_valid':(116,'c835d4fc2544c16b17ab322828d34d9b3a071b3c4c3184df40488db336c13cc5'),
    '_installed_hooks_valid':(128,'50bd5e002877635399c93866b7f7e9052436d3a9d552e77dc646946d507a72da'),
    '_normalize_heap_dispatch':(276,'8827906101a825940b4e3bceb2a13f413538081a267b4d74af8fe15e2f2bf4f2'),
    '_kui_vmu_native_terminal_dispatch_span.constprop.0':(128,'064b673e141fbd0c394b6740ef7ca832e4d86f384550df0bb87306e57c2385eb'),
    '_kui_vmu_native_terminal_dispatch_tagged.constprop.0':(100,'cd6e6a11b173562a3b675f6a83cb16c67f07bb15285df93da4a008a50f43c9b6'),
    '_kui_vmu_native_patch_encode':(168,'1a1547eb012f9b28948e575d486931f28192c4ff232663ff73d460940dd56291'),
    '_read_heap_raw':(104,'ba6f179b61b78a8bf700d46bac6237e4287d61bbe9cde913a8768ce99e732035'),
    '_memcpy':(72,'cf04fa8199a588139e7f567443f39a51dce313766b1ca5a9e0085b6de93b4634'),
    '_kui_vmu_native_trace_point':(1132,'1ac58feb1f5a4a20d6cbf2eeba7cb8fb6f7f0cbe3ae6c6dd948e5aa0e2dedd19'),
    '_reader_capture_region':(100,'a2ba9caef7fb729bab16d02c1d16ea468018ab128cc2ba8143802eac16bad827'),
    '_reader_cooperative_service':(32,'9a04c43d4b1fc25d6b832a808b55f5bd28df0e70fd24134aa060af0bcb7c4e68'),
    '_reader_readahead_exports':(200,'6a753b4f52fd9aad30c334241ea8c0de92cefcac3c3a85009af4645b260d67ef'),
    '_reader_readahead_visit':(56,'c44759200e43937665b2987281e995b4f39675f1e05180371aad4078cc02dbf3'),
}
DATA_PINS={
    '_kui_vmu_native_terminal_dispatch_words':(96,'5304ca217467bd183ac4ec020dd7793ad95b46229c4caea4ed3ea8d3e947c82b'),
    '_kui_vmu_native_terminal_dispatch_entries':(16,'8477f830f8adb39068e9fe7d4f16e7729d6939c9d7cdaa5da013e519325a68ac'),
}
# Separate complete pins for the bounded application-timer policy. The new
# callback helper and its FILE-owned RAM/range callees execute in the model;
# older workers retain every preceding pin and the original zero-only guard.
OWNED_TIMER_PINS={**PINS,
    '_failure_witness.isra.0':(16,'9752e82987994a19f8731a90d3b4535ad753e1b665a1a545be8978d4ee8e2280'),
    '_poll_lease_valid':(168,'cd8437d849d88e5898f3400170a7b41dbb56184c9698f1d2e39ec001a184e078'),
    '_controller_context_valid':(1336,'021568cc10a09ba02fb597727ef6ac4e45731fd38cd48b5b43f307b1e38b05ae'),
    '_kui_vmu_native_passive_complete':(3144,'183a6bc48d8bfb47294ca817b483c0581c9e0d79f8a49d7ca67246855df7f760'),
    '_kui_vmu_native_passive_submit':(1428,'ba425d588b269391806da90277783d073b7c8f20352dbd01b8689959c081698d'),
    '_stack_valid':(116,'a3bffefb714c3fd498142d7497a5604494489efe5ff57b29bd0df0c14763fc7f'),
    '_installed_hooks_valid':(128,'cdc12c06950385246a00cbf644b4ae91dc248fc63de3b35786acb0fdd53f9511'),
    '_normalize_heap_dispatch':(276,'b10f6faa7720058741d0704568998085e9f11a3f33d0a8222a07746f32f18092'),
    '_kui_vmu_native_terminal_dispatch_span.constprop.0':(128,'697c229576a1d5b73015e2f734462614c2d843c139ca64c0dae338a324f20cf2'),
    '_kui_vmu_native_terminal_dispatch_tagged.constprop.0':(100,'7934714b0642d723206a950440cc8322f96fd50f4f363ac1cfec3439e5d7c04d'),
    '_kui_vmu_native_patch_encode':(168,'f6c5c4c520a4c04d874fcfba785b5ff344bd37905055ee2523b96819babe6a0a'),
    '_kui_vmu_native_trace_point':(1132,'789805da528225a5aed8c814e94e3f7c70ec5de6d1dce0e19b50de5a6e9a9c5c'),
    '_reader_capture_region':(100,'f46dc671c3f853a40f735778e6e4d841b869e7a84d2ba92be85aca4e1e3870ed'),
    '_reader_cooperative_service':(32,'932a8c6f2a8ffa5ffb5ead01a2a1fbfb8093d39f4c1836deabc9403ef027f505'),
    '_reader_readahead_exports':(200,'13ca433d369112a4b3184b17ec96e025a76e142e537d65c8d3bba2c6e9ce4d5e'),
    '_kui_vmu_native_timer_callback_owned':(92,'98fb3d043e7ef477bc88e178136cb90e366de05a5f3d95a8c80f2090bd8c7602'),
    '_timer_runtime_ram':(104,'4fb7919a6f74ec1c9aa3f434e446b352f7961eb7c565580f0c4e2e52a3eff415'),
    '_timer_runtime_in':(26,'a0ff5d4d27ccbf7b073b8bc6f0e6b26659e6b729763f7b02d49aa124ea023824'),
}
LAYOUT_FIELDS={
    'exit_bytes':'sizeof(struct kui_vmu_native_exit)',
    **{'config.'+name:'offsetof(struct kui_vmu_native_passive_config,'+name+')' for name in
       ('driver','driver.load_address','driver.image_bytes','driver.manager_cell','driver.manager_initial','driver.resume','driver.consumer','runtime.rx_slots',
        'lease.owner.dispatch_cell','lease.owner.dispatch_table','low_terminal','exits.version','exits.bytes',
        'exits.family','exits.load','exits.image_bytes','exits.count','exits.entries','exits.terminal_dispatch.table')},
    **{'live.'+name:'offsetof(struct kui_vmu_native_live,'+name+')' for name in
       ('self','ready','input_count','tx','rx','gun','timer1','timer2','input_address','input_value')},
    **{'overlay.'+name:'offsetof(struct kui_vmu_readonly_overlay,'+name+')' for name in
       ('admitted','pending','stats.advertisements','stats.completed')},
    **{'durable.'+name:'offsetof(struct kui_vmu_durable,'+name+')' for name in ('ready','faulted','commits')},
    **{'lifetime.'+name:'offsetof(struct kui_vmu_native_lifetime_plan,'+name+')' for name in
       ('self','ready','count','identity')},
}
MEMORY={}
MODELED_FUNCTIONS=set()


def worker_geometry(worker):
    """Keep historical model bounds separate from the exact lean General ABI."""
    lean=lean_worker_profile(worker)
    from native_runtime_layout import layout_for
    span=layout_for(worker[4]).WORKER_BYTES if lean else 0x20000
    return dict(lean=lean,begin=0x0c100000,end=0x0c100000+span,
                stack_begin=0x0c100000+span-0x1000)


def require(ok,message):
    if not ok:raise ValueError(message)


def sha(data):return hashlib.sha256(data).hexdigest()


def witness_profile(worker):
    marker=worker[4].get(WITNESS_MARK)
    terminal=worker[4].get('__kui_native_terminal_dispatch_worker')
    require((marker is None)==(terminal is None),'witness and terminal-dispatch markers must be paired')
    if marker is None:
        require(TIMER_MARK not in worker[4],'owned timer marker lacks its witnessed worker')
        return False
    require(marker==(1,0,0,0xfff1) and
            terminal==(1,0,0,0xfff1) and
            worker[4].get('__kui_native_dual_readers_worker')==(1,0,0,0xfff1),
            'controller witness marker requires exact absolute one and the dual worker')
    return True


def owned_timer_profile(worker):
    marker=worker[4].get(TIMER_MARK)
    if marker is None:return False
    require(marker==(1,0,0,0xfff1) and witness_profile(worker),
            'owned timer callbacks require exact absolute one and the witnessed dual worker')
    return True


def code_pins(worker):
    selected=OWNED_TIMER_PINS if owned_timer_profile(worker) else PINS
    from check_native_followup_profile import worker_pins
    selected=worker_pins(worker,'witness',selected)
    require(selected,'controller witness timer profile lacks independently reviewed pins')
    return selected


def timer_symbol_aliases(worker):
    """Bind duplicate local helper names by their actual ELF FILE owner."""
    if not owned_timer_profile(worker):return worker
    found={};symbol=struct.Struct('<IIIBBH')
    for section in worker[3]:
        if section[1]!=2:continue
        require(section[9]==symbol.size and section[5]%symbol.size==0,
                'timer helper symbol table shape differs')
        strings=worker[3][section[6]]
        names=worker[0][strings[4]:strings[4]+strings[5]];owner_file=None
        for at in range(section[4],section[4]+section[5],symbol.size):
            name,address,count,info,_,owner=symbol.unpack_from(worker[0],at)
            name=names[name:].split(b'\0',1)[0].decode('ascii')
            if info&15==4:owner_file=name
            if owner_file=='vmu_native_runtime.c' and name in ('_ram','_in') and info&15==2:
                require(info>>4==0 and name not in found,
                        'timer helper local source binding is not unique')
                found[name]=(address,count,2,owner)
    require(set(found)=={'_ram','_in'},'timer helper lacks both source-owned actual callees')
    symbols=dict(worker[4])
    for name,record in found.items():symbols['_timer_runtime'+name]=record
    image=(*worker[:4],symbols)
    _,body,_=symbol_bytes(image,'_kui_vmu_native_timer_callback_owned',2)
    for name in found:
        require(body.count(struct.pack('<I',found[name][0]))==1,
                'timer callback helper does not bind its exact source-owned callee')
    return image


def pinned(worker):
    worker=timer_symbol_aliases(worker)
    selected=code_pins(worker)
    result={}
    for kind,pins in ((2,selected),(1,DATA_PINS)):
        for name,(count,digest) in pins.items():
            address,body,section=symbol_bytes(worker,name,kind)
            require(section[2]&7==(6 if kind==2 else 2) and (len(body),sha(body))==(count,digest),
                    'controller witness instructions/targets changed: '+name)
            result[name]=dict(address=hex(address),bytes=count,sha256=digest)
    return result


def witness_companion(worker,read_ahead_proof):
    proof=read_ahead_proof.get('controller_failure_witness') if isinstance(read_ahead_proof,dict) else None
    require(proof is None or isinstance(proof,dict),'controller witness companion must be a report object')
    present=witness_profile(worker)
    require(present==bool(isinstance(proof,dict) and proof.get('enabled')),
            'controller witness marker and companion proof differ')
    if not present:
        require(proof is None or proof.get('enabled') is False,
                'unmarked worker cannot select a controller witness companion')
        return False
    require(witness_profile(worker) and proof.get('enabled') is True and proof.get('schema')==SCHEMA and
            proof.get('artifact_sha256')==sha(worker[0]),
            'controller witness companion lacks the matching exact linked worker')
    require(proof.get('immutable_pins')==pinned(worker),
            'controller witness companion lacks reviewed actual instruction pins')
    require(proof.get('uncached_owned_objects')==objects(worker) and
            proof.get('model_scope',{}).get('actual_linked_worker_c_executed') is True and
            proof.get('scalar_truth_cases')==1584,
            'controller witness companion lacks actual owned-state and instruction-model proofs')
    coverage=proof.get('predicate_coverage',{})
    timers=owned_timer_profile(worker)
    require(coverage.get('all_controller_reasons')==list(range(1,12)) and
            coverage.get('all_controller_context_reasons')==list(range(0x20,0x27 if timers else 0x26)) and
            coverage.get('identity_first_failure_read_order') is True and
            coverage.get('identity_shape_bitmap') is True and
            coverage.get('resume_then_consumer_first_failure') is True and
            coverage.get('old_transfer_witness_preserved') is True and
            coverage.get('success_only_parent_presence_byte') is True and
            coverage.get('failure_no_owner_writes') is True and
            proof.get('helper_model_cases')==64 and proof.get('controller_model_cases')==(410 if timers else 326) and
            proof.get('identity_model_cases')==72 and proof.get('success_model_cases')==72 and
            proof.get('failure_model_cases')==116,
            'controller witness companion lacks complete actual predicate/store coverage')
    timer_proof=proof.get('owned_timer_callbacks',{})
    require(timer_proof.get('enabled',False)==timers,
            'owned timer marker and actual callback ownership proof differ')
    if timers:
        require(timer_proof.get('cases')==81 and timer_proof.get('accepted_cases')==36 and
                timer_proof.get('rejected_cases')==45 and timer_proof.get('worker_imask')==15 and
                timer_proof.get('exact_owner_executable_halfword_bounds') is True and
                timer_proof.get('u0_p1_p2_callback_and_owner_aliases') is True and
                timer_proof.get('odd_foreign_alias_and_mmio_refused') is True and
                timer_proof.get('loader_heap_and_worker_targets_refused') is True and
                timer_proof.get('callback_target_never_read_or_called') is True and
                timer_proof.get('callback_validation_changes_no_owner_memory_or_sr_control_bits') is True,
                'owned timer companion lacks complete read-only executable-range proof')
    identity=proof.get('identity',{});terminal=proof.get('terminal_identity',{})
    require(identity.get('ordinary_capability_absent_owner_reads')==8 and
            identity.get('ordinary_capability_absent_performs_no_terminal_table_reads') is True and
            terminal.get('enabled') is True and terminal.get('cases')==25 and
            terminal.get('actual_table_and_wrapper_and_terminal_patch_rechecked') is True and
            terminal.get('actual_validation_owner_reads')==18 and
            terminal.get('one_full_validation_per_changed_routing_word') is True and
            terminal.get('invalid_capability_preserves_raw_first_failure') is True,
            'controller witness companion lacks actual tagged routing-read normalization proof')
    return True


def scalar_truth_cases():
    """Cross-check newly needed scalar semantics against the earlier model."""
    count=0
    values=(0,1,31,32,0x7fffffff,0x80000000,0xfffffffe,MASK)
    shifts=(0,1,31,32,33,63,0xffffffff,0xffffffe1,0xffffffe0,0xffffffdf,0xffffffc0)
    ops=(0x4010,0x4028,0x4029,0x0327,0x031a,0x632a,0x632c,0x402d,0x402c)
    for op in ops:
        for value in values:
            for shift in shifts:
                for bit in (0,1):
                    a=object.__new__(WitnessCPU);b=object.__new__(ReleaseSH)
                    for cpu in (a,b):
                        cpu.r=[0]*16;cpu.r[0]=value;cpu.r[2]=shift;cpu.r[3]=value
                        cpu.sr=0x400001f0|bit;cpu.mach=0x87654321;cpu.macl=0x12345678
                        cpu.instructions=0;cpu.executing=0
                    a.plain(op,0x8c100000);b.plain(op,0x8c100000)
                    assert (a.r,a.sr,a.mach,a.macl)==(b.r,b.sr,b.mach,b.macl)
                    count+=1
    return count


def console_layout():
    compiler=shutil.which('sh-elf-gcc')
    require(compiler is not None,'controller witness layout proof requires sh-elf-gcc')
    with tempfile.TemporaryDirectory(prefix='kui-controller-witness-layout-') as temp:
        source=Path(temp)/'layout.c';binary=Path(temp)/'layout.o'
        source.write_text('#include <stddef.h>\n#include "kui/vmu_native_passive.h"\n'+
            '#include "kui/vmu_native_live.h"\n#include "kui/vmu_readonly_overlay.h"\n'+
            '#include "kui/vmu_durable.h"\n#include "kui/vmu_native_lifetime.h"\n'+
            'const uint32_t layout[] __attribute__((section(".rodata.witness_layout"),used))={'+
            ','.join(LAYOUT_FIELDS.values())+'};\n')
        subprocess.run([compiler,'-std=c11','-Os','-m4-single','-ml','-ffreestanding',
                        '-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1','-I'+str(ROOT/'include'),
                        '-c',str(source),'-o',str(binary)],check=True,capture_output=True)
        raw=object_section(binary,'.rodata.witness_layout')
    require(len(raw)==4*len(LAYOUT_FIELDS),'controller console layout word count changed')
    return dict(zip(LAYOUT_FIELDS,struct.unpack('<'+'I'*len(LAYOUT_FIELDS),raw)))


class Failure(Exception):pass
class PrefixComplete(Exception):pass


class WitnessCPU(SDKCPU):
    """Actual immutable worker text with only one typed transfer boundary."""
    def __init__(self,resident,worker,layout,seed=0,bank=0):
        worker=timer_symbol_aliases(worker)
        super().__init__(resident,seed=seed,mask=15,bank=bank)
        # Mutant byte objects are short lived; object IDs can be reused while
        # this cache persists. Bind the cached RAM image to exact bytes so a
        # later mutant never executes a previous worker's instruction image.
        key=(sha(resident[0]),sha(worker[0]))
        if key not in MEMORY:
            memory=bytearray(self.memory)
            for kind,offset,address,_,count,_,_,_ in worker[2]:
                if kind==1 and count:
                    at=self.offset(address,count);memory[at:at+count]=worker[0][offset:offset+count]
            MEMORY[key]=bytes(memory)
        self.memory=MEMORY[key];self.symbols=dict(worker[4]);self.layout=layout
        self.geometry=worker_geometry(worker)
        self.worker=worker;self.targets=set();self.transfer_ok=True;self.transfer_calls=[]
        self.code_reads=set();self.identity_reads=[];self.stop_at_mdstar=False
        self.forbidden_reads=set()
        self.owner_reads=[];self.executed_entry_counts={}
        self.function_entries={address:name for name,(address,_,kind,_) in self.symbols.items() if kind==2}
        self.state=self.addr('_state');self.config=self.addr('_config');self.pair=self.addr('_controller_pair')
        self.live=self.addr('_live_plan');self.overlay_at=self.addr('_overlay');self.durable=self.addr('_durable_card')
        self.lifetime=self.addr('_lifetime_plan');self.frame=0x8c900000
        self.source=0x0c800000;self.manager=0x8c990000;self.manager_cell=0x8c980000
        self.rx_base=0x0c820000;self.r[15]=0x80000000|(self.geometry['end']-32);self.pr=RETURN
        self.write(self.state,1);self.write(self.state+4,1)
        self.write(self.state+8,30);self.write(self.state+12,29)
        self.write(self.state+20,self.source)
        for i in range(16):self.write((0x80000000|self.geometry['stack_begin'])+4*i,0x4b535431)
        self.write(self.frame+68,self.sr)
        self.owner_code=0x8c010000;self.owner_bytes=0x20000
        self.write(self.config+layout['config.driver.load_address'],self.owner_code)
        self.write(self.config+layout['config.driver.image_bytes'],self.owner_bytes)
        self.write(self.config+layout['config.driver.manager_cell'],self.manager_cell)
        self.write(self.config+layout['config.driver.manager_initial'],self.manager)
        self.write(self.manager_cell,self.manager)
        self.write(self.addr('_hook_witness_ready'),0x4e485031)
        self.write(self.addr('_hook_witness_count'),1)
        self.write(self.addr('_hook_witness'),0x8c950000)
        self.write(self.addr('_hook_witness')+4,0x13572468)
        self.write(self.addr('_hook_witness')+8,MASK)
        self.write(0x8c950000,0x13572468)
        self.write(self.live+layout['live.self'],self.live)
        self.write(self.live+layout['live.ready'],0x4e4c5031)
        self.write(self.live+layout['live.tx'],0xac800000)
        self.write(self.live+layout['live.rx'],0xac820000)
        for offset,value in ((0xfc4,0xac800000),(0xfc8,0xac820000),
                             (0x10a8,0xac800000),(0x10ac,0xac803000)):
            self.write(self.manager+offset,value)
        for i,name in enumerate(('gun','timer1','timer2')):
            self.write(self.live+layout['live.'+name],0x8c970000+4*i)
        self.write(self.overlay_at+layout['overlay.admitted'],1)
        self.write(self.durable+layout['durable.ready'],1)
        self.identity_addresses=[0x8c960000+4*i for i in range(8)]
        self.write(self.lifetime+layout['lifetime.self'],self.lifetime)
        self.write(self.lifetime+layout['lifetime.ready'],0x4e4c4631)
        for i,address in enumerate(self.identity_addresses):
            expected=0x13572460+i
            self.write(self.lifetime+layout['lifetime.identity']+8*i,address)
            self.write(self.lifetime+layout['lifetime.identity']+8*i+4,expected)
            self.write(address,expected)
        self.writes=[];self.identity_reads=[]

    def addr(self,name):return self.symbols[name][0]

    def read(self,address,count=4):
        assert address not in getattr(self,'forbidden_reads',()),'query error unexpectedly read a function word'
        if getattr(self,'stop_at_mdstar',False) and address==0xa05f6c04:raise PrefixComplete()
        if hasattr(self,'identity_addresses') and count==4 and address in self.identity_addresses:
            self.identity_reads.append(address)
        if hasattr(self,'owner_reads') and count==4 and not self.owned_worker_address(address):
            self.owner_reads.append(address)
        return super().read(address,count)

    def plain(self,op,pc):
        if op>>8==0xc8:self.flag(not(self.r[0]&(op&255)));return
        if op>>8==0x80:
            self.write((self.r[op>>4&15]+(op&15))&MASK,self.r[0],1);return
        if op>>8==0x81:
            self.write((self.r[op>>4&15]+2*(op&15))&MASK,self.r[0],2);return
        if op&0xf00f==0x0005:
            self.write((self.r[0]+self.r[op>>8&15])&MASK,self.r[op>>4&15],2);return
        if op&0xf0ff==0x4010:
            n=op>>8&15;self.r[n]=(self.r[n]-1)&MASK;self.flag(self.r[n]==0);return
        if op&0xf0ff==0x4028:
            n=op>>8&15;self.r[n]=(self.r[n]<<16)&MASK;return
        if op&0xf0ff==0x4029:
            n=op>>8&15;self.r[n]>>=16;return
        if op&0xf00f==0x0007:
            self.macl=(self.r[op>>8&15]*self.r[op>>4&15])&MASK;return
        if op&0xf00f in (0x200e,0x200f):
            a,b=self.r[op>>8&15]&65535,self.r[op>>4&15]&65535
            self.macl=((signed(a,16)*signed(b,16)) if op&15==15 else a*b)&MASK;return
        if op&0xf0ff in (0x000a,0x001a):
            self.r[op>>8&15]=self.mach if op&0xf0ff==0x000a else self.macl;return
        if op&0xf00f in (0x400c,0x400d):
            n,m=op>>8&15,op>>4&15;shift=signed(self.r[m],32)
            if shift>=0:self.r[n]=(self.r[n]<<(shift&31))&MASK
            else:
                count=(-shift)&31
                value=signed(self.r[n],32) if op&15==12 else self.r[n]
                self.r[n]=((value>>count) if count else (-1 if value<0 else 0))&MASK
            return
        if op&0xf00f==0x000c:
            self.r[op>>8&15]=signed(self.read((self.r[0]+self.r[op>>4&15])&MASK,1),8)&MASK;return
        if op&0xf00f==0x6005:
            n,m=op>>8&15,op>>4&15;value=signed(self.read(self.r[m],2),16)&MASK
            if n!=m:self.r[m]=(self.r[m]+2)&MASK
            self.r[n]=value;return
        if op&0xf00f==0x600a:
            n,m=op>>8&15,op>>4&15;value=self.r[m]+(self.sr&1)
            self.r[n]=(-value)&MASK;self.flag(value!=0);return
        if op&0xf00f==0x600b:
            self.r[op>>8&15]=(-self.r[op>>4&15])&MASK;return
        if op&0xf00f in (0x600c,0x600d):
            self.r[op>>8&15]=self.r[op>>4&15]&(255 if op&15==12 else 65535);return
        if op&0xf00f==0x6009:
            value=self.r[op>>4&15];self.r[op>>8&15]=((value>>16)|(value<<16))&MASK;return
        if op&0xf00f==0x6008:
            value=self.r[op>>4&15]
            self.r[op>>8&15]=(value&0xffff0000)|((value&255)<<8)|((value>>8)&255);return
        if op&0xf00f==0x3003:
            self.flag(signed(self.r[op>>8&15],32)>=signed(self.r[op>>4&15],32));return
        super().plain(op,pc)

    def execute(self,name,args=(),limit=20000):
        self.r[4:4+len(args)]=list(args);self.pr=RETURN;self.pc=self.addr(name)
        steps=0
        for _ in range(limit):
            if self.pc==RETURN:return steps
            if self.pc==self.addr('_fail'):
                assert self.r[4] in (3,0x47),'unexpected earlier/later terminal failure'
                raise Failure(self.r[4])
            if self.pc==self.addr('_transfer_context_valid'):
                self.transfer_calls.append(self.r[4:8].copy())
                continuation=self.pr
                for i in range(8):self.r[i]=(0x913579bd+i*0x13579bd)&MASK
                self.r[0]=int(self.transfer_ok);self.sr^=1;self.pc=continuation;continue
            pc=self.pc;self.executing=pc;self.code_reads.add(pc)
            require(any(section[1]==1 and section[2]&6==6 and
                        section[3]<=pc<pc+2<=section[3]+section[5] for section in self.worker[3]),
                    'controller witness branched outside owned immutable worker code')
            if pc in self.function_entries:
                name=self.function_entries[pc]
                self.executed_entry_counts[name]=self.executed_entry_counts.get(name,0)+1
                MODELED_FUNCTIONS.add(name)
            steps+=1
            op=self.read(pc,2);n=op>>8&15;target=None;delay=False
            if op>>12 in (0xa,0xb):
                target=pc+4+2*signed(op&4095,12);delay=True
                if op>>12==0xb:self.pr=pc+4
            elif op&0xf0ff in (0x400b,0x402b):
                target=self.r[n];delay=True
                if op&0xf0ff==0x400b:self.pr=pc+4
            elif op==0xb:target=self.pr;delay=True
            elif op>>8 in (0x89,0x8b,0x8d,0x8f):
                taken=bool(self.sr&1)==(op>>8 in (0x89,0x8d))
                delay=op>>8 in (0x8d,0x8f)
                target=pc+4+2*signed(op&255,8) if taken else pc+(4 if delay else 2)
            if target is None:self.plain(op,pc);self.pc=pc+2;continue
            if delay:
                slot=self.read(pc+2,2);validate_delay_slot(slot,pc+2)
                self.executing=pc+2;self.code_reads.add(pc+2);self.plain(slot,pc+2)
                steps+=1
            self.pc=target&MASK
        raise AssertionError('controller witness actual-instruction bound exceeded')

    def witness(self):
        return tuple(self.read(self.state+offset) for offset in (76,64,68,72))

    def packet(self,ports=(0,),commands=None,bank=0):
        commands=commands or [9]*len(ports);self.ports=ports
        source=[];self.reply_headers=[];self.rx=[]
        for i,(port,command) in enumerate(zip(ports,commands)):
            rx=self.rx_base+port*0x400;self.rx.append(rx)
            endpoint=0x2000|(port<<14)|(port<<22)
            header=endpoint|command|(0x01000000 if command==9 else 0)
            words=int(command==9)
            control=(int(i==len(ports)-1)<<31)|(port<<16)|words
            source.extend([control,rx,header]+([0x01000000] if words else []))
            self.write(self.pair+156+8*i,rx);self.write(self.pair+160+8*i,header)
            code=5 if command==1 else 6 if command==2 else 8
            length=28 if command==1 else 48 if command==2 else 3
            response=(length<<24)|0x00200000|(port<<14)|(port<<22)|code
            self.reply_headers.append(response);self.write(rx|0xa0000000,response)
            self.write((rx+4)|0xa0000000,0x01000000)
            slot=bank*24+port*6
            self.write(self.manager+0xfe8+4*slot,rx|0xa0000000)
            self.write(self.config+self.layout['config.runtime.rx_slots']+4*slot,rx|0xa0000000)
        for i,value in enumerate(source):
            self.write(self.source+4*i,value);self.write(self.pair+28+4*i,value)
        self.write(self.pair,1);self.write(self.pair+4,bank);self.write(self.pair+8,self.source)
        self.write(self.pair+12,len(source));self.write(self.pair+16,len(ports))
        self.write(self.pair+24,ports.index(0));self.write(self.state+24,4*len(source))
        self.write(self.state+28,len(ports))
        self.writes=[];self.identity_reads=[]

    def assert_no_owner_writes(self,allowed=()):
        for _,address,_,count in self.writes:
            if self.owned_worker_address(address,count):continue
            assert any(address==at and count==width for at,width in allowed),\
                ('unexpected owner/MMIO write',hex(address),count)

    def owned_worker_address(self,address,count=1):
        physical=address&0x1fffffff
        return self.geometry['begin']<=physical<physical+count<=self.geometry['end']

    def owned_stack_address(self,address,count=1):
        physical=address&0x1fffffff
        return self.geometry['stack_begin']<=physical<physical+count<=self.geometry['end']


def helper_cases(resident,worker,layout):
    count=0
    for index in (0,1,3,7,23,32,56,255):
        for reason in (1,3,6,8,10,0x20,0x25,0xff):
            cpu=WitnessCPU(resident,worker,layout,index,reason&1)
            values=(0x40000|(index<<8)|reason,0xac123400+4*index,0xdead0000|index,0x12340000|reason)
            before=cpu.snapshot();cpu.writes=[]
            cpu.execute('_failure_witness.isra.0',values)
            assert cpu.witness()==values
            assert_c_abi(cpu,before)
            assert [(a,v,n) for _,a,v,n in cpu.writes if not cpu.owned_stack_address(a,n)]==\
                [(cpu.state+64,values[1],4),(cpu.state+68,values[2],4),
                 (cpu.state+72,values[3],4),(cpu.state+76,values[0],4)]
            cpu.assert_no_owner_writes();count+=1
    return count


def observed_case(cpu,entry,expected=None,terminal=None,args=None):
    cpu.writes=[];cpu.identity_reads=[];cpu.owner_reads=[]
    before=cpu.snapshot()
    try:cpu.execute(entry,[cpu.frame,1] if args is None else args)
    except Failure as error:
        assert terminal is not None and error.args==(terminal,),('wrong terminal',error.args,terminal)
    else:
        assert terminal is None,'actual linked predicate accepted a refused fixture'
        assert_c_abi(cpu,before)
    if expected is not None:assert cpu.witness()==expected,(entry,cpu.witness(),expected)
    cpu.assert_no_owner_writes()


TIMER_BAD_CALLBACKS=(0x8c00fffe,0x8c030000,0x8c02ffff,0x8c010001,
    0x4c010002,0xec010002,0xa05f6c00,0x8c004000,0x8c100000,
    0x8c400000,0x8ce00000,MASK)
TIMER_VALID_CALLBACKS=(0,*(alias|offset for alias in (0,0x80000000,0xa0000000)
    for offset in (0x0c010000,0x0c010002,0x0c02fffe)))


def callback_ownership_cases(resident,worker,layout):
    if not owned_timer_profile(worker):return dict(enabled=False)
    cases=accepted=rejected=0
    def run(load,bytes_,callback,expected,*,null_driver=False):
        nonlocal cases,accepted,rejected
        c=WitnessCPU(resident,worker,layout,cases,cases&1)
        c.write(c.config+layout['config.driver.load_address'],load)
        c.write(c.config+layout['config.driver.image_bytes'],bytes_)
        if callback:c.forbidden_reads.add(callback)
        c.writes=[];before=c.snapshot()
        c.execute('_kui_vmu_native_timer_callback_owned',
            [0 if null_driver else c.config+layout['config.driver'],callback])
        assert c.r[0]==int(expected),(hex(load),hex(bytes_),hex(callback),c.r[0],expected)
        assert_c_abi(c,before);c.assert_no_owner_writes()
        assert c.sr&~1==before[3]&~1,'read-only callback ownership helper changed SR control bits'
        cases+=1;accepted+=bool(expected);rejected+=not expected
    for alias in (0,0x80000000,0xa0000000):
        load=alias|0x0c010000
        for callback in TIMER_VALID_CALLBACKS:run(load,0x20000,callback,True)
        for callback in TIMER_BAD_CALLBACKS:run(load,0x20000,callback,False)
    for load,bytes_ in ((0,0),(0x4c010000,0x20000),(0x8c010001,0x20000),
                        (0x8c010000,0),(0x8cfff000,0x2000),(0x8c010000,MASK)):
        for callback in (0,0x8c010000):run(load,bytes_,callback,callback==0)
    for callback in (0,0x8c010000,MASK):run(0x8c010000,0x20000,callback,False,null_driver=True)
    return dict(enabled=True,cases=cases,accepted_cases=accepted,rejected_cases=rejected,
        worker_imask=15,exact_owner_executable_halfword_bounds=True,
        u0_p1_p2_callback_and_owner_aliases=True,odd_foreign_alias_and_mmio_refused=True,
        loader_heap_and_worker_targets_refused=True,callback_target_never_read_or_called=True,
        callback_validation_changes_no_owner_memory_or_sr_control_bits=True)


def context_cases(resident,worker,layout):
    cases=0;coverage=set()
    timers=owned_timer_profile(worker)
    def fixture(bank=0,inputs=24):
        c=WitnessCPU(resident,worker,layout);c.packet((0,1,2,3),[9,1,2,9],bank)
        c.write(c.live+layout['live.input_count'],inputs)
        for i in range(inputs):
            c.write(c.live+layout['live.input_address']+4*i,0x8c970100+4*i)
            c.write(c.live+layout['live.input_value']+4*i,0x24681350+i)
            c.write(0x8c970100+4*i,0x24681350+i)
        return c
    shapes=(('self',0,1),('ready',0,2),('input_count',25,4))
    for name,value,bad in shapes:
        c=fixture();c.write(c.live+layout['live.'+name],value)
        observed_case(c,'_controller_context_valid',(0x40020,c.live,bad,0));assert c.r[0]==0
        coverage.add(0x20);cases+=1
    for offset,value,bad in ((4,2,8),(16,0,0x50),(16,5,32),(24,4,64)):
        c=fixture();c.write(c.pair+offset,value)
        observed_case(c,'_controller_context_valid',(0x40020,c.live,bad,0));assert c.r[0]==0;cases+=1
    for variant in ('read','span','identity'):
        c=fixture()
        if variant=='read':
            c.write(c.config+layout['config.driver.manager_cell'],0)
            expected=(0x40021,0,MASK,c.manager)
        elif variant=='span':
            c.write(c.manager_cell,0x4c990000);expected=(0x40022,c.manager_cell,0x4c990000,c.manager)
        else:
            c.write(c.manager_cell,c.manager+0x2000);expected=(0x40023,c.manager_cell,c.manager+0x2000,c.manager)
        observed_case(c,'_controller_context_valid',expected);assert c.r[0]==0
        coverage.add(expected[0]&255);cases+=1
    compared=[];failed_reads=[]
    for bank in (0,1):
        c=fixture(bank);before=c.snapshot();c.writes=[]
        c.execute('_controller_context_valid',[c.frame,1]);assert c.r[0]==1;assert_c_abi(c,before)
        assert c.transfer_calls==[[c.frame,bank,1,c.source]];c.assert_no_owner_writes();cases+=1
        fields=[(0,c.manager+0xfc4,0xac800000),(1,c.manager+0xfc8,0xac820000),
                (2,c.manager+0x10a8,0xac800000),(3,c.manager+0x10ac,0xac803000)]
        fields.extend((4+i,0x8c970000+4*i,0) for i in range(1 if timers else 3))
        fields.extend((8+i,0x8c970100+4*i,0x24681350+i) for i in range(24))
        fields.extend((32+bank*24+6*port,c.manager+0xfe8+4*(bank*24+6*port),
                       (c.rx_base+0x400*port)|0xa0000000) for port in range(4))
        for index,address,expected in fields:
            c=fixture(bank);value=expected^0x100;c.write(address,value)
            observed_case(c,'_controller_context_valid',(0x40025|(index<<8),address,value,expected))
            assert c.r[0]==0;compared.append(index);coverage.add(0x25);cases+=1
        for index,name in ((4,'gun'),(5,'timer1'),(6,'timer2')):
            c=fixture(bank);c.write(c.live+layout['live.'+name],0)
            expected=c.owner_code if timers and index in (5,6) else 0
            observed_case(c,'_controller_context_valid',(0x40024|(index<<8),0,MASK,expected))
            assert c.r[0]==0;failed_reads.append(index);coverage.add(0x24);cases+=1
        if timers:
            for index in (5,6):
                address=0x8c970000+4*(index-4)
                for value in TIMER_VALID_CALLBACKS:
                    c=fixture(bank);c.write(address,value);before=c.snapshot();c.writes=[]
                    if value:c.forbidden_reads.add(value)
                    c.execute('_controller_context_valid',[c.frame,1]);assert c.r[0]==1
                    assert_c_abi(c,before);c.assert_no_owner_writes();cases+=1
                for value in TIMER_BAD_CALLBACKS:
                    c=fixture(bank);c.write(address,value)
                    observed_case(c,'_controller_context_valid',
                        (0x40026|(index<<8),address,value,c.owner_code))
                    assert c.r[0]==0;coverage.add(0x26);cases+=1
        for i in range(24):
            c=fixture(bank);c.write(c.live+layout['live.input_address']+4*i,0)
            observed_case(c,'_controller_context_valid',(0x40024|((8+i)<<8),0,MASK,0x24681350+i))
            assert c.r[0]==0;failed_reads.append(8+i);coverage.add(0x24);cases+=1
        c=fixture(bank);c.transfer_ok=False
        sentinel=(0x20032,0x8c123400,0x7f,0x80)
        for offset,value in zip((76,64,68,72),sentinel):c.write(c.state+offset,value)
        observed_case(c,'_controller_context_valid',sentinel);assert c.r[0]==0;cases+=1
    assert coverage==set(range(0x20,0x27 if timers else 0x26))
    return dict(cases=cases,reasons=sorted(coverage),compared_field_indices=sorted(set(compared)),
                read_failure_field_indices=sorted(set(failed_reads)),old_transfer_witness_preserved=True)


def identity_cases(resident,worker,layout):
    cases=0;absent_steps=[]
    for seed in range(4):
        for count in (0,64):
            c=WitnessCPU(resident,worker,layout,seed,seed&1)
            c.write(c.lifetime+layout['lifetime.count'],count)
            c.writes=[];c.identity_reads=[];c.owner_reads=[];before=c.snapshot()
            absent_steps.append(c.execute('_poll_lease_valid'));assert c.r[0]==1;assert_c_abi(c,before)
            assert c.identity_reads==c.identity_addresses and c.owner_reads==c.identity_addresses
            c.assert_no_owner_writes();cases+=1
    for seed in range(2):
        for i in range(8):
            c=WitnessCPU(resident,worker,layout,seed,seed&1)
            address=c.identity_addresses[i];value=c.read(address)^1;expected=value^1;c.write(address,value)
            observed_case(c,'_poll_lease_valid',(0x50003|(i<<8),address,value,expected),args=[])
            assert c.r[0]==0 and c.identity_reads==c.identity_addresses[:i+1];cases+=1
    for mask in range(1,8):
        c=WitnessCPU(resident,worker,layout)
        if mask&1:c.write(c.lifetime+layout['lifetime.self'],0)
        if mask&2:c.write(c.lifetime+layout['lifetime.ready'],0)
        if mask&4:c.write(c.lifetime+layout['lifetime.count'],65)
        observed_case(c,'_poll_lease_valid',(0x50001,c.lifetime,mask,0),args=[])
        assert c.r[0]==0 and not c.identity_reads;cases+=1
    for seed in range(4):
        for which in ('valid','resume','consumer','both'):
            c=WitnessCPU(resident,worker,layout,seed,seed&1)
            c.write(c.state+4,0);c.write(c.state+12,30)
            c.write(c.frame+16,c.manager_cell);c.write(c.frame+24,1)
            c.write(c.config+layout['config.driver.resume'],0x8c1d0000)
            c.write(c.config+layout['config.driver.consumer'],0x8c1d0100)
            resume=c.addr('_kui_vmu_native_passive_resume');consumer=c.addr('_kui_vmu_native_passive_consumer')
            c.write(resume,0x8c1d0000+int(which in ('resume','both'))*2)
            c.write(consumer,0x8c1d0100+int(which in ('consumer','both'))*2)
            c.stop_at_mdstar=True;c.writes=[];c.identity_reads=[]
            if which=='valid':
                try:c.execute('_kui_vmu_native_passive_submit',[c.frame])
                except PrefixComplete:pass
                else:raise AssertionError('valid submit prefix did not reach MDSTAR')
                assert c.witness()==(0,0,0,0);c.assert_no_owner_writes()
            else:
                address=resume if which in ('resume','both') else consumer
                expected=0x8c1d0000 if address==resume else 0x8c1d0100
                code=0x50004 if address==resume else 0x50005
                observed_case(c,'_kui_vmu_native_passive_submit',(code,address,expected+2,expected),3,[c.frame])
            assert c.identity_reads==c.identity_addresses;cases+=1
    return dict(cases=cases,first_failure_read_order=True,shape_bitmap=True,
                resume_then_consumer_first_failure=True,
                ordinary_capability_absent_success_instruction_counts=sorted(set(absent_steps)),
                ordinary_capability_absent_owner_reads=8,
                ordinary_capability_absent_performs_no_terminal_table_reads=True,
                compiled_identity_read_failure_path_absent=True,
                read_failure_note='read_heap_proved is an admitted direct load returningtrue in SH; 0x5ii02 is host-only')


def terminal_identity_cases(resident,worker,layout):
    if '__kui_native_terminal_dispatch_worker' not in worker[4]:
        return dict(enabled=False,cases=0)
    require(worker[4]['__kui_native_terminal_dispatch_worker']==(1,0,0,0xfff1),
            'terminal identity worker marker differs')
    common=0x8c960102;table=0x8c960400;default=0x8c960800;terminal=0x8c004100
    entries=(0x26,0x7a,0x80,0x88)
    words=((0x26,0xd4126543),(0x2a,0x0009afe9),(0x7a,0xafc0d417),(0x7e,0xd417e5ff),
           (0x82,0xafbc2f56),(0x86,0xd4167f04),(0x8a,0xafb82f56),(0x8e,0x00007f04))
    patch=(0xd001,0x402b,9,terminal&65535,terminal>>16,9)
    lean=lean_worker_profile(worker)
    if lean:
        require(layout['exit_bytes']==52,'lean witness requires its exact version-two exit shape')
    def fixture():
        c=WitnessCPU(resident,worker,layout)
        fields={'exits.version':2 if lean else 1,'exits.bytes':layout['exit_bytes'],'exits.family':5,
            'exits.load':0x8c950000,'exits.image_bytes':0x20000,'exits.count':3,
            'exits.terminal_dispatch.table':table,'low_terminal':terminal,
            'lease.owner.dispatch_cell':c.identity_addresses[0],'lease.owner.dispatch_table':default}
        for name,value in fields.items():c.write(c.config+layout['config.'+name],value)
        c.write(c.config+layout['config.exits.entries']+8,common)
        if lean:c.write(c.config+layout['config.exits.entries']+12,0)
        c.write(c.identity_addresses[0],table)
        c.write(c.lifetime+layout['lifetime.identity']+4,default)
        for i,offset in enumerate(entries):c.write(table+4*i,common+offset)
        for offset,value in words:c.write(common+offset,value)
        for i,value in enumerate(patch):c.write(common+2*i,value,2)
        return c
    cases=0;success_counts=[];valid_reads=[]
    for current in (table,default):
        c=fixture();c.write(c.identity_addresses[0],current)
        c.writes=[];c.identity_reads=[];c.owner_reads=[];before=c.snapshot()
        success_counts.append(c.execute('_poll_lease_valid'))
        assert c.r[0]==1 and c.witness()==(0,0,0,0);assert_c_abi(c,before)
        assert c.identity_reads==c.identity_addresses;c.assert_no_owner_writes()
        if current==table:
            expected=[c.identity_addresses[0]]+[table+4*i for i in range(4)]+\
                [common+offset for offset,_ in words]+[(common+2*i)&~3 for i in range(6)]+c.identity_addresses[1:]
            assert c.owner_reads==expected,('terminal validation read order differs',c.owner_reads,expected)
            valid_reads=c.owner_reads
        else:assert c.owner_reads==c.identity_addresses
        cases+=1
    damaged=[('table',table+4*i,4) for i in range(4)]+\
        [('wrapper',common+offset,4) for offset,_ in words]+\
        [('patch',common+2*i,2) for i in range(6)]
    for _,address,width in damaged:
        c=fixture();c.write(address,c.read(address,width)^1,width)
        observed_case(c,'_poll_lease_valid',(0x50003,c.identity_addresses[0],table,default),args=[])
        assert c.r[0]==0 and c.identity_reads==c.identity_addresses[:1];cases+=1
    for field,value in (('exits.version',0),('exits.bytes',0),('exits.count',2),
                        ('exits.load',0x4c950000),('exits.image_bytes',0)):
        c=fixture();c.write(c.config+layout['config.'+field],value)
        observed_case(c,'_poll_lease_valid',(0x50003,c.identity_addresses[0],table,default),args=[])
        assert c.r[0]==0 and c.identity_reads==c.identity_addresses[:1];cases+=1
    return dict(enabled=True,cases=cases,configured_success_instruction_counts=success_counts,
        actual_table_and_wrapper_and_terminal_patch_rechecked=True,
        actual_validation_owner_reads=len(valid_reads)-8,
        one_full_validation_per_changed_routing_word=True,
        all_other_identity_words_keep_raw_read_order=True,
        invalid_capability_preserves_raw_first_failure=True,owner_writes=0)


def complete_cases(resident,worker,layout):
    cases=successes=failures=0;coverage=set();fail_indices={}
    def fixture(ports=(0,1,2,3),commands=(9,1,2,9),bank=0):
        c=WitnessCPU(resident,worker,layout);c.packet(ports,commands,bank);return c
    def rejected(c,expected):
        nonlocal cases,failures
        observed_case(c,'_kui_vmu_native_passive_complete',expected,0x47,[c.frame])
        reason=expected[0]&255;coverage.add(reason)
        fail_indices.setdefault(reason,set()).add(expected[0]>>8&255);cases+=1;failures+=1
    for bank in (0,1):
        for count in range(1,5):
            for parent in range(count):
                ports=list(range(1,count));ports.insert(parent,0)
                for command in (1,2,9):
                    commands=[9 if port==0 else command for port in ports]
                    c=fixture(tuple(ports),tuple(commands),bank)
                    c.write(c.durable+layout['durable.commits'],bank)
                    # A physical A1 bitmap is permitted; only A2 conflicts.
                    c.write(c.rx[parent]|0xa0000000,c.reply_headers[parent]|0x10000)
                    c.writes=[];before=c.snapshot();c.execute('_kui_vmu_native_passive_complete',[c.frame])
                    assert_c_abi(c,before)
                    c.assert_no_owner_writes([((c.rx[parent]+2)|0xa0000000,1)])
                    owner=[(a,v,n) for _,a,v,n in c.writes if not c.owned_worker_address(a,n)]
                    assert owner==[((c.rx[parent]+2)|0xa0000000,0x23,1)]
                    assert c.read(c.pair)==c.read(c.state+4)==0 and c.read(c.state+12)==30
                    assert c.read(c.overlay_at+layout['overlay.stats.advertisements'])==1
                    assert c.read(c.overlay_at+layout['overlay.stats.completed'])==1
                    assert c.read(c.state+80)==int(bool(bank)) and c.witness()==(0,0,0,0)
                    assert c.transfer_calls==[[c.frame,bank,1,c.source]]
                    cases+=1;successes+=1
    # All reachable aggregate conditions. The outer dispatcher requires
    # pending!=0 before entering this inlined path, so that bit is unreachable.
    aggregates=[('pair',8,0x0c800004,2),('pair',12,0,0x84),('pair',12,33,0x88),
        ('pair',16,0,0x150),('pair',16,5,0x120),('pair',24,4,64),
        ('state',24,1,0x80),('state',28,3,0x100),
        ('overlay',layout['overlay.pending'],1,0x200),('overlay',layout['overlay.admitted'],0,0x400),
        ('durable',layout['durable.ready'],0,0x800),('durable',layout['durable.faulted'],1,0x1000)]
    for obj,offset,value,bad in aggregates:
        c=fixture();base={'pair':c.pair,'state':c.state,'overlay':c.overlay_at,'durable':c.durable}[obj]
        c.write(base+offset,value);rejected(c,(0x40001,c.pair,bad,0))
    for index in range(32):
        c=fixture();c.write(c.pair+12,32);c.write(c.state+24,128)
        for i in range(32):
            value=0x700+i;c.write(c.source+4*i,value);c.write(c.pair+28+4*i,value)
        c.write(c.source+4*index,0xbad00000|index)
        rejected(c,(0x40003|(index<<8),(c.source+4*index)|0xa0000000,0xbad00000|index,0x700+index))
        c=fixture();source=0x0d000000-4*index
        c.write(c.state+20,source);c.write(c.pair+8,source);c.write(c.pair+12,32);c.write(c.state+24,128)
        for i in range(32):
            c.write(c.pair+28+4*i,0x700+i)
            if i<index:c.write(source+4*i,0x700+i)
        rejected(c,(0x40002|(index<<8),0xad000000,MASK,0x700+index))
    for index in range(4):
        ports=list(range(1,4));ports.insert(index,0)
        commands=[9 if p==0 else 1 for p in ports]
        c=fixture(tuple(ports),tuple(commands));c.write(c.pair+156+8*index,0x0d000000)
        rejected(c,(0x40004|(index<<8),0xad000000,MASK,0))
        c=fixture(tuple(ports),tuple(commands));rx=c.rx[index];old=c.reply_headers[index]
        c.write(rx|0xa0000000,old^1);rejected(c,(0x40006|(index<<8),rx|0xa0000000,old^1,old))
        c=fixture(tuple(ports),tuple(commands));c.write(c.pair+156+8*index,0x0cfffffc)
        c.write(0xacfffffc,c.reply_headers[index])
        rejected(c,(0x40007|(index<<8),0xad000000,MASK,0x01000000))
        c=fixture(tuple(ports),tuple(commands));rx=c.rx[index];c.write((rx+4)|0xa0000000,0)
        rejected(c,(0x40008|(index<<8),(rx+4)|0xa0000000,0,0x01000000))
        c=fixture(tuple(ports),tuple(commands));rx=c.rx[index];c.write(rx|0xa0000000,MASK)
        rejected(c,(0x40009|(index<<8),rx|0xa0000000,MASK,0))
        c=fixture(tuple(ports),tuple(commands));rx=c.rx[index];value=c.reply_headers[index]|0x20000
        c.write(rx|0xa0000000,value)
        rejected(c,(0x4000a|(index<<8),rx|0xa0000000,value,value&~0x20000))
        c=fixture(tuple(ports),tuple(commands));rx=c.rx[index]+4
        c.write(c.pair+156+8*index,rx);c.write(rx|0xa0000000,c.reply_headers[index]);c.write((rx+4)|0xa0000000,0x01000000)
        c.write(c.manager+0xfe8,rx|0xa0000000);c.write(c.config+layout['config.runtime.rx_slots'],rx|0xa0000000)
        rejected(c,(0x4000b|(index<<8),rx,0,1024))
        # Non-parent query errors use their exact port identity and no function.
        for error in (0xfc,0xfd,0xfe):
            ports=[0,2,3];ports.insert(index,1);commands=[9 if p==0 else 1 for p in ports]
            c=fixture(tuple(ports),tuple(commands));i=index;port=1
            expected=0x00200000|(port<<14)|(port<<22)|error
            c.write(c.rx[i]|0xa0000000,expected^0x100)
            rejected(c,(0x40005|(i<<8),c.rx[i]|0xa0000000,expected^0x100,expected))
            c=fixture(tuple(ports),tuple(commands));c.write(c.rx[i]|0xa0000000,expected)
            c.forbidden_reads.add((c.rx[i]+4)|0xa0000000)
            parent=ports.index(0);c.writes=[];before=c.snapshot()
            c.execute('_kui_vmu_native_passive_complete',[c.frame]);assert_c_abi(c,before)
            c.assert_no_owner_writes([((c.rx[parent]+2)|0xa0000000,1)])
            assert c.read((c.rx[parent]+2)|0xa0000000,1)==0x22 and c.witness()==(0,0,0,0)
            cases+=1;successes+=1
    assert coverage==set(range(1,12)),coverage
    return dict(cases=cases,successes=successes,failures=failures,reasons=sorted(coverage),
                failure_indices={str(k):sorted(v) for k,v in sorted(fail_indices.items())},
                aggregate_pending_bit_unreachable_after_outer_dispatch=True,
                success_only_parent_presence_byte=True,failure_no_owner_writes=True)


def objects(worker):
    result={}
    geometry=worker_geometry(worker)
    if geometry['lean']:
        from check_native_general_lean_profile import prepared_objects
        from check_native_compact_background_profile import worker_profile as compact_profile
        prepared_objects(worker)
        overlay_bytes=1452 if compact_profile(worker) else 17172
        selected=(('_state',100),('_controller_pair',188),('_config',2304),
            ('_live_plan',456),('_lifetime_plan',616),('_overlay',overlay_bytes),('_durable_card',4160))
    else:
        selected=(('_state',100),('_controller_pair',188),('_config',2300),
            ('_live_plan',456),('_lifetime_plan',616),('_overlay',0x45a0),('_durable_card',0x1040))
    for name,minimum in selected:
        address,count,kind,owner=worker[4][name];section=worker[3][owner]
        require(kind==1 and section[1]==8 and section[2]&7==3 and
                (count==minimum if geometry['lean'] else count>=minimum) and
                0xac100000<=address<address+count<=(0xa0000000|geometry['stack_begin']),
                'controller witness mutable object escapes its owned uncached BSS: '+name)
        result[name]=dict(address=hex(address),bytes=count)
    return result


def mutations(resident,worker,layout):
    worker=timer_symbol_aliases(worker)
    functional=bytes_rejected=schemas=timer_branches=0
    address,body,section=symbol_bytes(worker,'_failure_witness.isra.0',2)
    offset=section[4]+address-section[3]
    for at,opcode in ((2,0x2152),(4,0x1161),(6,0x1172),(10,0x1143)):
        require(struct.unpack_from('<H',body,at)[0]==opcode,'witness store mutation site changed')
        changed=bytearray(worker[0]);struct.pack_into('<H',changed,offset+at,9)
        trial=(bytes(changed),*worker[1:])
        try:helper_cases(resident,trial,layout)
        except AssertionError:functional+=1
        else:raise ValueError('actual witness model accepted an omitted terminal-row store')
    address,body,section=symbol_bytes(worker,'_kui_vmu_native_passive_complete',2)
    pattern=struct.pack('<I',0xffe0ffff)
    require(body.count(pattern)==1,'controller masked-response functional mutation is not unique')
    changed=bytearray(worker[0]);at=section[4]+address-section[3]+body.index(pattern)
    changed[at:at+4]=struct.pack('<I',MASK);trial=(bytes(changed),*worker[1:])
    try:complete_cases(resident,trial,layout)
    except (AssertionError,Failure):functional+=1
    else:raise ValueError('actual completion model accepted loss of allowed A1 attachment bits')
    if owned_timer_profile(worker):
        typed=timer_symbol_aliases(worker)
        # Invert each actual policy branch while retaining its delay-slot
        # shape: null driver/callback, both RAM admissions, owner/callback
        # aliases, alignment, complete RAM span and lower/upper code bounds.
        branches={
            '_kui_vmu_native_timer_callback_owned':((12,0x8d0e),(18,0x8d0b),(36,0x8f07),(66,0x8df1)),
            '_timer_runtime_ram':((20,0x8f18),(46,0x8b0a),(54,0x8f07),(60,0x8b04)),
            '_timer_runtime_in':((2,0x8f08),(12,0x8d03)),
        }
        for name,sites in branches.items():
            address,body,section=symbol_bytes(typed,name,2);offset=section[4]+address-section[3]
            for at,opcode in sites:
                require(struct.unpack_from('<H',body,at)[0]==opcode,
                        'owned timer policy branch mutation site changed')
                changed=bytearray(worker[0]);struct.pack_into('<H',changed,offset+at,opcode^0x0200)
                try:callback_ownership_cases(resident,(bytes(changed),*worker[1:]),layout)
                except (AssertionError,ValueError,Failure):functional+=1;timer_branches+=1
                else:raise ValueError('actual callback model accepted inverted ownership branch: '+name+'+'+str(at))
    for kind,pins in ((2,code_pins(worker)),(1,DATA_PINS)):
        for name in pins:
            address,body,section=symbol_bytes(worker,name,kind);offset=section[4]+address-section[3]
            for i in range(len(body)):
                changed=bytearray(worker[0]);changed[offset+i]^=1
                try:pinned((bytes(changed),*worker[1:]))
                except ValueError:bytes_rejected+=1
                else:raise ValueError('witness pin accepted changed linked instructions/targets: '+name)
    for name in DATA_PINS:
        address,count,kind,owner=worker[4][name]
        for variant in ('function','size','writable'):
            symbols=dict(worker[4]);sections=list(worker[3])
            if variant=='function':symbols[name]=(address,count,2,owner)
            elif variant=='size':symbols[name]=(address,count+4,kind,owner)
            else:
                section=list(sections[owner]);section[2]|=1;sections[owner]=tuple(section)
            trial=(*worker[:3],sections,symbols)
            try:pinned(trial)
            except ValueError:schemas+=1
            else:raise ValueError('controller witness accepted an untyped/mutable constant table')
    geometry_schemas=0
    if lean_worker_profile(worker):
        geometry=worker_geometry(worker)
        stack_begin=0xa0000000|geometry['stack_begin']
        worker_end=0xa0000000|geometry['end']
        # New geometry must never fall back to the historical larger object
        # minima or its now-unowned final 16 KiB. Exercise schema admission
        # independently from the code/literal pins above.
        for name in objects(worker):
            address,count,kind,owner=worker[4][name]
            for variant in ('size','function','stack','readonly'):
                symbols=dict(worker[4]);sections=list(worker[3])
                if variant=='size':symbols[name]=(address,count+4,kind,owner)
                elif variant=='function':symbols[name]=(address,count,2,owner)
                elif variant=='stack':symbols[name]=(stack_begin,count,kind,owner)
                else:
                    section=list(sections[owner]);section[2]&=~1;sections[owner]=tuple(section)
                trial=(*worker[:3],sections,symbols)
                try:objects(trial)
                except ValueError:geometry_schemas+=1
                else:raise ValueError('lean witness accepted changed exact owned object geometry: '+name)
        marker='__kui_native_general_lean_worker_layout'
        for value in (None,(0,0,0,0xfff1),(1,4,0,0xfff1),(1,0,1,0xfff1),(1,0,0,1)):
            symbols=dict(worker[4])
            if value is None:symbols.pop(marker)
            else:symbols[marker]=value
            try:objects((*worker[:4],symbols))
            except ValueError:geometry_schemas+=1
            else:raise ValueError('lean witness accepted an inexact compact worker marker')
        c=WitnessCPU(resident,worker,layout)
        require(c.owned_stack_address(stack_begin,0x1000) and
                not c.owned_worker_address(worker_end) and
                not c.owned_worker_address(worker_end-1,2) and
                not c.owned_stack_address(stack_begin-1),
                'lean witness model retained historical private-stack/worker ownership')
    return dict(actual_instruction_mutation_rejections=functional,
                owned_timer_policy_branch_mutation_rejections=timer_branches,
                instruction_and_literal_byte_mutation_rejections=bytes_rejected,
                typed_constant_schema_mutation_rejections=schemas,
                lean_owned_state_schema_mutation_rejections=geometry_schemas)


def companion_mutations(worker,report):
    import copy
    require(witness_companion(worker,{'controller_failure_witness':report}),
            'complete controller witness companion did not select its profile')
    variants=[]
    for field,value in (('schema','wrong'),('artifact_sha256','0'*64),('immutable_pins',{}),
                        ('helper_model_cases',0),('controller_model_cases',0),('identity_model_cases',0),
                        ('enabled',False)):
        candidate=copy.deepcopy(report);candidate[field]=value;variants.append(candidate)
    for field,value in (('all_controller_reasons',[]),('all_controller_context_reasons',[]),
                        ('old_transfer_witness_preserved',False),('failure_no_owner_writes',False),
                        ('identity_first_failure_read_order',False)):
        candidate=copy.deepcopy(report);candidate['predicate_coverage'][field]=value;variants.append(candidate)
    for name in DATA_PINS:
        candidate=copy.deepcopy(report);candidate['immutable_pins'].pop(name);variants.append(candidate)
    for key,field,value in (('terminal_identity','actual_validation_owner_reads',17),
                            ('terminal_identity','invalid_capability_preserves_raw_first_failure',False),
                            ('identity','ordinary_capability_absent_owner_reads',9)):
        candidate=copy.deepcopy(report);candidate[key][field]=value;variants.append(candidate)
    if owned_timer_profile(worker):
        for field,value in (('enabled',False),('cases',80),('accepted_cases',37),
                ('worker_imask',14),('exact_owner_executable_halfword_bounds',False),
                ('callback_target_never_read_or_called',False),
                ('callback_validation_changes_no_owner_memory_or_sr_control_bits',False)):
            candidate=copy.deepcopy(report);candidate['owned_timer_callbacks'][field]=value;variants.append(candidate)
    refused=0
    for candidate in variants:
        try:witness_companion(worker,{'controller_failure_witness':candidate})
        except ValueError:refused+=1
        else:raise ValueError('controller witness accepted an incomplete/foreign companion')
    for value in ((0,0,0,0xfff1),(1,0,0,1)):
        symbols=dict(worker[4]);symbols[WITNESS_MARK]=value
        try:witness_companion((*worker[:4],symbols),{'controller_failure_witness':report})
        except ValueError:refused+=1
        else:raise ValueError('controller witness accepted an inexact marker')
    for name in (WITNESS_MARK,'__kui_native_terminal_dispatch_worker'):
        symbols=dict(worker[4]);symbols.pop(name)
        try:witness_companion((*worker[:4],symbols),None)
        except ValueError:refused+=1
        else:raise ValueError('controller witness accepted an unpaired terminal/witness marker')
    if owned_timer_profile(worker):
        for value in (None,(0,0,0,0xfff1),(1,4,0,0xfff1),(1,0,1,0xfff1),(1,0,0,1)):
            symbols=dict(worker[4])
            if value is None:symbols.pop(TIMER_MARK)
            else:symbols[TIMER_MARK]=value
            try:witness_companion((*worker[:4],symbols),{'controller_failure_witness':report})
            except ValueError:refused+=1
            else:raise ValueError('controller witness accepted an inexact owned timer marker')
    return refused


def check(directory,self_test=False):
    directory=Path(directory);worker=read_elf(directory/'native-worker.elf')
    if not witness_profile(worker):return dict(schema=SCHEMA,enabled=False)
    pins=pinned(worker);owned=objects(worker)
    resident=read_elf(directory/'resident-sci.elf');layout=console_layout()
    truth=scalar_truth_cases()
    MODELED_FUNCTIONS.clear()
    helpers=helper_cases(resident,worker,layout)
    timers=callback_ownership_cases(resident,worker,layout)
    context=context_cases(resident,worker,layout)
    identity=identity_cases(resident,worker,layout)
    terminal=terminal_identity_cases(resident,worker,layout)
    complete=complete_cases(resident,worker,layout)
    require(MODELED_FUNCTIONS<=set(code_pins(worker)),'actual witness execution entered an unpinned function')
    report=dict(schema=SCHEMA,enabled=True,artifact_sha256=sha(worker[0]),immutable_pins=pins,
        uncached_owned_objects=owned,scalar_truth_cases=truth,executed_owned_functions=sorted(MODELED_FUNCTIONS),
        actual_sh_console_layout=layout,helper_model_cases=helpers,
        controller_model_cases=context['cases']+complete['cases'],identity_model_cases=identity['cases']+terminal['cases'],
        success_model_cases=complete['successes'],failure_model_cases=complete['failures'],
        controller_context=context,controller_completion=complete,identity=identity,terminal_identity=terminal,
        owned_timer_callbacks=timers,
        predicate_coverage=dict(all_controller_reasons=complete['reasons'],
            all_controller_context_reasons=context['reasons'],identity_first_failure_read_order=True,
            identity_shape_bitmap=True,resume_then_consumer_first_failure=True,
            old_transfer_witness_preserved=True,success_only_parent_presence_byte=True,failure_no_owner_writes=True),
        model_scope=dict(actual_linked_worker_c_executed=True,
            typed_substitute='Unchanged transfer_context_valid boolean boundary only; earlier transfer witness preserved',
            failure_terminal='_fail argument checked before display; witness stores execute in actual shared helper',
            source_bool_return='Compiler cloned helper to void isra form; original callers synthesize false',
            coherent_ram_only=True,console_execution_verified=False))
    if self_test:
        report.update(mutations(resident,worker,layout))
        report['companion_mutation_rejections']=companion_mutations(worker,report)
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path);parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args()
    try:report=check(args.directory,args.self_test)
    except (OSError,ValueError,AssertionError,KeyError,struct.error,subprocess.CalledProcessError) as error:
        parser.exit(2,'Controller failure witness refused: '+str(error)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
