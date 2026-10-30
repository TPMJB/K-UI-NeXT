#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the dual native selector/binder/getter and audit the legacy image.

Only the three separately audited cold binders are ABI substitutes. The
selector, mode gate, worker getter and small scalar helpers execute their
actual linked SH instructions. This proves selection and isolation, not
console compatibility or reader performance.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
from native_runtime_layout import LEASE_BYTES, BACKGROUND_COMPACT, layout_for
import struct
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from check_loader_layout import inspect_elf
from check_vmu_native_sdk_sci_route import SDKCPU,RETURN,assert_c_abi
from check_vmu_native_readahead import WorkerCPU,closed_callgraph
from test_vmu_native_heap_execution import MASK,signed
from test_native_dual_readers import console_layouts

MARK='__kui_native_dual_readers_'
CAPABILITY=struct.pack('<8I',0x3152444b,0x4c415544,1,32,1,2,LEASE_BYTES,0)
# Populated only after reviewing the complete candidate instructions and
# relocation literals. No digest is learned from the artifact at run time.
STAGE_PINS={
    '_select_resident':(272,'c2224460dee18bfaaab0f0cca6864cc939f0546e51e4877cc7e5cd2435ba11df'),
    '_native_dual_reader_bind':(68,'459969f187c257290af41c200c907794553f806d6e05c12e9835f74af6c76580'),
    '_kui_native_dual_u32':(30,'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c'),
}
WORKER_PINS={
    '_reader_readahead_exports':(200,'04a1748a2f5f36a1e79fa2a791a5e16a9f90c6ac632f13bd797abf48ce58545c'),
    '_reader_capture_region':(100,'ac7d64231e97b1bbfa703121a832e4cce8a5ee8f7d1302cb6cdc9b77bd076b71'),
    '_reader_readahead_visit':(56,'c44759200e43937665b2987281e995b4f39675f1e05180371aad4078cc02dbf3'),
}
WITNESS_STAGE_PINS={
    '_select_resident':(272,'e089cfbbdace75248af472f8083013555a24292aae83b369f99c18bbf5d883b3'),
    '_native_dual_reader_bind':(68,'22523932dde059f9027053b15ba16e7258190d75af0fca131575b2062c2bf1ba'),
    '_kui_native_dual_u32':(30,'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c'),
}
# The card-preflight failure page relocates18 selector pointer literals and
# three binder literals to the same exact symbols/offsets and string. These
# remain complete whole-body pins, selected only for the witnessed stage.
PREFLIGHT_WITNESS_STAGE_PINS={
    '_select_resident':(272,'fe08e4dac2eb616e186ff9b2a1d593e431f14a01bcd887c124c700d7b1e0cc6c'),
    '_native_dual_reader_bind':(68,'8591d839def3c7507326e6226c18c126384ae874e375bbaa68505fd8dee1603f'),
}
WITNESS_WORKER_PINS={
    '_reader_readahead_exports':(200,'6a753b4f52fd9aad30c334241ea8c0de92cefcac3c3a85009af4645b260d67ef'),
    '_reader_capture_region':(100,'a2ba9caef7fb729bab16d02c1d16ea468018ab128cc2ba8143802eac16bad827'),
    '_reader_readahead_visit':(56,'c44759200e43937665b2987281e995b4f39675f1e05180371aad4078cc02dbf3'),
}


# Exact whole bodies reviewed for the paired owned-timer/owner-stack profile.
GAME_STARTUP_STAGE_PINS={
    '_select_resident':(272, '454d8e82f79c75f015f79554bea11500cd7950558a72426807f48e1c33f6be86'),
    '_native_dual_reader_bind':(68, 'ab24aceac26e9a5b3d9b37645421a0d9df83cf05fb0c34d53f293813d4e68f42'),
    '_kui_native_dual_u32':(30, 'c13060e358e2a0e9f53878d9aa8b77d1fbfb03a31b8c4e049dc49bf3e7feec6c'),
}

# Exact whole bodies reviewed for the paired owned-timer/owner-stack profile.
GAME_STARTUP_WORKER_PINS={
    '_reader_readahead_exports':(200, '13ca433d369112a4b3184b17ec96e025a76e142e537d65c8d3bba2c6e9ce4d5e'),
    '_reader_capture_region':(100, 'f46dc671c3f853a40f735778e6e4d841b869e7a84d2ba92be85aca4e1e3870ed'),
    '_reader_readahead_visit':(56, 'c44759200e43937665b2987281e995b4f39675f1e05180371aad4078cc02dbf3'),
}

def profile_pins(worker,stage=None):
    from check_native_game_startup_profile import game_startup_profile
    from check_native_followup_profile import stage_pins,worker_pins
    if game_startup_profile(worker):
        historical_stage,historical_worker=GAME_STARTUP_STAGE_PINS,GAME_STARTUP_WORKER_PINS
    else:
        from check_native_controller_failure_witness import witness_profile
        if witness_profile(worker):
            require(WITNESS_STAGE_PINS and WITNESS_WORKER_PINS,
                    'dual failure-witness profile lacks separately reviewed pins')
            historical_stage,historical_worker=WITNESS_STAGE_PINS,WITNESS_WORKER_PINS
        else:
            historical_stage,historical_worker=STAGE_PINS,WORKER_PINS
    return (stage_pins(stage,'dual',historical_stage) if stage is not None else historical_stage,
            worker_pins(worker,'dual',historical_worker))


def require(test,message):
    if not test:raise ValueError(message)


def low_callback_companion(artifacts, proof):
    """A blob selector alone cannot prove its cold callback entry points."""
    from check_native_dual_callbacks import SCHEMA, FIELDS
    require(isinstance(proof,dict) and proof.get('schema')==SCHEMA and
            proof.get('enabled') is True and proof.get('actual_linked_stage_executed') is True,
            'dual reader requires the executed selected low-callback proof')
    recorded=proof.get('artifact_sha256',{})
    require(set(recorded)=={'stage','resident','legacy'} and
            all(isinstance(artifacts.get(name),str) and len(artifacts[name])==64 and
                recorded[name]==artifacts[name] for name in recorded),
            'dual low-callback proof does not bind these exact stage/readers')
    roles=proof.get('expected_callback_roles',{})
    require(set(roles)=={'new','legacy'} and
            all(set(row)==set(FIELDS) and all(type(value) is int and 0x8c004000<=value<0x8c007ba0
                for value in row.values()) for row in roles.values()),
            'dual low-callback proof omits a typed selected-reader role')
    selector=proof.get('selector',{});consumer=proof.get('consumers',{})
    require(selector.get('actual_linked_selector_cases')==210 and
            selector.get('unsupported_mode_refusals')==198 and
            all(selector.get(key) is True for key in ('all_six_roles_from_selected_actual_elf',
                'exact_immutable_row_pointer','c_abi_preserved','no_interrupt_register_or_nonstack_writes')) and
            consumer.get('actual_linked_arming_cases')==24 and
            consumer.get('actual_linked_configuration_cases')==24 and
            consumer.get('actual_linked_manifest_binding_cases')==96 and
            consumer.get('arming_ownership_refusals')==48 and
            consumer.get('invalid_consumer_mode_refusals')==24 and
            all(consumer.get(key) is True for key in (
                'initializer_slot_and_owner_return_use_selected_reader',
                'three_worker_callbacks_use_selected_reader','indirect_manifest_binding_uses_selected_reader',
                'source_and_owned_manifest_arguments_and_return_preserved',
                'original_control_and_frame_neighbours_preserved',
                'exact_initializer_cache_line_published_before_readback',
                'typed_manifest_binder_clobbers_caller_saved_registers',
                'tail_binder_retains_actual_rts_pr_contract','no_interrupt_register_writes',
                'unsupported_modes_stop_before_control_or_callback_writes')),
            'dual low-callback proof lacks actual selection, arming, publication or binding execution')
    return True


def sha(data):return hashlib.sha256(data).hexdigest()


def pin(image,pins,label):
    require(pins,'dual '+label+' reviewed instruction pins are missing')
    result={}
    for name,(count,digest) in pins.items():
        address,body,section=symbol_bytes(image,name,2)
        actual=sha(body)
        accepted=((count,digest),)
        if pins is WITNESS_STAGE_PINS and label=='stage' and name in PREFLIGHT_WITNESS_STAGE_PINS:
            accepted+=(PREFLIGHT_WITNESS_STAGE_PINS[name],)
        require(section[2]&7==6 and (len(body),actual) in accepted,
                'dual '+label+' immutable instructions/targets changed: '+name)
        result[name]=(address,len(body),actual)
    return result


def markers(stage,worker):
    for image,role in ((stage,'stage'),(worker,'worker')):
        require(image[4].get(MARK+role)==(1,0,0,0xfff1),
                'dual '+role+' marker is not exact absolute one')
    require(layout_for(stage[4]) is layout_for(worker[4]),
            'dual stage/worker select different complete runtime geometries')


def compact_component_markers(stage,worker,resident,legacy,auxiliary):
    images=((stage,'stage'),(worker,'worker'),(resident,'resident'),
            (legacy,'resident'),(auxiliary,'auxiliary'))
    geometry=layout_for(worker[4])
    require(all(layout_for(image[4]) is geometry for image,_ in images),
            'Combined reader components select different geometry contracts')
    if geometry is not BACKGROUND_COMPACT:
        return {'enabled':False}
    for image,role in images:
        require(image[4].get('__kui_native_background_compact_'+role)==(1,0,0,0xfff1) and
                image[4].get('__kui_native_isolation_lease_bytes')==(0x47000,0,0,0xfff1),
                'Compact Background component lost its exact marker/lease: '+role)
    require(worker[4].get('__kui_native_background_compact_worker_layout')==(1,0,0,0xfff1),
            'Compact Background worker lacks its separately selected linker layout')
    return {'enabled':True,'matching_immutable_component_markers':True,
            'worker_bytes':geometry.WORKER_BYTES,'lease_bytes':geometry.LEASE_BYTES,
            'cache_bytes':geometry.DATA_BYTES,'components':len(images)}


def compact_component_mutations(stage,worker,resident,legacy,auxiliary):
    """Refuse missing, forged or mixed contracts in all five actual images."""
    images=(stage,worker,resident,legacy,auxiliary)
    roles=('stage','worker','resident','resident','auxiliary')
    require(compact_component_markers(*images).get('enabled') is True,
            'Compact component mutations require the complete actual profile')
    rejected=0
    for index,(image,role) in enumerate(zip(images,roles)):
        names=['__kui_native_background_compact_'+role,
               '__kui_native_isolation_lease_bytes']
        if role=='worker':names.append('__kui_native_background_compact_worker_layout')
        for name in names:
            original=image[4][name]
            for value in (None,(original[0]^4,*original[1:]),
                          (original[0],4,*original[2:]),
                          (*original[:3],1)):
                symbols=dict(image[4])
                if value is None:del symbols[name]
                else:symbols[name]=value
                changed=list(images);changed[index]=(*image[:4],symbols)
                try:compact_component_markers(*changed)
                except (ValueError,KeyError):rejected+=1
                else:raise ValueError('Compact combined audit accepted altered '+role+' '+name)
    return rejected


def initialized(image,address,count):
    for section in image[3]:
        if section[1]==1 and section[2]&2 and section[3]<=address<address+count<=section[3]+section[5]:
            start=section[4]+address-section[3]
            return image[0][start:start+count]
    raise ValueError('dual referenced unowned initialized bytes '+hex(address))


def capability(stage,module):
    address,body,section=symbol_bytes(stage,'_kui_native_dual_reader_capability',1)
    require(len(body)==32 and section[2]&7==2 and body==struct.pack('<8I',0x3152444b,0x4c415544,1,32,1,2,layout_for(stage[4]).LEASE_BYTES,0),
            'dual capability must be its exact immutable typed32B descriptor')
    stage_payload=b''.join(stage[0][p[1]:p[1]+p[4]] for p in stage[2] if p[0]==1 and p[4])
    require(stage_payload.count(body)==1 and module.count(body)==1,
            'dual capability is missing or duplicated in the stage/module')
    return dict(address=hex(address),bytes=32,sha256=sha(body),exactly_one_in_stage_and_module=True)


def embedded_blobs(stage,resident,legacy,directory):
    result={}
    for role,image,filename,prefix in (
        ('x',resident,'resident-sci.bin','__retail_resident_sci_blob_'),
        ('y',legacy,'resident-legacy.bin','__retail_resident_legacy_blob_')):
        begin=stage[4][prefix+'start'][0];end=stage[4][prefix+'end'][0]
        binary=(directory/filename).read_bytes()
        inspected=inspect_elf(image[0],0x8c004000,0x8c007ba0)
        require(inspected['payload']==binary,'dual '+role+' binary differs from its exact linked ELF')
        require(end>begin and end-begin==len(binary) and initialized(stage,begin,len(binary))==binary,
                'dual embedded '+role+' blob differs from its separately built image')
        require(image[4].get('__kui_vmu_native_background_resident')==(1,0,0,0xfff1),
                'dual '+role+' low image lacks repaired VMU Background guards')
        result[role]=dict(address=hex(begin),bytes=len(binary),sha256=sha(binary),elf_sha256=sha(image[0]))
    require(result['x']['address']!=result['y']['address'] and result['x']['sha256']!=result['y']['sha256'],
            'dual module substituted one low engine for both modes')
    for name in ('__kui_native_readahead_resident','__kui_native_sdk_sci_route_resident'):
        require(resident[4].get(name)==(1,0,0,0xfff1) and name not in legacy[4],
                'dual SDK/read-ahead engine separation differs')
    for name in ('_kui_native_ra_reader_ops','_kui_retail_sdk_sci_entry'):
        require(name in resident[4] and name not in legacy[4],'legacy low image contains a new-engine entry')
    for name in ('_kui_retail_irq_entry','_kui_retail_vector_interrupt','_kui_retail_rehook',
                 '_kui_retail_release_100','_kui_retail_release_400','_kui_retail_release_600'):
        require(name in legacy[4] and name not in resident[4],
                'dual legacy raw-vector ownership differs: '+name)
    return result


def require_wide_legacy(proof,image):
    from check_vmu_native_map_refusal import wide_guard_pins
    READAHEAD_PINS=wide_guard_pins(image)
    lease_bytes=layout_for(image[4]).LEASE_BYTES
    require(isinstance(proof,dict),'dual legacy guard companion must be a report object')
    guards=proof.get('wide_lease_linked_guard_pins',{})
    require(isinstance(guards,dict),'dual legacy guard pins must be an exact table')
    require(proof.get('selected_guarded_lease_bytes')==lease_bytes and set(guards)==set(READAHEAD_PINS) and
            all(isinstance(guards[name],dict) and guards[name].get('sha256')==digest and guards[name].get('bytes')==count
                for name,(digest,count) in READAHEAD_PINS.items()),
            'dual legacy image requires all three exact compiled384KiB lease guards')
    return guards


def dual_companion(worker,proof):
    """Select dual-only pins only after the complete exact-artifact proof."""
    marker=worker[4].get(MARK+'worker')
    require(bool(marker)==bool(proof and proof.get('dual_reader_candidate')),
            'dual worker and read-ahead companion profile differ')
    if not marker:return False
    from check_native_controller_failure_witness import witness_companion
    witness_companion(worker,proof)
    from check_vmu_native_sdk_sci_route import pio_companion,pio_pair_companion
    require(marker==(1,0,0,0xfff1) and proof.get('enabled') is True and
            proof.get('paired_markers') is True and proof.get('structural_sdk_route') is True and
            proof.get('schema')=='kui.native.read-ahead.audit.v1' and
            pio_companion(worker,proof) and pio_pair_companion(worker,proof),
            'dual profile requires the exact structural PIO-pair read-ahead companion')
    report=proof.get('dual_reader',{});artifacts=report.get('artifact_sha256',{})
    parent_artifacts=proof.get('artifact_sha256',{})
    require(report.get('enabled') is True and report.get('schema')=='kui.native.dual-readers.audit.v1' and
            artifacts.get('worker')==sha(worker[0]) and
            all(artifacts.get(name)==parent_artifacts.get(name) and
                isinstance(artifacts.get(name),str) and len(artifacts[name])==64
                for name in ('resident','worker','stage')),
            'dual pin selection requires the complete matching linked artifact identities')
    _,selected_worker_pins=profile_pins(worker)
    worker_pins=pin(worker,selected_worker_pins,'worker');recorded=report.get('immutable_worker_pins',{})
    require(set(recorded)==set(worker_pins) and
            all(tuple(recorded[name])==tuple(value) for name,value in worker_pins.items()),
            'dual companion lacks the exact executed getter/region/visit pins')
    selected=report.get('selector',{});binding=report.get('late_binding',{});visits=report.get('worker',{})
    from check_native_general_lean_profile import worker_profile
    if worker_profile(worker):
        worker_events=(visits.get('modeled_worker_entry')=='_reader_cooperative_service' and
            visits.get('actual_tick_arguments_checked') is True and
            visits.get('capture_start_not_present_in_general_profile') is True and
            visits.get('rolling_stream_threshold_saturation_reset_and_resume_checked') is True and
            visits.get('both_durable_writer_busy_fields_refused') is True and
            visits.get('only_private_stream_history_and_caller_stack_written') is True and
            visits.get('c_abi_preserved') is True and visits.get('actual_stream_guard_cases',0)>=164 and
            visits.get('actual_busy_refusal_cases',0)>=12 and visits.get('actual_imask_and_bank_cases',0)>=128)
    else:worker_events=visits.get('heartbeat_and_capture_start_checked') is True
    require(selected.get('actual_linked_selector_cases',0)>=1600 and
            selected.get('actual_capability_bit_mutations_before_mode_reads')==256 and
            all(selected.get(key) is True for key in ('all_imasks_and_integer_banks',
                'standard_non_sci_unknown_modes_refused','exact_blob_and_guard_selected',
                'only_selected_metadata_and_caller_stack_written')) and
            binding.get('actual_linked_late_binding_cases',0)>=4032 and
            all(binding.get(key) is True for key in ('x_calls_timeline_sdk_aux_in_order',
                'y_standard_unknown_modes_do_not_call_any_binder',
                'all_argument_values_and_fifth_stack_argument_checked',
                'only_three_exact_typed_binder_targets_substituted',
                'actual_linked_ra_binder_and_initializer_executed')) and
            visits.get('actual_linked_worker_cases',0)>=266 and
            all(visits.get(key) is True for key in (
                'y_standard_unknown_modes_and_forged_cookie_cannot_read_aux_or_vector_entries',
                'foreign_manifest_pointer_refused_before_map_dereference',
                'null_region_and_invalid_config_payloads_refused_before_dependent_reads',
                'malformed_aux_headers_and_public_context_bounds_refused')) and worker_events and
            report.get('additional_scalar_instruction_truth_cases',0)>=5728,
            'dual pin selection requires the complete actual selector/binder/worker SH proof')
    cold=proof.get('cold_binder',{});cold_pins=cold.get('exact_instruction_literal_pins',{})
    tail_pins=binding.get('exact_cold_binder_pins',{})
    require(cold.get('actual_linked_cold_binder_executed') is True and
            cold.get('actual_auxiliary_c_initializer_executed') is True and cold.get('dual_reader_stage') is True and
            cold_pins and set(cold_pins)==set(tail_pins) and
            all(tuple(cold_pins[name])==tuple(tail_pins[name]) for name in cold_pins),
            'dual tail target lacks its matching actual cold-binder/initializer proof')
    legacy=report.get('legacy_background',{});require_wide_legacy(legacy,worker)
    low_callback_companion(artifacts,report.get('selected_low_callbacks'))
    stack=legacy.get('private_stack',{});engines=report.get('embedded_engines',{})
    require(legacy.get('schema')=='kui.native.background.audit.v1' and
            legacy.get('resident_sha256')==artifacts.get('legacy') and
            isinstance(artifacts.get('legacy'),str) and len(artifacts['legacy'])==64 and
            legacy.get('all_emitted_legacy_callgraphs_closed') is True and
            stack.get('available_bytes')==352 and 0<stack.get('worst_total_bytes',0)<=352 and
            stack.get('guard_bytes')==16 and stack.get('top_gap_bytes')==32 and
            engines.get('x',{}).get('elf_sha256')==artifacts.get('resident') and
            engines.get('y',{}).get('elf_sha256')==artifacts.get('legacy') and
            engines.get('x',{}).get('sha256')!=engines.get('y',{}).get('sha256') and
            report.get('capability',{}).get('exactly_one_in_stage_and_module') is True,
            'dual pins require both exact embedded engines and the closed guarded legacy stack proof')
    return True


def legacy_audit(directory,*,self_test=False):
    from check_vmu_native_background import check as check_background
    reports=sorted(set((directory/'legacy').rglob('*.ci'))|set(directory.glob('resident-legacy.elf*.ci')))
    require(reports,'dual legacy image lacks emitted callgraphs')
    for report in reports:
        require(closed_callgraph(report.read_text()),'dual legacy emitted callgraph is truncated: '+str(report))
    with tempfile.TemporaryDirectory(prefix='kui-dual-legacy-audit-') as temporary:
        view=Path(temporary)
        for extension in ('elf','bin'):
            (view/('resident-sci.'+extension)).symlink_to((directory/('resident-legacy.'+extension)).resolve())
        for report in directory.glob('resident-legacy.elf*.ci'):
            (view/report.name.replace('resident-legacy','resident-sci',1)).symlink_to(report.resolve())
        (view/'resident').mkdir();(view/'resident/lto').symlink_to((directory/'legacy/lto').resolve())
        proof=check_background(view,self_test=self_test,require_stage=False)
    require_wide_legacy(proof,read_elf(directory/'resident-legacy.elf'))
    proof['all_emitted_legacy_callgraphs_closed']=True
    proof['closed_legacy_callgraph_files']=len(reports)
    return proof


def tail_contract(directory,stage,resident,worker,proof=None):
    from check_vmu_native_readahead_stage import check_stage,pin_stage
    from check_native_controller_failure_witness import witness_profile
    from check_native_game_startup_profile import game_startup_profile
    pins=pin_stage(stage,diagnostic=True,pio=True,pair=True,multi_owner=True,structural=True,dual=True,
                   witness=witness_profile(worker),
                   progress='__kui_native_cold_progress_stage' in stage[4],
                   owned_timers=game_startup_profile(worker,resident))
    if proof is None:
        proof=check_stage(stage,resident,read_elf(directory/'native-ra-aux.elf'),self_test=False,worker=worker)
    actual=proof.get('exact_instruction_literal_pins',{})
    require(proof.get('actual_linked_cold_binder_executed') is True and
            proof.get('actual_auxiliary_c_initializer_executed') is True and
            proof.get('dual_reader_stage') is True and proof.get('positive_cases',0)>0 and
            set(actual)==set(pins) and all(tuple(actual[name])==tuple(value) for name,value in pins.items()),
            'dual tail binder requires its exact actual cold SH/initializer companion proof')
    return dict(exact_cold_binder_pins=pins,actual_linked_ra_binder_and_initializer_executed=True,
        positive_cases=proof['positive_cases'],
        tail_target_model='first two binders are ABI adversaries including PR; final RA tail target retains the exactly pinned actual binder RTS/PR contract')


class ScalarOps:
    """Only scalar byte assembly operations; each extra form is truth-tested."""
    def plain(self,op,pc):
        n,m=op>>8&15,op>>4&15
        if op&0xf00f==0x6000:self.r[n]=signed(self.read(self.r[m],1),8)&MASK;return
        if op&0xf00f==0x600c:self.r[n]=self.r[m]&255;return
        if op&0xf00f==0x6008:
            value=self.r[m];self.r[n]=(value&0xffff0000)|((value&255)<<8)|((value>>8)&255);return
        if op>>8==0x84:self.r[0]=signed(self.read(self.r[m]+(op&15),1),8)&MASK;return
        if op>>8==0x80:self.write(self.r[m]+(op&15),self.r[0],1);return
        if op&0xf0ff==0x4028:self.r[n]=(self.r[n]<<16)&MASK;return
        super().plain(op,pc)


class BoundExecution:
    def set_code_bounds(self,images):
        self.code_bounds=[(image[4][name][0],image[4][name][0]+count)
                          for image,pins in images for name,(count,_) in pins.items()]
    def read(self,address,count=4):
        if count==2 and address==self.pc and hasattr(self,'code_bounds'):
            require(any(begin<=address<end for begin,end in self.code_bounds),
                    'dual instruction escaped reviewed functions: '+hex(address))
        return super().read(address,count)


class Stop(Exception):pass


class StageCPU(ScalarOps,BoundExecution,SDKCPU):
    def __init__(self,resident,stage,reader,transport,mask=15,bank=1):
        super().__init__(resident,seed=reader&255,mask=mask,bank=bank)
        self.memory=bytearray(self.memory)
        for kind,offset,address,_,count,_,_,_ in stage[2]:
            if kind==1 and count:self.store_block(address,stage[0][offset:offset+count])
        self.symbols=stage[4];self.stage=stage
        self.manifest=self.symbols['_manifest'][0]
        self.write(self.manifest+48,reader);self.write(self.manifest+44,transport)
        self.r[15]=0x8cfeffe0;self.pr=RETURN;self.stack_top=self.r[15]
        self.stop=self.symbols['_stopped'][0]
        self.binders={self.symbols[name][0]:name for name in
                      ('_native_loader_timeline_bind','_native_sdk_sci_route_bind','_native_readahead_bind')}
        self.targets={self.stop,*self.binders};self.bound_calls=[];self.binding=False
        self.forbid_manifest=False;self.manifest_reads=0;self.stop_detail=None
        self.set_code_bounds(((stage,STAGE_PINS),));self.writes=[]

    def read(self,address,count=4):
        if hasattr(self,'manifest') and self.manifest<=address<self.manifest+1020:
            require(not self.forbid_manifest,'bad capability was used to read a reader mode')
            self.manifest_reads+=1
        return super().read(address,count)

    def callee(self):
        if self.pc==self.stop:
            self.stop_detail=self.r[5];raise Stop()
        require(self.binding and self.pc in self.binders,'unadmitted dual stage indirect call')
        name=self.binders[self.pc];self.bound_calls.append(name);continuation=self.pr
        if name=='_native_loader_timeline_bind':
            assert self.r[4:8]==list(self.expected[:4])
            assert self.read(self.r[15])==self.expected[4]
        else:assert self.r[4]==self.expected[3]
        for i in range(8):self.r[i]=(0x81234567+i*0x13579bd+len(self.bound_calls))&MASK
        self.r[0]=0;self.sr^=1
        # The final call is a tail JMP after the helper restored its caller
        # PR. The separately executed RA binder returns with RTS and has an
        # explicit PR-preservation proof; retain that exact target property.
        if name!='_native_readahead_bind':self.pr^=0x2468ace0
        self.pc=continuation


def selector_cases(resident,stage):
    cases=refused=0
    for mask in range(16):
        for bank in (0,1):
            for reader in (0,1,2,3,255,256,MASK):
                for transport in (0,1,2,3,255,MASK):
                    c=StageCPU(resident,stage,reader,transport,mask,bank);before=c.snapshot()
                    valid=reader in (1,2) and transport==1
                    try:c.run_symbol('_select_resident')
                    except Stop:
                        assert not valid and c.stop_detail==reader;refused+=1
                    else:
                        assert valid;assert_c_abi(c,before)
                        prefix='__retail_resident_sci_blob_' if reader==1 else '__retail_resident_legacy_blob_'
                        begin,end=(stage[4][prefix+suffix][0] for suffix in ('start','end'))
                        assert c.read(stage[4]['_resident_blob'][0])==begin
                        assert c.read(stage[4]['_resident_bytes'][0])==end-begin
                        assert c.read(stage[4]['_resident_limit'][0])==0x8c007ba0
                    allowed={stage[4][name][0] for name in ('_resident_blob','_resident_bytes','_resident_limit')}
                    assert all(address in allowed or c.stack_top-256<=address<c.stack_top
                               for _,address,_,_ in c.writes)
                    assert not c.vbr_writes and not c.iprb_writes
                    cases+=1
    cap=stage[4]['_kui_native_dual_reader_capability'][0]
    for bit in range(256):
        c=StageCPU(resident,stage,1,1);c.write(cap+bit//8,c.read(cap+bit//8,1)^(1<<(bit%8)),1)
        c.forbid_manifest=True;c.manifest_reads=0;c.writes=[]
        try:c.run_symbol('_select_resident')
        except Stop:assert c.stop_detail==0 and not c.manifest_reads;refused+=1
        else:raise ValueError('linked dual selector accepted a changed capability')
        cases+=1
    return dict(actual_linked_selector_cases=cases,refusal_cases=refused,
        all_imasks_and_integer_banks=True,actual_capability_bit_mutations_before_mode_reads=256,
        standard_non_sci_unknown_modes_refused=True,exact_blob_and_guard_selected=True,
        only_selected_metadata_and_caller_stack_written=True)


def binding_cases(resident,stage,worker):
    cases=0
    for mask in range(16):
        for bank in (0,1):
            for reader in (0,1,2,3,255,MASK):
                for payload in (0x0c400000,0x8c400000,0xac400000):
                    for mode in (0,1,2,3,4,5,6):
                        c=StageCPU(resident,stage,reader,1,mask,bank);c.binding=True
                        dest=((payload&0x1fffffff)|0x80000000)+0x20000000
                        binary_bytes=max((p[3]&0x1fffffff)+p[4] for p in worker[2] if p[0]==1 and p[4])-0x0c100000
                        c.expected=(dest,dest,binary_bytes,payload,mode)
                        c.r[4:8]=list(c.expected[:4]);c.write(c.r[15],mode)
                        before=c.snapshot();c.writes=[]
                        c.run_symbol('_native_dual_reader_bind');assert_c_abi(c,before)
                        assert not c.vbr_writes and not c.iprb_writes
                        assert c.bound_calls==(list(c.binders.values()) if reader==1 else [])
                        assert all(c.stack_top-256<=address<c.stack_top for _,address,_,_ in c.writes)
                        cases+=1
    return dict(actual_linked_late_binding_cases=cases,x_calls_timeline_sdk_aux_in_order=True,
        y_standard_unknown_modes_do_not_call_any_binder=True,all_argument_values_and_fifth_stack_argument_checked=True,
        only_three_exact_typed_binder_targets_substituted=True,callee_c_abi_adversary=True,
        tail_ra_binder_pr_preservation_requires_actual_cold_binder_companion=True)


class DualWorkerCPU(ScalarOps,BoundExecution,WorkerCPU):
    def __init__(self,resident,worker,payload,event,variant,layout,pins=None):
        super().__init__(resident,worker,payload,event,variant,656)
        lease=(payload&0x1fffffff)|0x80000000;self.owned_map=lease+layout_for(worker[4]).MAP_OFFSET
        self.write(worker[4]['_config'][0]+layout['config_payload'],payload)
        self.write(self.region+layout['engine.manifest'],self.owned_map)
        self.write(self.owned_map+layout['manifest_reader'],1)
        self.write(self.owned_map+layout['manifest_transport'],1)
        self.write(self.region+0x530,0x53434931)
        self.forbid_aux=False;self.forbid_map=False;self.aux_reads=self.vector_reads=self.map_reads=0
        self.set_code_bounds(((worker,WORKER_PINS if pins is None else pins),));self.writes=[]

    def read(self,address,count=4):
        if hasattr(self,'forbid_aux'):
            if self.aux_header<=address<self.aux_header+4096:
                require(not self.forbid_aux,'Y/invalid mode dereferenced auxiliary memory');self.aux_reads+=1
            if self.region+0x500<=address<self.region+0x508:
                require(not self.forbid_aux,'Y/invalid mode interpreted raw vector words as auxiliary entries');self.vector_reads+=1
            if self.owned_map<=address<self.owned_map+1020:
                require(not self.forbid_map,'foreign manifest pointer was dereferenced');self.map_reads+=1
        return super().read(address,count)


class GeneralWorkerCPU(DualWorkerCPU):
    """The actual General cooperative service, including its inlined getter.

    Only its final auxiliary gateway is an ABI substitute. The three complete
    pinned functions execute; unknown code, dependent reads after a rejected
    payload, and writes outside stream history/private stack are refused.
    """
    def __init__(self,resident,worker,payload,variant,layout,pins):
        super().__init__(resident,worker,payload,2,variant,layout,pins)
        self.stream,self.stream_bytes,kind,owner=worker[4]['_reader_stream']
        require(kind==1 and self.stream_bytes==44 and owner<len(worker[3]) and
                worker[3][owner][1]==8 and worker[3][owner][2]&7==3,
                'General stream history must be its exact private44-byte BSS object')
        _,wire,_=symbol_bytes(worker,'_kui_vmu_native_worker_exports',1)
        fields=struct.unpack('<20I',wire)
        self.worker_begin=worker[4]['_kui_vmu_native_worker_exports'][0]&0x1fffffff
        self.worker_end=fields[11]&0x1fffffff
        self.stack_begin,self.stack_end=(v&0x1fffffff for v in fields[9:11])
        self.r[15]=(fields[10]&0x1fffffff)|0x80000000
        self.r[15]-=32
        self.no_dependent_reads=False
        self.busy_fields=tuple(self.durable[0]+layout[name] for name in
                              ('durable.handling','durable.inflight'))
        require(len(set(self.busy_fields))==2 and all(
                self.durable[0]<=v<=self.durable[0]+self.durable[1]-4 for v in self.busy_fields),
                'General durable busy fields escape their SH-compiled object layout')
        self.initial=self.snapshot();self.writes=[];self.durable_reads=set()

    def read(self,address,count=4):
        if getattr(self,'no_dependent_reads',False):
            at=address&0x1fffffff
            require(self.worker_begin<=at and at+count<=self.worker_end,
                    'Rejected General payload was used to read dependent memory')
        return super().read(address,count)

    def set_stream(self,*,last=0,primed=0,at=0,history=None):
        history=[0]*32 if history is None else history
        require(len(history)==32 and 0<=at<32 and all(0<=v<=255 for v in history),
                'General stream fixture is not a bounded32-frame history')
        self.write(self.stream,last);self.write(self.stream+4,sum(history))
        for index,value in enumerate(history):self.write(self.stream+8+index,value,1)
        self.write(self.stream+40,at,1);self.write(self.stream+41,primed,1)

    def set_blocks(self,layout,irq,call=0):
        self.write(self.region+layout['engine.stats.irq_blocks'],irq)
        self.write(self.region+layout['engine.stats.call_blocks'],call)

    def execute(self,name='_reader_cooperative_service'):
        before=self.snapshot();calls=self.calls;self.writes=[]
        self.run_symbol(name);assert_c_abi(self,before)
        require(not self.vbr_writes and not self.iprb_writes,
                'General cooperative service changed interrupt ownership')
        for _,address,_,count in self.writes:
            at=address&0x1fffffff;stream=self.stream&0x1fffffff
            require((self.stack_begin<=at and at+count<=self.stack_end) or
                    (stream<=at and at+count<=stream+self.stream_bytes),
                    'General cooperative service wrote outside its stream state/private stack')
        return self.calls-calls


def general_worker_cases(resident,worker,layout,*,expected_pins=None):
    """Execute General's real TICK path, never a synthesized capture callback."""
    from check_native_general_lean_profile import worker_profile,worker_pins
    require(worker_profile(worker),'General cooperative proof requires its complete marked profile')
    selected=worker_pins(worker,'dual',{})
    require(expected_pins is None or expected_pins==selected,
            'General read-ahead and dual companions selected different immutable worker functions')
    require(set(selected)=={'_reader_capture_region','_reader_cooperative_service','_reader_streaming'} and
            selected['_reader_capture_region'][0]==100 and
            selected['_reader_cooperative_service'][0]==248 and
            selected['_reader_streaming'][0]==120,
            'General cooperative service lacks its closed three-function instruction map')
    pinned=pin(worker,selected,'General cooperative worker')
    require(all(name not in worker[4] for name in
                ('_reader_readahead_exports','_reader_readahead_visit','_reader_capture_start')),
            'General proof cannot substitute a separately emitted getter or PROFILE capture path')
    g=layout_for(worker[4]);maximum=0x0d000000-g.LEASE_BYTES
    payloads=(0x0c400000,0x8c400000,0xac400000,maximum)
    variants=('valid','magic','version','bytes','image_low','image_high','entry_low','entry_high',
              'bound_entry','bound_context','context_low','context_at_public_limit','context_high')
    cases=refusals=stream_cases=busy_cases=abi_cases=0
    for payload in payloads:
        for variant in variants:
            c=GeneralWorkerCPU(resident,worker,payload,variant,layout,selected)
            called=c.execute();accepted=variant in ('valid','context_at_public_limit')
            assert called==int(accepted)
            refusals+=not accepted;cases+=1
        invalid=[('reader',v) for v in (0,2,3,255,256,MASK)]
        invalid += [('cookie',v) for v in (0,1,0x53434930,MASK)]
        owned=((payload&0x1fffffff)|0x80000000)+g.MAP_OFFSET
        invalid += [('map',v) for v in (0,1,owned+4,owned-4,owned&0x1fffffff,owned|0x20000000,MASK)]
        for kind,value in invalid:
            c=GeneralWorkerCPU(resident,worker,payload,'valid',layout,selected)
            if kind=='reader':c.write(c.owned_map+layout['manifest_reader'],value)
            elif kind=='cookie':c.write(c.region+0x530,value)
            else:c.write(c.region+layout['engine.manifest'],value);c.forbid_map=True
            c.forbid_aux=True
            assert c.execute()==0 and not c.aux_reads and not c.vector_reads
            cases+=1;refusals+=1
        for mask in range(16):
            for bank in (0,1):
                c=GeneralWorkerCPU(resident,worker,payload,'valid',layout,selected)
                c.set_sr((c.sr&~0x200000f0)|(bank<<29)|(mask<<4))
                assert c.execute()==1
                cases+=1;abi_cases+=1
        for handling,inflight in ((1,0),(0,1),(MASK,MASK)):
            c=GeneralWorkerCPU(resident,worker,payload,'valid',layout,selected)
            for address,value in zip(c.busy_fields,(handling,inflight)):c.write(address,value)
            c.forbid_aux=True
            assert c.execute()==0 and not c.aux_reads and not c.vector_reads
            assert c.durable_reads==set(c.busy_fields)
            cases+=1;busy_cases+=1;refusals+=1
        # Boundary, saturation, counter-reset and ring-wrap fixtures have
        # explicit expected results, independent of the compiled helper.
        fixtures=((0,0,0,0,0,0,0,1), (5000,0,0,1000,0,0,0,1), (0,1,0,96,0,96,96,1),
                  (0,1,0,97,0,97,97,0), (0,1,0,256,0,255,255,0),
                  (500,1,0,499,0,0,0,1), (MASK,1,0,0,0,0,0,1),
                  (0,1,0,48,48,96,96,1), (0,1,31,97,0,97,97,0))
        for last,primed,index,irq,call,delta,total,called in fixtures:
            c=GeneralWorkerCPU(resident,worker,payload,'valid',layout,selected)
            c.set_stream(last=last,primed=primed,at=index);c.set_blocks(layout,irq,call)
            assert c.execute()==called and c.read(c.stream+4)==total
            assert c.read(c.stream+8+index,1)==delta and c.read(c.stream+40,1)==(index+1)&31
            assert c.read(c.stream)==(irq+call)&MASK and c.read(c.stream+41,1)==1
            cases+=1;stream_cases+=1
        # A burst exceeding96 blocks suppressesTICK until that sample rolls
        # out of all32 controller frames, then normal prefetch resumes.
        c=GeneralWorkerCPU(resident,worker,payload,'valid',layout,selected)
        c.set_stream(last=0,primed=1);c.set_blocks(layout,128)
        assert c.execute()==0;cases+=1;stream_cases+=1
        for _ in range(31):
            assert c.execute()==0;cases+=1;stream_cases+=1
        assert c.execute()==1 and c.read(c.stream+4)==0
        cases+=1;stream_cases+=1
    invalid_payloads=(0,1,0x0c010000,maximum+32,0x8c400004,0x6c400000,
                      0xcc400000,0x8d000000,MASK)
    for payload in invalid_payloads:
        for name in ('_reader_capture_region','_reader_cooperative_service'):
            c=GeneralWorkerCPU(resident,worker,0x8c400000,'valid',layout,selected)
            c.write(worker[4]['_config'][0]+layout['config_payload'],payload)
            c.forbid_aux=c.forbid_map=c.no_dependent_reads=True
            assert c.execute(name)==0 and not c.aux_reads and not c.map_reads and not c.vector_reads
            if name=='_reader_capture_region':assert c.r[0]==0
            cases+=1;refusals+=1
    return dict(actual_linked_worker_cases=cases,actual_worker_binding_cases=cases,
        refusal_cases=refusals,modeled_worker_entry='_reader_cooperative_service',
        actual_stream_guard_cases=stream_cases,actual_busy_refusal_cases=busy_cases,
        actual_imask_and_bank_cases=abi_cases,all_three_lease_aliases_and_highest_lease=True,
        y_standard_unknown_modes_and_forged_cookie_cannot_read_aux_or_vector_entries=True,
        foreign_manifest_pointer_refused_before_map_dereference=True,
        null_region_and_invalid_config_payloads_refused_before_dependent_reads=True,
        malformed_aux_headers_and_public_context_bounds_refused=True,
        both_durable_writer_busy_fields_refused=True,actual_tick_arguments_checked=True,
        capture_start_not_present_in_general_profile=True,
        rolling_stream_threshold_saturation_reset_and_resume_checked=True,
        only_private_stream_history_and_caller_stack_written=True,c_abi_preserved=True,
        immutable_function_pins=pinned,
        c_auxiliary_target='ABI substitute; complete auxiliary gateway proved separately')


def worker_cases(resident,worker,layout):
    from check_native_general_lean_profile import worker_profile
    if worker_profile(worker):return general_worker_cases(resident,worker,layout)
    cases=0
    variants=('valid','magic','version','bytes','image_low','image_high','entry_low','entry_high',
              'bound_entry','bound_context','context_low','context_at_public_limit','context_high')
    for payload in (0x0c400000,0x8c400000,0xac400000,0x0cfa0000):
        for event in (2,6):
            for variant in variants:
                c=DualWorkerCPU(resident,worker,payload,event,variant,layout)
                before=c.snapshot();c.run_symbol('_reader_readahead_visit');assert_c_abi(c,before)
                assert not c.vbr_writes and not c.iprb_writes
                assert c.calls==int(variant in ('valid','context_at_public_limit'));cases+=1
            invalid=[('reader',value) for value in (0,2,3,255,256,MASK)]
            invalid += [('cookie',value) for value in (0,1,0x53434930,MASK)]
            lease=(payload&0x1fffffff)|0x80000000;owned=lease+layout_for(worker[4]).MAP_OFFSET
            invalid += [('map',value) for value in (0,1,owned+4,owned-4,owned&0x1fffffff,owned|0x20000000,MASK)]
            for kind,value in invalid:
                c=DualWorkerCPU(resident,worker,payload,event,'valid',layout)
                if kind=='reader':c.write(c.owned_map+layout['manifest_reader'],value)
                elif kind=='cookie':c.write(c.region+0x530,value)
                else:c.write(c.region+layout['engine.manifest'],value);c.forbid_map=True
                c.forbid_aux=True;before=c.snapshot();c.writes=[]
                c.run_symbol('_reader_readahead_visit');assert_c_abi(c,before)
                assert not c.vbr_writes and not c.iprb_writes
                assert not c.calls and not c.aux_reads and not c.vector_reads;cases+=1
            c=DualWorkerCPU(resident,worker,payload,event,'valid',layout)
            c.r[4]=0;c.forbid_aux=c.forbid_map=True;before=c.snapshot()
            c.run_symbol('_reader_readahead_exports');assert_c_abi(c,before)
            assert c.r[0]==0 and not c.aux_reads and not c.map_reads and not c.vector_reads;cases+=1
    for payload in (0,1,0x0c010000,0x0cfa0020,0x8c400004,0x6c400000,0xcc400000,0x8d000000,MASK):
        for event in (2,6):
            c=DualWorkerCPU(resident,worker,0x8c400000,event,'valid',layout)
            c.write(worker[4]['_config'][0]+layout['config_payload'],payload)
            c.forbid_aux=c.forbid_map=True;before=c.snapshot()
            c.run_symbol('_reader_readahead_visit');assert_c_abi(c,before)
            assert not c.calls and not c.aux_reads and not c.map_reads and not c.vector_reads;cases+=1
    return dict(actual_linked_worker_cases=cases,all_three_lease_aliases_and_highest_lease=True,
        y_standard_unknown_modes_and_forged_cookie_cannot_read_aux_or_vector_entries=True,
        foreign_manifest_pointer_refused_before_map_dereference=True,
        null_region_and_invalid_config_payloads_refused_before_dependent_reads=True,
        malformed_aux_headers_and_public_context_bounds_refused=True,heartbeat_and_capture_start_checked=True)


def instruction_truth():
    class TruthCPU(ScalarOps):
        def __init__(self):self.r=[0]*16;self.sr=0;self.memory={}
        def read(self,address,count=4):
            assert count==1;return self.memory[address]
        def write(self,address,value,count=4):
            assert count==1;self.memory[address]=value&255
    c=TruthCPU();cases=0;at=0x8c900040
    for n in range(16):
        for m in range(16):
            for value in (0,1,0x7f,0x80,0xff):
                c.r=[0x2468ace0+i for i in range(16)];c.r[m]=at;c.write(at,value,1);c.sr=0x400000f1
                c.plain(0x6000|(n<<8)|(m<<4),0);assert c.r[n]==(signed(value,8)&MASK) and c.sr==0x400000f1;cases+=1
                c.r[m]=value|0xabcdef00;c.sr=0x400000f0
                c.plain(0x600c|(n<<8)|(m<<4),0);assert c.r[n]==value and c.sr==0x400000f0;cases+=1
    for n in range(16):
        for m in range(16):
            for value in (0,1,0x80ff,0x10000,0x12345678,0x80000000,MASK):
                c.r[m]=value;c.sr=0x400000f1;c.plain(0x6008|(n<<8)|(m<<4),0)
                expected=int.from_bytes(value.to_bytes(4,'little')[1::-1]+value.to_bytes(4,'little')[2:],'little')
                assert c.r[n]==expected and c.sr==0x400000f1;cases+=1
    for m in range(16):
        for disp in range(16):
            for value in (0,1,0x7f,0x80,0xff):
                c.r[m]=at;c.write(at+disp,value,1);c.sr=0x400000f1
                c.plain(0x8400|(m<<4)|disp,0);assert c.r[0]==(signed(value,8)&MASK) and c.sr==0x400000f1;cases+=1
                # Source and base overlap when m==0. Set both explicitly,
                # then check the architectural byte store and neighbours.
                c.r[m]=at;c.r[0]=value
                target=c.r[m]+disp;c.memory[target-1]=0x5a;c.memory[target+1]=0xa5
                registers=c.r.copy();c.sr=0x400000f1
                c.plain(0x8000|(m<<4)|disp,0)
                assert c.memory[target]==value and c.memory[target-1]==0x5a and c.memory[target+1]==0xa5
                assert c.r==registers and c.sr==0x400000f1;cases+=1
    for n in range(16):
        for value in (0,1,0xffff,0x10000,0x80000000,MASK):
            c.r[n]=value;c.sr=0x400000f1;c.plain(0x4028|(n<<8),0)
            assert c.r[n]==(value<<16)&MASK and c.sr==0x400000f1;cases+=1
    return cases


def check(directory,*,self_test=False,cold_binder_proof=None,witness_proof=None):
    require(__debug__,'dual linked model requires Python assertions')
    directory=Path(directory)
    resident,legacy,stage,worker=(read_elf(directory/name) for name in
        ('resident-sci.elf','resident-legacy.elf','stage.elf','native-worker.elf'))
    markers(stage,worker)
    auxiliary=read_elf(directory/'native-ra-aux.elf')
    compact=compact_component_markers(stage,worker,resident,legacy,auxiliary)
    if self_test and compact.get('enabled'):
        compact['mutation_rejections']=compact_component_mutations(stage,worker,resident,legacy,auxiliary)
    from check_native_dual_callbacks import check as check_low_callbacks
    low_callbacks=check_low_callbacks(directory,self_test=self_test)
    low_artifacts={name:sha(image[0]) for name,image in
                   (('stage',stage),('resident',resident),('legacy',legacy))}
    low_callback_companion(low_artifacts,low_callbacks)
    callback_refusals=0
    if self_test:
        malformed=[None,{},dict(enabled=False)]
        for name in low_artifacts:
            changed=copy.deepcopy(low_callbacks);changed['artifact_sha256'][name]='0'*64;malformed.append(changed)
        for role,row in low_callbacks['expected_callback_roles'].items():
            for name in row:
                changed=copy.deepcopy(low_callbacks);del changed['expected_callback_roles'][role][name];malformed.append(changed)
        for category in ('selector','consumers'):
            for key,value in low_callbacks[category].items():
                changed=copy.deepcopy(low_callbacks)
                changed[category][key]=False if type(value) is bool else 0
                malformed.append(changed)
        for changed in malformed:
            try:low_callback_companion(low_artifacts,changed)
            except (ValueError,KeyError,TypeError):callback_refusals+=1
            else:raise ValueError('dual accepted an incomplete or stale low-callback companion')
    from check_native_game_startup_profile import game_startup_profile
    require(game_startup_profile(worker,resident)==game_startup_profile(worker,legacy), 'Dual low hooks use different owner-stack profiles')
    selected_stage_pins,selected_worker_pins=profile_pins(worker,stage)
    stage_pins=pin(stage,selected_stage_pins,'stage');worker_pins=pin(worker,selected_worker_pins,'worker')
    from check_native_controller_failure_witness import witness_profile,check as check_witness,witness_companion
    if witness_proof is None:
        witness_proof=check_witness(directory,self_test=self_test) if witness_profile(worker) else {'enabled':False}
    witness_companion(worker,{'controller_failure_witness':witness_proof})
    module=(directory/'retail-maple-observe.kui').read_bytes()
    cap=capability(stage,module);blobs=embedded_blobs(stage,resident,legacy,directory)
    geometry=layout_for(stage[4])
    layouts=console_layouts(general=geometry.WORKER_BYTES in (0x1c000,0x12000),
                            compact=geometry is BACKGROUND_COMPACT);layout=layouts['layouts']['x']
    tail=tail_contract(directory,stage,resident,worker,cold_binder_proof)
    selection=selector_cases(resident,stage);binding=binding_cases(resident,stage,worker)
    binding.update(tail)
    worker_proof=worker_cases(resident,worker,layout);truth=instruction_truth()
    legacy_proof=legacy_audit(directory,self_test=self_test)
    rejected=blob_refusals=cap_refusals=closure_refusals=guard_refusals=0
    if self_test:
        malformed=[]
        changed=copy.deepcopy(legacy_proof);changed['selected_guarded_lease_bytes']=0x30000
        changed['wide_lease_linked_guard_pins']={};malformed.append(changed)
        changed=copy.deepcopy(legacy_proof);changed['wide_lease_linked_guard_pins']['unknown']={};malformed.append(changed)
        for name in legacy_proof['wide_lease_linked_guard_pins']:
            for field,value in (('sha256','0'*64),('bytes',0)):
                changed=copy.deepcopy(legacy_proof);changed['wide_lease_linked_guard_pins'][name][field]=value
                malformed.append(changed)
            changed=copy.deepcopy(legacy_proof);del changed['wide_lease_linked_guard_pins'][name];malformed.append(changed)
        for changed in malformed:
            try:require_wide_legacy(changed,legacy)
            except ValueError:guard_refusals+=1
            else:raise ValueError('dual accepted absent/changed compiled384KiB legacy guards')
        for image,pins,label in ((stage,selected_stage_pins,'stage'),(worker,selected_worker_pins,'worker')):
            for name,(count,_) in pins.items():
                address,body,section=symbol_bytes(image,name,2);start=section[4]+address-section[3]
                for at in range(count):
                    altered=bytearray(image[0]);altered[start+at]^=1
                    try:pin((bytes(altered),*image[1:]),pins,label)
                    except ValueError:rejected+=1
                    else:raise ValueError('dual accepted a changed reviewed function byte')
        for image,role in ((stage,'stage'),(worker,'worker')):
            for replacement in (None,(0,0,0,0xfff1),(2,0,0,0xfff1),(1,4,1,1),(1,0,0,1)):
                symbols=dict(image[4])
                if replacement is None:symbols.pop(MARK+role)
                else:symbols[MARK+role]=replacement
                changed=(*image[:4],symbols)
                try:markers(changed,worker) if role=='stage' else markers(stage,changed)
                except ValueError:rejected+=1
                else:raise ValueError('dual accepted a changed profile marker')
        for prefix in ('__retail_resident_sci_blob_','__retail_resident_legacy_blob_'):
            begin,end=(stage[4][prefix+suffix][0] for suffix in ('start','end'))
            section=next(section for section in stage[3] if section[1]==1 and section[2]&2 and
                         section[3]<=begin<end<=section[3]+section[5])
            start=section[4]+begin-section[3]
            for at in sorted({0,(end-begin)//2,end-begin-1}):
                changed=bytearray(stage[0]);changed[start+at]^=1
                try:embedded_blobs((bytes(changed),*stage[1:]),resident,legacy,directory)
                except ValueError:blob_refusals+=1
                else:raise ValueError('dual accepted changed embedded low image bytes')
        capability_bytes=struct.pack('<8I',0x3152444b,0x4c415544,1,32,1,2,layout_for(stage[4]).LEASE_BYTES,0)
        offset=module.index(capability_bytes)
        changed=bytearray(module);changed[offset]^=1
        for candidate in (bytes(changed),module+capability_bytes):
            try:capability(stage,candidate)
            except ValueError:cap_refusals+=1
            else:raise ValueError('dual accepted an absent or duplicated module capability')
        cap_address,cap_body,cap_section=symbol_bytes(stage,'_kui_native_dual_reader_capability',1)
        changed=bytearray(stage[0]);changed[cap_section[4]+cap_address-cap_section[3]]^=1
        try:capability((bytes(changed),*stage[1:]),module)
        except ValueError:cap_refusals+=1
        else:raise ValueError('dual accepted a changed typed stage capability')
        reports=sorted(set((directory/'legacy').rglob('*.ci'))|set(directory.glob('resident-legacy.elf*.ci')))
        for report in reports:
            text=report.read_text()
            for changed in (text[:text.rfind('}')],text+'node: {}'):
                require(not closed_callgraph(changed),'dual accepts an incomplete legacy callgraph')
                closure_refusals+=1
    return dict(schema='kui.native.dual-readers.audit.v1',enabled=True,
        compact_background=compact,
        artifact_sha256={name:sha(image[0]) for name,image in
                         (('resident',resident),('legacy',legacy),('stage',stage),('worker',worker))},
        capability=cap,embedded_engines=blobs,immutable_stage_pins=stage_pins,immutable_worker_pins=worker_pins,
        controller_failure_witness=witness_proof,
        selected_low_callbacks=low_callbacks,
        selected_low_callback_companion_mutation_rejections=callback_refusals,
        console_layout=layouts,selector=selection,late_binding=binding,worker=worker_proof,
        additional_scalar_instruction_truth_cases=truth,legacy_background=legacy_proof,
        instruction_and_marker_mutation_rejections=rejected,
        embedded_blob_mutation_rejections=blob_refusals,capability_mutation_rejections=cap_refusals,
        legacy_callgraph_closure_mutation_rejections=closure_refusals,
        legacy_guard_companion_mutation_rejections=guard_refusals,
        console_compatibility_verified=False,console_performance_verified=False)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
    p.add_argument('--self-test',action='store_true');a=p.parse_args()
    try:result=check(a.directory,self_test=a.self_test)
    except (OSError,ValueError,AssertionError,KeyError,struct.error) as e:
        p.exit(2,'Dual native reader audit refused: '+str(e)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
