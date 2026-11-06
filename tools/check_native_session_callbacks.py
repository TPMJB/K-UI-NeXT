#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the session-X cold callback ABI using pristine authored code maps.

The ordinary dual reader keeps its original X/Y models. Session admission,
low-data bounds and pointer rebase execute as linked SH. Only bounded memory
primitives, the separate publisher and final low publication ABI are fixtures.
"""
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile

from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from check_native_session_preclear_profile import (stage_profile,FUNCTIONS,
    symbol_records,role_value,REFERENCE_COMMIT)
from check_native_session_readahead_profile import FUNCTIONS as READAHEAD_FUNCTIONS

ROOT=Path(__file__).resolve().parents[1]
QUALIFIER='@vmu_native_heap_probe_stage.c'
NAMES=('_native_low_bindings','_native_arm_heap_hook','_native_low_configure',
       '_native_session_low_data','_native_session_cold_bind','_control',
       '_kui_vmu_native_background_map_address')
PINS={name:(READAHEAD_FUNCTIONS[name] if name=='_kui_vmu_native_background_map_address' else
            FUNCTIONS[name+QUALIFIER]) for name in NAMES}

def require(ok,message):
    if not ok:raise ValueError(message)

def profile(stage):return stage_profile(stage)

def pin_helpers(stage):
    from check_native_dual_callbacks import preclear_profile
    require(profile(stage),'Session callback map requires the exact General/session stage')
    require(preclear_profile(stage),'Session callbacks require the exact preheap publisher markers')
    _,message,owner=symbol_bytes(stage,'_native_low_bindings_refused',1)
    require(message==b'DUAL LOW CALLBACK READER REFUSED\0' and owner[2]&7==2,
            'Session callback refusal message changed')
    records=symbol_records(stage);result={}
    for name,(count,digest,roles) in PINS.items():
        address,body,section=symbol_bytes(stage,name,2);normalized=bytearray(body)
        require(len(body)==count and section[1]==1 and section[2]&7==6,
                'Session callback function type/size changed: '+name)
        for offset,role in roles:
            require(not(address+offset)&3 and 0<=offset<=len(body)-4,'Session callback literal escapes its body')
            role_value(stage,records,role,struct.unpack_from('<I',body,offset)[0])
            normalized[offset:offset+4]=bytes(4)
        require(hashlib.sha256(normalized).hexdigest()==digest,
                'Session callback instructions/typed targets changed: '+name)
        result[name]=dict(bytes=count,actual_sha256=hashlib.sha256(body).hexdigest(),
            named_role_canonical_sha256=digest,exact_named_literal_words=len(roles))
    return result

def console_layout():
    from test_native_dual_readers import console_layouts,object_section
    result=console_layouts(general=True,session=True)['layouts']['x']
    fields={name:'offsetof(kui_retail_async_region_type,'+name+')' for name in (
        'engine.cursor.manifest','engine.cursor.context','shared.service.tracks',
        'shared.service.ops.context','shared.card.device.sd','shared.card.device.sd.bus.ctx',
        'shared.stream.card','shared.stream.area[0]','shared.stream.area[1]')}
    fields['manifest_slots']='offsetof(struct kui_retail_manifest,slots)'
    fields.update(config_bytes='sizeof(struct kui_vmu_native_passive_config)',
        config_sd_lease='offsetof(struct kui_vmu_native_passive_config,sd_lease)',
        config_low_terminal='offsetof(struct kui_vmu_native_passive_config,low_terminal)',
        config_low_heap_hook='offsetof(struct kui_vmu_native_passive_config,low_heap_hook)',
        config_session_control='offsetof(struct kui_vmu_native_passive_config,session_control)')
    compiler=shutil.which('sh-elf-gcc');require(compiler is not None,'Actual SH layout compiler required')
    with tempfile.TemporaryDirectory(prefix='kui-session-callback-layout-') as temporary:
        folder=Path(temporary);make=folder/'flags.mk';flags=folder/'flags'
        make.write_text('include '+str(ROOT/'Makefile.vmu-native-session-controls')+'\n'
            'dump-session-callback-flags:\n\t$(file >'+str(flags)+',$(RESIDENT_FLAGS))\n\t@true\n')
        subprocess.run(['make','--no-print-directory','-s','-f',str(make),'dump-session-callback-flags'],cwd=ROOT,check=True)
        selected=[word for word in shlex.split(flags.read_text()) if word.startswith(('-D','-I'))]
        source=folder/'layout.c';binary=folder/'layout.o'
        source.write_text('#include "retail_async.h"\n#include "kui/vmu_native_passive.h"\n'
            '#if KUI_NATIVE_SESSION_CONTROLS != 1\n#error Session ABI required\n#endif\n'
            'const unsigned layout[] __attribute__((section(".rodata.session_layout"),used))={'+
            ','.join(fields.values())+'};\n')
        subprocess.run([compiler,'-std=c11','-Os','-m4-single','-ml','-ffreestanding',*selected,
            '-c',str(source),'-o',str(binary)],cwd=ROOT,check=True)
        values=object_section(binary,'.rodata.session_layout')
        require(len(values)==4*len(fields),'Session callback scalar layout differs')
        result.update(zip(fields,struct.unpack('<'+'I'*len(fields),values)))
    return result


def binder_cases(resident,stage,expected,layout):
    from check_native_dual_callbacks import CallbackCPU,assert_c_abi,MASK
    from check_vmu_native_readahead import session_low_profile
    require(session_low_profile(resident),'Actual session getter requires its exact pinned low profile')
    getter=resident[4]['_kui_retail_native_session_control'][0]
    descriptor=resident[4]['_native_session_binding'][0]
    low_end=resident[4]['__retail_resident_binary_end'][0]
    class SessionCPU(CallbackCPU):
        def __init__(self,seed=0,bank=1,paused=False,result=1):
            super().__init__(resident,stage,1,1,seed=seed,bank=bank)
            self.code_bounds=[]
            for name in NAMES:
                at,body,_=symbol_bytes(stage,name,2);self.code_bounds.append((at,at+len(body)))
            at,body,_=symbol_bytes(resident,'_kui_retail_native_session_control',2)
            self.code_bounds.append((at,at+len(body)))
            self.code_bounds.append((stage[4]['_kui_vmu_native_worker_call'][0],stage[4]['_kui_vmu_native_worker_call'][0]+2))
            self.api={stage[4][name][0]:name for name in ('_memcpy','_memcmp','_kui_native_passive_publish')}
            self.publisher=expected[0][5];self.targets|=set(self.api)|{self.publisher}
            self.sr|=0x40000000
            self.previous=stage[4]['_native_background_boot_region'][0]
            self.lease=0x8c400000;self.owned=self.lease+0x1c000;self.retained=self.lease+0x1c400
            self.source=self.manifest;self.blob=0x8cdf0000;self.published=[];self.result=result
            for at in range(0x760):self.write(self.previous+at,0,1)
            for at in range(1020):self.write(self.source+at,(at*31+seed)&255,1)
            for key,value in (('shared.service.initialized',1),('shared.service.tracks',self.source+layout['manifest_slots']),
                    ('engine.manifest',self.source)):
                self.write(self.previous+layout[key],value)
            if paused:
                for key,value in (('engine.cursor.manifest',self.source),
                    ('shared.stream.card',self.previous+layout['shared.card.device.sd']),
                    ('shared.stream.area[0]',self.previous+layout['area0']),
                    ('shared.stream.area[1]',self.previous+layout['area1']),
                    ('shared.stream.state',1),('engine.opened',1)):
                    self.write(self.previous+layout[key],value)
                self.write(self.previous+layout['shared.port.acquired'],1,1)
            self.write(self.source+layout['manifest_reader'],1)
            self.write(self.source+layout['manifest_transport'],1)
            for at in range(1020):self.write(self.owned+at,self.read(self.source+at,1),1)
            for at in range(0x760):self.write(self.retained+at,self.read(self.previous+at,1),1)
            self.original=bytes(self.read(self.previous+at,1) for at in range(0x760))
            self.expected=bytearray(self.original)
            replacements={'shared.service.tracks':self.owned+layout['manifest_slots'],'engine.manifest':self.owned}
            if paused:replacements.update({'engine.cursor.manifest':self.owned,
                'shared.stream.card':self.retained+layout['shared.card.device.sd'],
                'shared.stream.area[0]':self.retained+layout['area0'],
                'shared.stream.area[1]':self.retained+layout['area1']})
            for key,value in replacements.items():struct.pack_into('<I',self.expected,layout[key],value)
            _,binding,_=symbol_bytes(resident,'_native_session_binding',1)
            self.region_cell,self.manifest_cell=struct.unpack_from('<2I',binding,12)
            self.write(self.region_cell|0x20000000,self.previous)
            self.write(self.manifest_cell|0x20000000,self.source)
            control=expected[0][2]|0x20000000
            for at in range(14):self.write(control+4*at,0)
            self.write(control+8,3);self.write(control+12,self.lease&0x1fffffff)
            self.control['vbr']=0x8c00f400
            for at in range(low_end-0x8c004000):self.write(self.blob+at,self.read(0x8c004000+at,1),1)
            for name,value in (('_resident_bytes',low_end-0x8c004000),('_resident_blob',self.blob),('_resident_limit',0x8c007ba0)):
                self.write(stage[4][name][0],value)
            self.r[4:8]=[getter,self.publisher,self.source,self.owned]
            self.initial=self.snapshot();self.writes=[];self.executed_addresses=set()
        def plain(self,op,pc):
            if op>>8==0xca:  # XOR #imm,R0 leaves the entire SR unchanged.
                self.r[0]^=op&255;return
            return super().plain(op,pc)
        def callee(self):
            if self.pc==self.publisher:
                require(self.r[4:6]==[self.source,self.owned] and self.published==[(self.retained,self.retained+0x760)],
                    'Session low publication preceded complete retained-region publication')
                require(bytes(self.read(self.retained+i,1) for i in range(0x760))==self.expected,
                    'Actual session rebase differs from independent named-field oracle')
                self.map_calls.append(self.pc);continuation=self.pr
                for i in range(8):self.r[i]=(0x93750000+i*0x13579bd)&MASK
                self.r[0]=self.result&MASK;self.sr^=1;self.pc=continuation;return
            name=self.api.get(self.pc)
            if name is None:return super().callee()
            continuation=self.pr;result=0
            if name=='_kui_native_passive_publish':
                require(self.r[4:6]==[self.retained,self.retained+0x760],'Session publisher span changed')
                self.published.append(tuple(self.r[4:6]))
            else:
                left,right,count=self.r[4:7]
                require(count in (24,1020,0x760),'Session memory primitive extent changed')
                data=bytes(self.read(right+i,1) for i in range(count))
                if name=='_memcpy':
                    require(count==24 and self.stack_top-256<=left<=self.stack_top-24 and right==self.blob+descriptor-0x8c004000,
                        'Session descriptor copy escaped its exact source/caller stack')
                    for i,value in enumerate(data):self.write(left+i,value,1)
                    result=left
                else:result=int(any(self.read(left+i,1)!=value for i,value in enumerate(data)))
            for i in range(8):self.r[i]=(0x94560000+i*0x13579bd)&MASK
            self.r[0]=result;self.sr^=1;self.pr^=0x2468ace0;self.pc=continuation
        def execute(self,accepted):
            retained_before=bytes(self.read(self.retained+i,1) for i in range(0x760))
            self.run_symbol('_native_session_cold_bind');assert_c_abi(self,self.initial)
            require(bytes(self.read(self.previous+i,1) for i in range(0x760))==self.original,
                'Session cold binder changed the original stage workspace')
            require(self.map_calls==([self.publisher] if accepted else []) and self.r[0]==(self.result&MASK if accepted else 0),
                'Session cold helper returned/published after an invalid snapshot')
            if not accepted:
                require(not self.published and bytes(self.read(self.retained+i,1) for i in range(0x760))==retained_before,
                        'Refused session helper changed/published a retained region')
            allowed={self.retained+layout[key] for key in ('shared.service.tracks','engine.manifest',
                'engine.cursor.manifest','shared.stream.card','shared.stream.area[0]','shared.stream.area[1]')}
            require(all((at in allowed and count==4) or self.stack_top-256<=at<self.stack_top
                for _,at,_,count in self.writes),'Session helper wrote outside named retained fields/caller stack')
            require(not self.iprb_writes and not self.vbr_writes,'Session helper changed interrupt ownership')
            return self
    truth=0
    for immediate in range(256):
        for value in (0,1,0x7fffffff,MASK):
            for t in (0,1):
                cpu=SessionCPU.__new__(SessionCPU);cpu.r=[0x53530000+i*0x12345 for i in range(16)]
                cpu.r[0]=value;cpu.sr=0x60000bf0|t;before=cpu.r.copy();sr=cpu.sr
                cpu.plain(0xca00|immediate,0)
                before[0]=value^immediate
                require(cpu.r==before and cpu.sr==sr,'Session XOR-immediate model changed SR/other registers')
                truth+=1
    successes=0
    for bank in (0,1):
        for seed in range(6):
            for paused in (False,True):
                for result in (0,1,2,MASK):
                    cpu=SessionCPU(seed,bank,paused,result).execute(True)
                    for name in ('_native_session_cold_bind','_native_session_low_data','_control','_native_low_bindings'):
                        require(stage[4][name][0] in cpu.executed_addresses,'Session helper was substituted/unreached: '+name)
                    successes+=1
    failures=0
    variants={
        'getter_odd':lambda c:c.r.__setitem__(4,getter|1),
        'publisher_odd':lambda c:c.r.__setitem__(5,c.publisher|1),
        'imask':lambda c:setattr(c,'sr',c.sr&~0xf0),
        'bl':lambda c:setattr(c,'sr',c.sr&~0x40000000),
        'vbr':lambda c:c.control.__setitem__('vbr',c.previous-0x100),
        'snapshot':lambda c:c.write(c.retained,1,1),
        'map':lambda c:c.write(c.owned,1,1),
        'cell':lambda c:c.write(c.manifest_cell|0x20000000,c.source+4),
        'binding_copy':lambda c:c.write(c.blob+descriptor-0x8c004000,0),
    }
    for key in ('shared.service.pending','shared.service.executing','engine.active','engine.in_irq',
                'engine.hooked','engine.release.armed','engine.release.depth','engine.native_loan_state','shared.stream.state'):
        variants[key]=lambda c,key=key:c.write(c.previous+layout[key],2)
    for name,change in variants.items():
        c=SessionCPU();change(c)
        # A changed source snapshot must remain unchanged by a refusal too.
        c.original=bytes(c.read(c.previous+i,1) for i in range(0x760))
        c.initial=c.snapshot();c.writes=[];c.execute(False);failures+=1
    return dict(actual_linked_manifest_binding_cases=successes,session_snapshot_refusals=failures,
        additional_scalar_truth_cases=truth,
        actual_session_admission_low_data_and_rebase_helpers_executed=True,
        actual_pinned_low_getter_executed=True,retained_all_pointer_rebases_checked=True,
        complete_region_published_before_selected_low_publication=True,
        low_final_publication='Strict selected-entry ABI substitute; low implementation has separate C/ownership tests',
        no_old_workspace_or_interrupt_ownership_changed=True)


def stage_reader_refusal():
    """Existing actual production C/decoder test explicitly rejects Legacy Y."""
    with tempfile.TemporaryDirectory(prefix='kui-session-reader-refusal-') as temporary:
        binary=Path(temporary)/'stage'
        plan=subprocess.check_output(['make','-n','-B','build/test-native-session-stage'],cwd=ROOT,text=True)
        commands=[shlex.split(line) for line in plan.splitlines() if 'tests/test_native_session_stage.c' in line and ' -o ' in line]
        require(len(commands)==1,'Missing actual production session-stage test command')
        command=commands[0];command[-1]=str(binary)
        subprocess.run(command,cwd=ROOT,check=True,capture_output=True,text=True)
        output=subprocess.check_output([str(binary)],cwd=ROOT,text=True,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
        sources={name for name in command if name.endswith('.c') and (ROOT/name).is_file()}
        sources.update(('src/loader/vmu_native_session_stage.inc','include/kui/native_session.h'))
        return dict(status='PASS',actual_production_C_and_decoders=True,
            legacy_y_refused_before_session_fields=True,output=output,
            source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in sorted(sources)})


def consumers(resident,stage,expected,layout):
    from check_native_dual_callbacks import (arm_case,configure_case,CallbackCPU,
        Refused,ARM,CONFIGURE)
    arms=configurations=refusals=invalid=0
    for reader in (1,2):
        for bank in (0,1):
            for seed in range(6):
                for preclear in (False,True):
                    arm_case(resident,stage,expected,reader,seed=seed,bank=bank,preclear=preclear);arms+=1
                configure_case(resident,stage,expected,layout,reader,seed=seed,bank=bank);configurations+=1
        for variant in [*(('control',i) for i in range(14)),*(('original',i) for i in range(8)),'slot','readback']:
            arm_case(resident,stage,expected,reader,variant=variant);refusals+=1
    for reader,transport in ((0,1),(3,1),(255,1),(0xffffffff,1),(1,0),(2,0),(1,2),(2,3)):
        for name in (ARM,CONFIGURE):
            cpu=CallbackCPU(resident,stage,reader,transport);cpu.forbid_control_read=True
            cpu.r[4:6]=[0x8ce00000,0x8c420000];cpu.writes=[]
            try:cpu.run_symbol(name)
            except Refused:
                require(cpu.refusal==[0,reader] and not cpu.flushes and not cpu.map_calls and
                    all(cpu.stack_top-256<=at<cpu.stack_top for _,at,_,_ in cpu.writes),
                    'Invalid session callback consumer reached dependent ownership')
                invalid+=1
            else:raise ValueError('Session cold consumer accepted an unsupported reader/transport')
    return dict(actual_linked_arming_cases=arms,actual_linked_configuration_cases=configurations,
        arming_ownership_refusals=refusals,invalid_consumer_mode_refusals=invalid,
        both_preheap_and_normal_installer_targets_checked=True,
        initializer_slot_and_owner_return_use_selected_reader=True,
        three_worker_callbacks_use_selected_reader=True,
        original_control_and_frame_neighbours_preserved=True,
        exact_initializer_cache_line_published_before_readback=True,
        unsupported_modes_stop_before_control_or_callback_writes=True,
        no_interrupt_register_writes=True,**binder_cases(resident,stage,expected,layout))


def pin_mutants(stage,expected):
    from check_native_dual_callbacks import linked_table,TABLE
    rejected=0
    for name in (*PINS,'_native_low_bindings_refused'):
        address,body,section=symbol_bytes(stage,name,1 if name=='_native_low_bindings_refused' else 2)
        start=section[4]+address-section[3]
        for at in range(len(body)):
            data=bytearray(stage[0]);data[start+at]^=1
            try:pin_helpers((bytes(data),*stage[1:]))
            except ValueError:rejected+=1
            else:raise ValueError('Session callback map accepted a changed instruction/literal/message')
    address,body,section=symbol_bytes(stage,TABLE,1);start=section[4]+address-section[3]
    for at in range(len(body)):
        data=bytearray(stage[0]);data[start+at]^=1
        try:linked_table((bytes(data),*stage[1:]),expected)
        except ValueError:rejected+=1
        else:raise ValueError('Session callback table accepted a mixed reader role')
    for marker in ('__kui_native_dual_low_callbacks_stage','__kui_native_preheap_clear_stage','__kui_native_preheap_clear_tail'):
        for value in (None,(0,0,0,0xfff1),(2,0,0,0xfff1),(1,4,1,1)):
            symbols=stage[4].copy()
            if value is None:symbols.pop(marker)
            else:symbols[marker]=value
            try:
                pin_helpers((*stage[:4],symbols));linked_table((*stage[:4],symbols),expected)
            except ValueError:rejected+=1
            else:raise ValueError('Session callback map accepted changed capability metadata')
    return rejected


def check(directory,*,self_test=False):
    from check_native_dual_callbacks import (SCHEMA,FIELDS,expected_entries,linked_table,
        selector_cases,instruction_truth,selector_case,changed_table_word,hardcoded_selector_mutant)
    directory=Path(directory)
    resident,legacy,stage=(read_elf(directory/name) for name in ('resident-sci.elf','resident-legacy.elf','stage.elf'))
    expected=(expected_entries(resident),expected_entries(legacy));table=linked_table(stage,expected)
    pins=pin_helpers(stage);layout=console_layout()
    proof=dict(schema=SCHEMA,enabled=True,session_controls=True,
        session_reader_scope='New X only; Legacy Y uses its separate ordinary General module',
        reference_commit=REFERENCE_COMMIT,
        artifact_sha256={name:hashlib.sha256(image[0]).hexdigest() for name,image in
            (('stage',stage),('resident',resident),('legacy',legacy))},
        actual_linked_stage_executed=True,helper_instruction_truth_cases=instruction_truth(),
        exact_named_role_instruction_pins=pins,
        expected_callback_roles={name:dict(zip(FIELDS,row)) for name,row in zip(('new','legacy'),expected)},
        actual_sh_configuration_layout=layout,selector=selector_cases(resident,stage,expected,table),
        consumers=consumers(resident,stage,expected,layout),session_y_stage_refusal=stage_reader_refusal())
    if self_test:
        mutated=0
        for row in (0,1):
            for i in range(6):
                replacement=expected[1-row][i]
                if replacement==expected[row][i]:replacement^=4
                changed=changed_table_word(stage,table+24*row+4*i,replacement)
                try:selector_case(resident,changed,expected,table,row+1,1)
                except (ValueError,AssertionError):mutated+=1
                else:raise ValueError('Session selector accepted an incorrect selected low role')
        proof['self_test']=dict(individual_wrong_role_mutants=mutated,
            pinned_byte_and_capability_mutants_rejected=pin_mutants(stage,expected),
            actual_hardcoded_sci_selector_mutant_rejected=hardcoded_selector_mutant(resident,stage,expected,table))
    return proof


def require_companion(proof):
    pins=proof.get('exact_named_role_instruction_pins',{})
    require(proof.get('session_controls') is True and proof.get('reference_commit')==REFERENCE_COMMIT and
        set(pins)==set(PINS) and all(pins[name].get('bytes')==count and
            pins[name].get('named_role_canonical_sha256')==digest and
            pins[name].get('exact_named_literal_words')==len(roles)
            for name,(count,digest,roles) in PINS.items()),'Session callback companion lacks its exact authored helper map')
    selected=proof.get('selector',{});consumer=proof.get('consumers',{})
    require(selected.get('actual_linked_selector_cases')==210 and selected.get('unsupported_mode_refusals')==198 and
        all(selected.get(key) is True for key in ('all_six_roles_from_selected_actual_elf','exact_immutable_row_pointer',
        'c_abi_preserved','no_interrupt_register_or_nonstack_writes')),'Session callback selector proof is incomplete')
    counts=dict(actual_linked_arming_cases=48,actual_linked_configuration_cases=24,arming_ownership_refusals=48,
        invalid_consumer_mode_refusals=16,actual_linked_manifest_binding_cases=96,session_snapshot_refusals=18,
        additional_scalar_truth_cases=2048)
    require(set(consumer)==set(counts)|{'low_final_publication'}|{key for key in (
        'both_preheap_and_normal_installer_targets_checked','initializer_slot_and_owner_return_use_selected_reader',
        'three_worker_callbacks_use_selected_reader','original_control_and_frame_neighbours_preserved',
        'exact_initializer_cache_line_published_before_readback','unsupported_modes_stop_before_control_or_callback_writes',
        'no_interrupt_register_writes','actual_session_admission_low_data_and_rebase_helpers_executed',
        'actual_pinned_low_getter_executed','retained_all_pointer_rebases_checked',
        'complete_region_published_before_selected_low_publication','no_old_workspace_or_interrupt_ownership_changed')} and
        all(consumer.get(key)==value for key,value in counts.items()) and
        all(value is True for key,value in consumer.items() if key not in counts and key!='low_final_publication') and
        consumer['low_final_publication']=='Strict selected-entry ABI substitute; low implementation has separate C/ownership tests',
        'Session callback companion lacks actual helper/arming/refusal execution')
    refusal=proof.get('session_y_stage_refusal',{})
    require(refusal.get('status')=='PASS' and refusal.get('actual_production_C_and_decoders') is True and
        refusal.get('legacy_y_refused_before_session_fields') is True and refusal.get('source_sha256') and
        all(hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest for name,digest in refusal['source_sha256'].items()),
        'Session companion lacks its current actual production Legacy-Y refusal')
    return True
