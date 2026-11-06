#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused qualification of the normal unified Smash collection delivery.

Four independent cold modules, two matched CE helpers and their explicitly
selected native/CE return contracts are qualified against the linked cohort.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf
from runtime_package import flatten_elf,verify
from check_cold_load_performance import audit as cold_audit,SOURCES as COLD_SOURCES
from test_smash_menu_loader_runtime import exercise as forward_case,exercise_redirect
from test_smash_ce_child_gin import audit as original_audit
from test_smash_ce_return_probe import audit as probe_audit
from test_smash_ce_terminal_entry import audit as terminal_audit
from test_smash_ce_helper import audit as embedded_helper_audit
from smash_ce_profile import profile as ce_profile
from check_smash_helper_body import validate as helper_body, REFERENCE_BUILD, PINS as HELPER_PINS
from check_retail_stack import load as graph_load,base as function_base,LIBRARY
from check_smash_accepted_witness import audit as accepted_audit,ORIGINAL_MODELS
from test_smash_ce_gin_adoption import audit as gin_adoption_audit
from test_smash_ce_gin_resume import audit as gin_resume_audit
from test_smash_ce_gin_handoff import audit as gin_handoff_audit
from test_smash_phase_clock_linked import audit as phase_clock_audit
from test_native_vmu_stage_read_cache_linked import audit as native_read_cache_audit
from test_retail_compact_memcpy import audit as compact_memcpy_audit
from test_native_stage_cached_crc import audit as cached_boot_crc_audit

TEXT_SOURCES=('include/kui/smash_phase_text.h','src/core/smash_phase_text.c',
 'include/kui/smash_phase_file.h','src/loader/smash_phase_file.c','src/loader/smash_phase_string.c',
 'src/loader/smash_phase_stage.inc','include/kui/smash_phase_trace.h','src/core/smash_phase_trace.c',
 'src/apps/smash_phase_text_prepare.inc','Makefile.smash-phase-text-tests',
 'Makefile.native-vmu-live-fat-memo-tests','tests/test_native_vmu_live_fat_memo.c',
 'Makefile.smash-phase-text-prepare-tests','tests/test_smash_phase_text.c',
 'tests/test_smash_phase_text_prepare.c','tests/test_smash_phase_file.c','tests/test_smash_phase_file.py',
 'tests/test_native_vmu_stage_read_cache.c','tests/test_native_vmu_stage_read_cache.py',
 'tests/test_native_vmu_stage_read_cache_linked.py')

SOURCES=(
 'Makefile.smash-vc2-child','Makefile.smash-swirl-child','Makefile.smash-ce-return-probe',
 'Makefile.vc2-ce-vmu','include/kui/smash_menu_collection.h','src/core/smash_menu_collection.c',
 'include/kui/smash_ce_child.h','src/core/smash_ce_child.c','src/loader/smash_ce_child_stage.inc',
 'include/kui/smash_menu_loader_runtime.h','include/kui/smash_menu_loader_capability.h',
 'src/loader/smash_menu_loader_stage.inc','include/kui/smash_ce_return.h',
 'include/kui/smash_ce_return_probe.h','src/core/smash_ce_return_probe.c',
 'src/loader/smash_ce_return_probe.c','src/loader/smash_ce_return_probe_entry.S',
 'src/loader/smash_ce_return_probe.ld','src/loader/retail_resident.S',
 'tools/smash_ce_return_probe_symbols.py','tools/smash_menu_loader_symbols.py',
 'include/kui/smash_ce_profile.h','src/core/smash_ce_profile.c','src/loader/smash_ce_profile_blob.S',
 'include/kui/smash_ce_helper.h','src/core/smash_ce_helper.c','tests/test_smash_ce_helper.c','tests/test_smash_ce_helper.py',
 'tools/smash_ce_profile.py','tests/test_smash_ce_core.c','tests/test_smash_ce_core.py',
 'tests/test_smash_ce_child_gin.py','tests/test_smash_ce_return_probe.py','tests/test_smash_ce_terminal_entry.py',
 'tools/check_smash_collection_forward.py','tools/check_smash_accepted_witness.py',
 'tests/test_smash_cold_image_handoff.py','tests/test_smash_full_source_scope.py',
 'tests/test_smash_native_cached_replay.py','tools/check_smash_helper_body.py',
 'tests/test_cold_image.c','tests/test_smash_complete_source_owner.c',
 'src/core/cold_image.c','include/kui/cold_image.h','src/loader/smash_source_owner_stage.inc',
 'src/loader/smash_native_preload_stage.inc','include/kui/smash_ce_gin.h',
 'src/core/smash_ce_gin.c','src/loader/smash_ce_gin_stage.inc','src/loader/smash_ce_gin_resume.S',
 'tests/test_smash_ce_gin.c','tests/test_smash_ce_gin_adoption.py',
 'tests/test_smash_ce_gin_resume.py','tests/test_smash_ce_gin_handoff.py',
 'tests/test_smash_ce_gin_display.py','src/loader/retail_display.c','src/loader/retail_display.h',
 'Makefile.smash-menu-loader','src/loader/minic.c','src/loader/retail_compact_memcpy.S',
 'tests/test_retail_compact_memcpy.py','tests/test_native_stage_cached_crc.py',
 'src/loader/native_proof_cache_scope.inc')
def sha(b):return hashlib.sha256(b).hexdigest()
def require(ok,message):
    if not ok:raise ValueError(message)
def embedded(image,first,last):
    a,b=image[4][first][0],image[4][last][0]
    for p in image[2]:
        if p[0]==1 and p[2]<=a<=b<=p[2]+p[4]:return image[0][p[1]+a-p[2]:p[1]+b-p[2]]
    raise ValueError('Embedded blob escapes load bytes: '+first)
def physical_image(image):
    rows=sorted((p[3],p[1],p[4]) for p in image[2] if p[0]==1 and p[4])
    require(rows,'ELF has no initialized physical image')
    begin=rows[0][0];end=max(a+n for a,_,n in rows)
    require(0<end-begin<=0x200000,'Physical image exceeds bounded cold image')
    out=bytearray(end-begin);prior=begin
    for at,offset,n in rows:
        require(at>=prior,'Initialized physical load segments overlap')
        out[at-begin:at-begin+n]=image[0][offset:offset+n];prior=at+n
    return bytes(out)
def object_bytes(image,name):
    a,n,_,_=image[4][name]
    require(n,'Named object has no bound size: '+name)
    for p in image[2]:
        if p[0]==1 and p[2]<=a<a+n<=p[2]+p[4]:return image[0][p[1]+a-p[2]:p[1]+a+n-p[2]]
    raise ValueError('Named object escapes initialized load bytes: '+name)

CE_CONTROLLER_MARKER='__kui_smash_ce_return_controller'
CE_TRANSPORT_MARKER='__kui_smash_ce_return_transport'
CE_RAM_CLEAR_MARKER='__kui_smash_ce_return_ram_clear'
CE_TRACE_MARKER='__kui_smash_ce_return_trace_revision'
CE_RAW_SHADOW_MARKER='__kui_smash_ce_return_raw_shadow'
CE_RAW_IMAGE_MARKER='__kui_smash_ce_return_raw_image'
CE_CRC_BITWISE_MARKER='__kui_smash_ce_return_crc32_bitwise'
CE_STARTUP_ABI_MARKER='__kui_smash_ce_return_startup_abi'
NATIVE_OPTIONS_MARKER='__kui_smash_native_original_options'
NATIVE_DIRECT_MARKER='__kui_smash_native_combo_direct_outer'
CE_RETURN_FEATURES=(CE_RAM_CLEAR_MARKER,CE_TRACE_MARKER,CE_RAW_SHADOW_MARKER,CE_RAW_IMAGE_MARKER,
    CE_CRC_BITWISE_MARKER,CE_STARTUP_ABI_MARKER)
CE_RAW_IMAGE_SHA256='e6d55610190b00eab227b25a73289a13aab86a8194a314571022cfdda102e9cc'
CE_CONTROLLER_ONE=(1,0,0,0xfff1)
CE_CONTROLLER_CAP=struct.pack('<4I',0x5349554b,0x31524543,1,16)
def current_execution_sources(proof,script,required=()):
    sources=proof.get('source_sha256',{})
    require(proof.get('test_sha256')==sha((ROOT/script).read_bytes()) and
        isinstance(sources,dict) and script in sources and all(path in sources for path in required) and
        all((ROOT/path).is_file() and sha((ROOT/path).read_bytes())==digest for path,digest in sources.items()),
        'CE execution proof has stale or missing test/model/production source bindings: '+script)
def external_or_run(proof,script,args):
    if proof:return json.loads(Path(proof).read_text())
    with tempfile.TemporaryDirectory(prefix='kui-session-qualification-') as directory:
        path=Path(directory)/'proof.json'
        subprocess.run([sys.executable,str(ROOT/script),*map(str,args),'--report',str(path)],
            cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
        return json.loads(path.read_text())
def ce_feature_cohort(controllers):
    for marker in CE_RETURN_FEATURES:
        features=[image[4].get(marker) for image in controllers]
        require(all(value in (None,CE_CONTROLLER_ONE) for value in features) and len(set(features))==1,
            'CE return requires matching exact child capabilities: '+marker)
    require(all(not (image[4].get(CE_RAW_IMAGE_MARKER) and image[4].get(CE_RAW_SHADOW_MARKER))
        for image in controllers),'CE canonical raw image and protected raw shadow cannot be combined')
    require(all(image[4].get(CE_CRC_BITWISE_MARKER)==image[4].get(CE_STARTUP_ABI_MARKER)
        for image in controllers),'CE bounded bitwise CRC and accepted startup ABI must be selected together')
def native_original_options(stage):
    options,direct=(stage[4].get(marker) for marker in (NATIVE_OPTIONS_MARKER,NATIVE_DIRECT_MARKER))
    require(options in (None,CE_CONTROLLER_ONE) and direct in (None,CE_CONTROLLER_ONE) and
        (options==CE_CONTROLLER_ONE)!=(direct==CE_CONTROLLER_ONE),
        'Native return requires exactly one known original-options or historical direct-outer capability')
    return options==CE_CONTROLLER_ONE
def native_options_contract(folder,originals,proof=None):
    script='tests/test_smash_native_options_return.py';original=Path(originals)/'MDE_US.BIN'
    report=external_or_run(proof,script,['--stage',folder/'stage.elf','--original-mde',original])
    owner='04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'
    require(len(original.read_bytes())==1848288 and sha(original.read_bytes())==owner and
        report.get('schema')=='kui.smash-native.options-return.v1' and report.get('status')=='PASS' and
        report.get('native_options_menu_preserved') is True and report.get('original_mde_sha256')==owner and
        report.get('stage_sha256')==sha((folder/'stage.elf').read_bytes()) and
        {str(Path(p).resolve()):value for p,value in report.get('inputs',{}).items()}==
        {str((folder/name).resolve()):sha((folder/name).read_bytes()) for name in
            ('stage.elf','return-loader.elf','return-loader.bin','smash-return-raw.bin',
             'native-worker.elf','native-worker.bin','native-worker.reloc')},
        'Native original-options proof has stale or incomplete source/artifact bindings')
    current_execution_sources(report,script,('tests/sh4_integer_fast.c','tests/test_smash_native_gin_linked.py',
        'tests/test_smash_menu_return_runtime.py','src/loader/smash_native_return_stage.inc','Makefile.smash-native-menu'))
    stage=read_elf(folder/'stage.elf')
    require(native_original_options(stage) and not any(name in stage[4] for name in
        (NATIVE_DIRECT_MARKER,'_kui_smash_native_combo_plan','_smash_return_combo_publish')),
        'Native original-options profile still links the old combo hijack')
    linked=report.get('linked',{})
    require(all(linked.get(key) is True for key in ('native_options_menu_preserved',
        'only_original_reset_patched','actual_low_reader_code_runtime_exact','full596_runtime_crc_verified',
        'actual72KiB_worker_bound','private_stack_zero')) and
        linked.get('reset_patch_address')=='0x8c0117f0' and linked.get('reset_patch_bytes')==12 and
        linked.get('original_combo_branch')=='0xa1f8' and
        all(linked.get(key,0)>0 for key in ('prepare_instructions','publication_instructions','worker_bind_instructions')),
        'Native original-options proof omits reset-only publication, exact code/runtime or worker binding')
    refusals=linked.get('refusals',[])
    require(len(refusals)==2 and {row.get('fault') for row in refusals}=={'reset_source_crc','return_seed_crc'} and
        all(row.get('no_publication') is True and row.get('actual_instructions',0)>0 for row in refusals),
        'Native original-options proof omits refusal before source/seed publication')
    rows=report.get('cases',[])
    held_values={hex(sum(bit for i,bit in enumerate((2,4,0x200,0x400)) if mask>>i&1)) for mask in range(16)}
    require(len(rows)==64 and {(row.get('held'),row.get('start_edge'),row.get('secondary')) for row in rows}==
        {(held,edge,secondary) for held in held_values for edge in (False,True) for secondary in (False,True)},
        'Native original-options proof omits button masks, Start edges or controller ports')
    for row in rows:
        combo=row['held']=='0x606' and row['start_edge']
        require(row.get('flag')==int(combo) and row.get('low_return_gate_writes')==0 and
            row.get('actual_source_instructions',0)>0 and row.get('controller_state_boundary_ports')==[0,6],
            'Native original-options proof changes the original combo predicate or enters low code before menu selection')
        if combo:
            route=row.get('original_options_return',{})
            require(route.get('original_options_entry')=='0x8c01adb0' and
                route.get('explicit_Return_reaches_reset')=='0x8c0117f0' and
                route.get('dirty_save_confirm_preserved') is True and route.get('low_entry')=='0xac008300' and
                route.get('authored_UI_SDK_boundaries')==['0x8c1a2130','0x8c1a1c4c',
                    '0x8c010a40','0x8c1a2130','0x8c1a1c4c','0x8c01834c'],
                'Native combo bypasses original Options, explicit Return or dirty-save confirmation')
        else:
            require(row.get('original_noncombo_fallthrough_preserved') is True,
                'Native original-options proof changes a noncombo game-loop path')
    return report
def genesis_options_contract(originals,proof=None):
    script='tests/test_smash_genesis_options_source.py';original=Path(originals)/'MDE_US.BIN'
    report=external_or_run(proof,script,['--original-mde',original])
    source=original.read_bytes();owner='04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'
    require(len(source)==1848288 and sha(source)==owner and
        report.get('schema')=='kui.smash.genesis-options-source.v1' and report.get('status')=='PASS' and
        report.get('original_mde_sha256')==owner and report.get('inputs_immutable_during_execution') is True and
        {str(Path(p).resolve()):value for p,value in report.get('inputs',{}).items()}=={str(original.resolve()):owner},
        'Original Genesis Options source proof has stale or incomplete input bindings')
    current_execution_sources(report,script,('tests/sh4_integer_fast.c','tests/test_smash_native_gin_linked.py'))
    require(all(report.get('checks',{}).get(key) is True for key in
        ('original_combo_reaches_options_before_reset','original_six_item_menu_includes_save_and_return',
         'save_SRAM_and_dirty_gates_exact','save_selection_rechecked','save_handler_returns_to_options',
         'explicit_return_reaches_original_reset','dirty_return_runs_original_confirmation_wrapper',
         'confirmed_save_precedes_reset','old_hijack_skips_options','original_input_unchanged')) and
        report.get('menu_descriptor')=='0x8c15ed3c' and report.get('menu_items')==
            ['Start Game','Resume Game','Controls','Save Game','Show Credits','Return to Menu'],
        'Original Genesis Options proof omits the original menu, Save eligibility or explicit Return route')
    windows=[(0x8c010480,18),(0x8c01086c,42),(0x8c01adb0,60),(0x8c01ae32,116),
        (0x8c01aeaa,44),(0x8c0103a0,32),(0x8c010292,12),(0x8c01834c,22)]
    require(report.get('source_windows')==[dict(address=hex(address),bytes=count,
        sha256=sha(source[address-0x8c010000:address-0x8c010000+count])) for address,count in windows],
        'Original Genesis Options proof names different branch, dispatch, confirmation or reset-tail bytes')
    saves=report.get('save_cases',[])
    require(len(saves)==4 and {(row.get('SRAM_bytes'),row.get('dirty')) for row in saves}==
        {(size,dirty) for size in (0,8192) for dirty in (0,1)},
        'Original Genesis Save proof omits SRAM-present/absent or dirty/clean cases')
    for row in saves:
        available=bool(row['SRAM_bytes'] and row['dirty']);combo=row.get('combo',{})
        require(row.get('selected_index')==3 and row.get('selected_label')=='Save Game' and
            row.get('save_disabled') is (not available) and row.get('original_save_handler_reached') is available and
            row.get('save_handler')=='0x8c018362' and row.get('save_return')==('0x8c01ae9e' if available else None) and
            row.get('menu_reentered') is True and row.get('menu_before_any_reset') is True and
            row.get('reset_reached') is False and row.get('save_backend_executed') is False and
            row.get('gate_instructions',0)>0 and row.get('dispatch_instructions',0)>0 and
            combo.get('executed_branch')=='0xa1f8' and combo.get('reached')=='0x8c01adb0' and
            combo.get('menu_reached_before_reset') is True and combo.get('low_return_gate_writes')==0,
            'Original Genesis Save proof skips the Options menu, changes its gate or claims save-backend execution')
    returns=report.get('return_cases',[])
    require(len(returns)==3 and {(row.get('dirty'),row.get('confirmation_answer')) for row in returns}==
        {(0,None),(1,0),(1,1)},'Original Genesis Return proof omits clean, declined-save or confirmed-save cases')
    for row in returns:
        dirty=bool(row['dirty']);save=dirty and row['confirmation_answer']==1
        events=['options','Return_to_Menu']+(['original_save_confirmation'] if dirty else [])+\
            (['original_save_handler'] if save else [])+['original_reset_gate']
        require(row.get('selected_index')==5 and row.get('selected_label')=='Return to Menu' and
            row.get('reset_tail')=='0x8c010292' and row.get('reset_entry')=='0x8c0117f0' and
            row.get('original_reset_gate_reached') is True and row.get('menu_before_any_reset') is True and
            row.get('dirty_save_confirmation_reached') is dirty and
            row.get('actual_confirmation_wrapper_executed') is dirty and
            row.get('original_save_handler_reached') is save and row.get('save_backend_executed') is False and
            row.get('dispatch_instructions',0)>0 and row.get('events')==events,
            'Original Genesis Return proof bypasses menu selection, save confirmation or the original reset tail')
    negative=report.get('old_hijack_negative',{})
    require(negative.get('executed_branch')=='0xa1f4' and negative.get('reached')=='0x8c0117f0' and
        negative.get('reset_before_options') is True and negative.get('menu_reached_before_reset') is False and
        negative.get('low_return_gate_writes')==0 and negative.get('post_loop_instructions',0)>0,
        'Original Genesis Options proof omits the old direct-outer hijack negative control')
    return report
def ce_ram_clear_contract(folder,gate,proof=None,originals=None):
    script='tests/test_smash_ce_return_ram_clear.py'
    require(proof or originals,'CE RAM clear needs its exact original MAIGO/SAMPLE source proof')
    source=Path(originals)/'SWIRL' if originals else None
    args=['--elf',folder/'ce-return.elf']
    if source:args+=['--maigo',source/'MAIGO.BIN','--sample',source/'SAMPLE.BIN']
    report=external_or_run(proof,script,args)
    require(report.get('schema')=='kui.smash.ce.return.ram-clear.v1' and report.get('status')=='PASS' and
        report.get('elf_sha256')==sha((folder/'ce-return.elf').read_bytes()) and
        report.get('inputs_immutable_during_execution') is True,
        'CE RAM-clear proof is stale or incomplete')
    current_execution_sources(report,script,('src/loader/smash_ce_return_ram_clear.S',
        'src/loader/smash_ce_return_loader.c','include/kui/smash_ce_return_loader.h',
        'Makefile.smash-ce-return-probe','tools/check_vmu_native_worker_gateway.py'))
    pins={'MAIGO.BIN':'55d89bfb608806138f5c675ba2187a99a0b4e2f11f7f65048c3f406b8993826a',
        'SAMPLE.BIN':'82bb1160814548f4656b273353fd6168a691fb5f215b77377860ff9fd2156c8a'}
    original=report.get('original_source',{});leaf=report.get('linked_leaf',{})
    require(report.get('original_sources')==pins and original.get('status')=='PASS' and
        original.get('input_sha256')==pins and
        [(row.get('begin'),row.get('end_exclusive')) for row in original.get('intervals',[])]==
            [('0x8c010000','0x8d000000'),('0x8c010000','0x8ce00000')],
        'CE RAM-clear proof omits the exact original return/SAMPLE clear intervals')
    require(leaf.get('status')=='PASS' and leaf.get('elf_sha256')==report['elf_sha256'] and
        report.get('leaf_sha256')==leaf.get('leaf_sha256') and
        leaf.get('begin')=='0xac010000' and leaf.get('end_exclusive')=='0xad000000' and
        leaf.get('bytes')==0xff0000 and leaf.get('stores')==0xff0000//4 and
        set(leaf.get('mutation_refusals',[]))=={'begin_literal','end_literal','store_opcode'} and
        len(leaf.get('native_executions',[]))==2 and
        all(row.get('stores')==0xff0000//4 and not row.get('error') and
            all(row.get(key)==1 for key in ('low_ok','high_ok','cleared','preserved','upper_dirty_before')) and
            not row.get('mmio_reads') and not row.get('mmio_writes') for row in leaf['native_executions']),
        'CE RAM-clear proof omits complete linked stores, state preservation or boundary canaries')
    successes=[row for row in gate.get('cases',[]) if row.get('outcome')=='outer_entry']
    require(gate.get('ram_clear_effect_boundary') is True and
        gate.get('ram_clear_independent_linked_proof')==leaf and successes and
        all(row.get('ram_clear_calls')==1 and row.get('ram_clear_before_raw') is True for row in successes),
        'CE controller proof omits terminal RAM-clear ordering or uses a different leaf proof')
    return report
def ce_fault_chain_contract(folder,kind,proof=None):
    script='tests/test_smash_ce_return_fault_chain.py'
    report=external_or_run(proof,script,['--build',folder,'--profile',kind])
    current=report.get('current',{})
    require(report.get('schema')=='kui.smash-ce.return-fault-chain.v1' and report.get('status')=='PASS' and
        report.get('profile')==kind and
        {str(Path(p).resolve()):v for p,v in current.get('inputs',{}).items()}==
        {str((folder/p).resolve()):sha((folder/p).read_bytes()) for p in
            ('ce-return.elf','helper/helper.elf','helper/helper.bin','resident-sci.elf')} and
        current.get('trace_revision')==1 and current.get('reason_family')=='0xce010000' and
        current.get('integrity_mask') is True and current.get('zero_detail_from_reason4_possible') is False,
        'CE revision1 failure-chain proof is stale or incomplete')
    current_execution_sources(report,script,('tests/test_smash_ce_return_loader.py',
        'tests/sh4_integer_fast.c','src/loader/smash_ce_return_loader.c',
        'src/loader/smash_return_fault_display.c'))
    raw_detail=0xc00 if read_elf(folder/'ce-return.elf')[4].get(CE_RAW_IMAGE_MARKER)==CE_CONTROLLER_ONE else 0x800
    expected={('bad_runtime',False):0x400,('bad_raw',False):raw_detail,
        ('bad_helper',False):0x1000,('bad_receipt',False):0x500,('bad_raw',True):raw_detail}
    rows=current.get('cases',[])
    require(len(rows)==len(expected) and {(row.get('case'),row.get('interlaced')) for row in rows}==set(expected),
        'CE revision1 failure-chain proof omits linked failure or interlaced cases')
    for row in rows:
        detail=expected[(row['case'],row['interlaced'])]
        require(row.get('profile')==kind and row.get('trace_revision')==1 and row.get('integrity_mask') is True and
            row.get('call_arguments')==[{'boundary':'stopped','reason':'0x4','detail':hex(detail)},
                {'boundary':'renderer','reason':'0xce010004','detail':hex(detail)}] and
            row.get('decoded_fields')==[['CE010004',f'{detail:08X}']]*(2 if row['interlaced'] else 1) and
            all(row.get(key) is True for key in ('all16_hex_digits_checked',
                'all_glyph_pixels_and_replication_checked','no_retirement_or_raw_reads')) and
            row.get('actual_instructions',0)>0 and row.get('fast_actual_CRC_instructions',0)>0 and
            row.get('pixel_writes',0)>0,
            'CE revision1 failure-chain proof omits actual stopped/renderer arguments or displayed digits')
    return report
def ce_raw_shadow_contract(folder,kind,gate,late,proof=None):
    script='tests/test_smash_ce_return_raw_shadow.py'
    report=external_or_run(proof,script,['--build',folder,'--profile',kind])
    require(report.get('schema')=='kui.smash-ce.return-raw-shadow.v1' and report.get('status')=='PASS' and
        report.get('profile')==kind and report.get('inputs_immutable_during_execution') is True and
        {str(Path(p).resolve()):v for p,v in report.get('inputs',{}).items()}==
        {str((folder/p).resolve()):sha((folder/p).read_bytes()) for p in
            ('ce-return.elf','ce-return.bin','helper/helper.elf','helper/helper.bin',
             'resident-sci.elf','stage.elf','helper/prepared.bin','vc2-raw.bin')},
        'CE protected raw-copy proof is stale or incomplete')
    current_execution_sources(report,script,('tests/test_smash_ce_return_loader.py',
        'tests/test_smash_ce_return_late.py','tests/sh4_integer_fast.c',
        'src/loader/smash_ce_return_copy.S','src/loader/smash_ce_return_loader.c',
        'src/loader/smash_ce_return_loader.ld','src/loader/ce_return_crc32.h',
        'src/loader/cold_load_crc32.c','src/core/smash_ce_return_loader.c',
        'tools/check_vmu_native_worker_gateway.py','tools/smash_ce_return_view.py'))
    checks=report.get('checks',{})
    require(all(checks.get(key) is True for key in ('capture_full512_authenticated_before_e0',
        'capture_word_readback_before_e0','capture_source_and_padding_crc_refusal',
        'protected_crc_refusal_before_retirement','changed_live_requires_terminal_restore',
        'restore_after_quiesce_cache_off_mmu_off','destination_full512_crc_before_execution',
        'failed_destination_crc_refuses_execution','sealed_expected_crc_never_refreshed',
        'no_worker_or_stack_reservation_growth')),
        'CE protected raw-copy proof omits capture authority, terminal restore or immutable expected CRC')
    raw=(folder/'vc2-raw.bin').read_bytes()
    require(len(raw)==512 and report.get('raw_sha256')==sha(raw) and
        report.get('raw_crc32')==f'{zlib.crc32(raw)&0xffffffff:08x}' and
        report.get('raw_shadow_interval')==['0xac00b400','0xac00b600'],
        'CE protected raw-copy proof names another reader or protected interval')
    expected={'dirty_alias_copy_and_clear':0,'copy_readback_fault':-1,'changed_e0_owner':-1,
        'raw_shadow_readback_fault':-1,'raw_source_crc_fault':-1,'raw_padding_crc_fault':-1}
    cases=late.get('cases',[])
    require(late.get('protected_raw_shadow') is True and
        late.get('raw_shadow_interval')==['0xac00b400','0xac00b600'] and
        len(cases)==len(expected) and {row.get('case'):row.get('result') for row in cases}==expected and
        all(row.get('protected_raw_shadow') is True and row.get('raw_helper_unchanged') is True and
            row.get('caller_nonvolatile_state_exact') is True for row in cases) and
        next(row for row in cases if row['case']=='dirty_alias_copy_and_clear').get('raw_shadow_exact') is True,
        'CE late installation proof omits protected512 capture, source/padding CRC or readback refusals')
    capture=report.get('capture',{})
    require(capture.get('status')=='PASS' and capture.get('protected_raw_shadow') is True and
        all(late.get(key)==value for key,value in capture.items()),
        'CE protected raw-copy proof uses a different late installer execution')
    terminal=report.get('terminal_cases',[])
    successes={'unchanged_live','changed_live_code','changed_live_padding'}
    protected_failures={'changed_shadow_code','changed_shadow_padding'}
    require(len(terminal)==6 and {row.get('case') for row in terminal}==
        successes|protected_failures|{'restore_write_fault'},
        'CE protected raw-copy proof omits live, protected or destination corruption cases')
    for row in terminal:
        require(all(row.get(key) is True for key in ('protected_copy_unchanged',
            'sealed_expected_crc_unchanged','no_physical_sd_read_executed')) and
            row.get('actual_instructions',0)>0 and row.get('fast_actual_crc_instructions',0)>0,
            'CE protected raw-copy proof omits actual execution or changes protected authority')
        protected=row['case'] in protected_failures
        require(row.get('target_bytes_written')==(0 if protected else 512) and
            row.get('full512_destination_crc_started')==(0 if protected else 1) and
            row.get('full512_destination_crc_finished')==(0 if protected else 1),
            'CE protected raw-copy proof omits full restore/CRC ordering or restores a refused source')
        result=row.get('result',[])
        require((row['case'] in successes and result==['first_raw_instruction']) or
            (protected and result==['stopped',4,0x800]) or
            (row['case']=='restore_write_fault' and len(result)==3 and result[:2]==['stopped',13] and
                isinstance(result[2],int) and result[2]!=(zlib.crc32(raw)&0xffffffff)),
            'CE protected raw-copy proof executed an unauthenticated destination or lost refusal details')
    full_cases={row.get('kind'):row for row in gate.get('cases',[])}
    require(all(full_cases.get(kind,{}).get('outcome')=='outer_entry' for kind in
        ('changed_live_raw_code','changed_live_raw_padding')) and
        all(row.get('raw_restore_bytes')==512 and row.get('authenticated_raw_restored') is True
            for row in full_cases.values() if row.get('outcome')=='outer_entry') and
        full_cases.get('bad_raw',{}).get('failure_reason')==4 and
        full_cases.get('bad_raw',{}).get('failure_detail')==0x800 and
        full_cases.get('bad_raw',{}).get('raw_restore_bytes')==0 and
        full_cases.get('restore_source_fault',{}).get('failure_reason')==13 and
        full_cases.get('restore_source_fault',{}).get('raw_blocks')==0 and
        full_cases.get('restore_source_fault',{}).get('ram_clear_calls')==0,
        'CE controller proof omits changed-live recovery, protected refusal or destination failure before execution')
    return report
def ce_raw_image_contract(folder,kind,gate,late,proof=None):
    script='tests/test_smash_ce_return_raw_image.py'
    report=external_or_run(proof,script,['--build',folder,'--profile',kind])
    require(report.get('schema')=='kui.smash-ce.return-raw-image.v1' and report.get('status')=='PASS' and
        report.get('profile')==kind and report.get('inputs_immutable_during_execution') is True and
        {str(Path(p).resolve()):v for p,v in report.get('inputs',{}).items()}==
        {str((folder/p).resolve()):sha((folder/p).read_bytes()) for p in
            ('ce-return.elf','ce-return.bin','helper/helper.elf','helper/helper.bin',
             'resident-sci.elf','stage.elf','helper/prepared.bin','vc2-raw.bin')},
        'CE canonical raw-image proof is stale or incomplete')
    current_execution_sources(report,script,('tests/test_smash_ce_return_loader.py',
        'tests/test_smash_ce_return_late.py','tests/sh4_integer_fast.c',
        'src/loader/smash_ce_return_copy.S','src/loader/smash_ce_return_loader.c',
        'src/loader/smash_ce_return_loader.ld','src/loader/ce_return_crc32.h',
        'src/loader/smash_ce_return_raw_image.h','tools/smash_ce_return_raw_image.py',
        'src/loader/cold_load_crc32.c','src/core/smash_ce_return_loader.c',
        'Makefile.smash-ce-return-probe','tools/check_vmu_native_worker_gateway.py','tools/smash_ce_return_view.py'))
    checks=report.get('checks',{})
    require(all(checks.get(key) is True for key in ('current_raw_image_byte_exact',
        'initialized_prefix_covered_by_code_crc','canonical_zero_tail_exact',
        'startup_template_matches_eb6','no_pre_nk_shadow_capture',
        'prefix_corruption_refused_before_retirement','changed_live_requires_terminal_reconstruction',
        'reconstruction_after_quiesce_cache_off_mmu_off','destination_full512_crc_before_execution',
        'failed_destination_crc_refuses_execution','sealed_expected_crc_never_refreshed',
        'worker_runtime_stack_geometry_unchanged')),
        'CE canonical raw-image proof omits fresh image equality, passive startup or authenticated reconstruction')
    # Recheck the actual fresh artifacts even for a reused report. A prior
    # reader body or generated header cannot become authority by metadata.
    from smash_ce_return_raw_image import verify as raw_image_verify
    raw=(folder/'vc2-raw.bin').read_bytes()
    canonical=raw_image_verify(raw,folder/'resident-sci.elf',
        ROOT/'src/loader/smash_ce_return_raw_image.h',folder/'ce-return.elf')
    controller=read_elf(folder/'ce-return.elf');symbols=controller[4]
    prefix=object_bytes(controller,'_kui_smash_ce_return_raw_prefix')
    require(len(raw)==512 and len(prefix)==260 and raw==prefix+bytes(252) and
        sha(raw)==CE_RAW_IMAGE_SHA256==report.get('raw_sha256') and
        report.get('raw_crc32')==f'{zlib.crc32(raw)&0xffffffff:08x}' and
        report.get('prefix_sha256')==sha(prefix) and report.get('prefix_bytes')==260 and
        report.get('zero_tail_bytes')==252 and report.get('canonical')==canonical and
        not any(name in symbols for name in (CE_RAW_SHADOW_MARKER,
            '__smash_ce_return_raw_shadow_begin','__smash_ce_return_raw_shadow_end')) and
        symbols['__smash_ce_return_bss_end'][0]-symbols['__smash_ce_return_bss_begin'][0]==16 and
        sha((ROOT/'src/loader/smash_ce_return_copy.S').read_bytes())==
            'a3e472c0a8467861dd9c9af187606f046f066961b119d59d3b7940a458313c91',
        'CE canonical raw image differs from the current pinned reader or retains a new startup capture')
    expected={'dirty_alias_copy_and_clear':0,'copy_readback_fault':-1,'changed_e0_owner':-1}
    cases=late.get('cases',[])
    require(late.get('protected_raw_shadow') is False and late.get('raw_shadow_interval') is None and
        late.get('canonical_raw_image') is True and late.get('no_new_direct_b400_writes') is True and
        len(cases)==len(expected) and {row.get('case'):row.get('result') for row in cases}==expected and
        all(row.get('protected_raw_shadow') is False and row.get('raw_helper_unchanged') is True and
            row.get('caller_nonvolatile_state_exact') is True and
            row.get('no_new_direct_b400_writes') is True for row in cases),
        'CE canonical raw-image cohort changed the passive startup installer or retains a shadow capture')
    capture=report.get('capture',{})
    require(capture.get('status')=='PASS' and capture.get('protected_raw_shadow') is False and
        all(late.get(key)==value for key,value in capture.items()),
        'CE canonical raw-image proof uses a different late installer execution')
    terminal=report.get('terminal_cases',[])
    successes={'unchanged_live','changed_live_code','changed_live_padding'}
    prefix_failures={'changed_prefix_code','changed_prefix_last'}
    destination_failures={'restore_write_fault','restore_padding_fault'}
    require(len(terminal)==7 and {row.get('case') for row in terminal}==
        successes|prefix_failures|destination_failures,
        'CE canonical raw-image proof omits live, initialized-prefix or destination corruption cases')
    for row in terminal:
        require(all(row.get(key) is True for key in ('canonical_prefix_unchanged','canonical_prefix_never_executed',
            'sealed_expected_crc_unchanged','no_physical_sd_read_executed')) and
            row.get('actual_instructions',0)>0 and row.get('fast_actual_crc_instructions',0)>0,
            'CE canonical raw-image proof omits actual execution or changes its sealed authority')
        refused=row['case'] in prefix_failures
        require(row.get('target_bytes_written')==(0 if refused else 512) and
            row.get('full512_destination_crc_started')==(0 if refused else 1) and
            row.get('full512_destination_crc_finished')==(0 if refused else 1),
            'CE canonical raw-image proof omits full reconstruction/CRC ordering or restores a refused source')
        result=row.get('result',[])
        require((row['case'] in successes and result==['first_raw_instruction']) or
            (refused and result==['stopped',4,0xc00]) or
            (row['case'] in destination_failures and len(result)==3 and result[:2]==['stopped',13] and
                isinstance(result[2],int) and result[2]!=(zlib.crc32(raw)&0xffffffff)),
            'CE canonical raw-image proof executed an unauthenticated destination or lost refusal details')
    full_cases={row.get('kind'):row for row in gate.get('cases',[])}
    require(all(full_cases.get(kind,{}).get('outcome')=='outer_entry' for kind in
        ('changed_live_raw_code','changed_live_raw_padding')) and
        all(row.get('raw_restore_bytes')==512 and row.get('authenticated_raw_restored') is True
            for row in full_cases.values() if row.get('outcome')=='outer_entry') and
        full_cases.get('bad_raw',{}).get('failure_reason')==4 and
        full_cases.get('bad_raw',{}).get('failure_detail')==0xc00 and
        full_cases.get('bad_raw',{}).get('raw_restore_bytes')==0 and
        full_cases.get('restore_source_fault',{}).get('failure_reason')==13 and
        full_cases.get('restore_source_fault',{}).get('raw_blocks')==0 and
        full_cases.get('restore_source_fault',{}).get('ram_clear_calls')==0,
        'CE controller proof omits changed-live reconstruction, initialized-prefix refusal or destination failure')
    return report
def ce_startup_contract(folder,kind,proof=None):
    script='tests/test_smash_ce_return_startup_contract.py'
    report=external_or_run(proof,script,['--build',folder,'--profile',kind])
    from test_smash_ce_return_startup_contract import ARTIFACTS,PINS,NEGATIVE_PINS,ACCEPTED_BUILD,NEGATIVE_BUILD,geometry,section,resident_body
    role='vc2' if kind==1 else 'swirl'
    accepted=ROOT/'build'/('smash-shared-card-'+ACCEPTED_BUILD)/role
    negative=ROOT/'build'/('smash-shared-card-'+NEGATIVE_BUILD)/role
    require(report.get('schema')=='kui.smash-ce.return-startup-contract.v1' and report.get('status')=='PASS' and
        report.get('profile')==kind and report.get('inputs_immutable_during_execution') is True and
        report.get('artifact_coupling_verified') is True and
        report.get('console_startup_claim') is False,
        'CE accepted startup-contract proof is incomplete or claims console acceptance')
    current_execution_sources(report,script,('tests/test_smash_ce_return_late.py',
        'src/loader/smash_ce_return_copy.S','src/loader/smash_ce_return_entry.S',
        'src/loader/smash_ce_return_loader.ld','src/loader/vc2_ce_vmu_gate.inc',
        'src/loader/vc2_ce_vmu_late_stage.inc','tools/check_vmu_native_worker_gateway.py'))
    for key,build,pins in (('inputs',folder,None),('accepted_inputs',accepted,PINS[kind]),
                           ('negative_inputs',negative,NEGATIVE_PINS[kind])):
        actual={str((build/name).resolve()):sha((build/name).read_bytes()) for name in ARTIFACTS}
        require({str(Path(p).resolve()):value for p,value in report.get(key,{}).items()}==actual and
            (pins is None or actual=={str((build/name).resolve()):value for name,value in pins.items()}),
            'CE startup-contract proof has stale candidate/accepted3337/rejectedcd3f inputs: '+key)
    checks=('initialized_extent_exact','copy_entry_runtime_BSS_previousE0_stack_exact',
        'linked_copy_template_exact','entry_opcodes_exact_except_return_main_literal',
        'resident_startup_bytes_exact_except_build_label','all_pre_initializer_write_addresses_exact',
        'all_cache_publications_exact','copy_caller_nonvolatile_and_FPU_exact','copy_no_MMIO',
        'copy_hook_argument_exact','copy_success_and_refusals_exact','non_reset_forwarding_exact')
    require(all(report.get('checks',{}).get(key) is True for key in checks),
        'CE startup-contract proof omits exact accepted geometry, writes, cache publication or E0 state')
    current,reference=read_elf(folder/'ce-return.elf'),read_elf(accepted/'ce-return.elf')
    require(report.get('geometry')==geometry(current)==geometry(reference)==report.get('accepted_geometry') and
        report.get('initialized_bytes')==report.get('accepted_initialized_bytes')==
            (7340 if kind==1 else 7500)==len((folder/'ce-return.bin').read_bytes()) and
        section(current,'__smash_ce_return_copy')==section(reference,'__smash_ce_return_copy') and
        resident_body(read_elf(folder/'resident-sci.elf'))==resident_body(read_elf(accepted/'resident-sci.elf')),
        'CE startup-contract proof differs from actual accepted copy/runtime/BSS/end or resident installer bytes')
    expected={'ready':0,'copy_readback_fault':-1,'changed_e0_owner':-1}
    cases=report.get('cases',[]);baseline=report.get('accepted_cases',[])
    require(len(cases)==len(baseline)==3 and cases==baseline and
        {row.get('case'):row.get('result') for row in cases}==expected and
        all(all(row.get(key) is True for key in ('caller_nonvolatile_state_exact','FPU_state_exact',
            'no_FPU_instructions','raw512_unchanged')) and not row.get('MMIO_operations') and
            row.get('instructions',0)>0 and row.get('hook_argument')=='0x8c005000' and
            all(re.fullmatch('[0-9a-f]{64}',row.get(key,'')) for key in
                ('write_addresses_sha256','cache_publications_sha256')) for row in cases),
        'CE startup-contract proof omits actual ordered write/cache traces or startup refusal state')
    forwarding=report.get('non_reset_forwarding',[])
    require(len(forwarding)==4 and {row.get('command') for row in forwarding}=={'0x0','0x2','0x3','0xffffffff'} and
        all(row.get('outcome')=='unchanged_original_hook_boundary' and row.get('writes')==0 and
            row.get('MMIO_operations')==0 and row.get('caller_state_exact') is True and
            row.get('instructions',0)>0 for row in forwarding),
        'CE startup-contract proof omits unchanged unrelated E0 forwarding')
    controls=report.get('controls',{});growth=controls.get('cd3f_growth',{})
    require(controls.get('accepted3337_self')=={'status':'PASS','all_checks_pass':True} and
        growth.get('status')=='FAIL' and growth.get('checks',{}).get('initialized_extent_exact') is False and
        growth.get('checks',{}).get('all_pre_initializer_write_addresses_exact') is False and
        growth.get('initialized_bytes')==report['initialized_bytes']+456 and
        growth.get('geometry')==geometry(read_elf(negative/'ce-return.elf')) and
        len(growth.get('cases',[]))==3 and
        next((row.get('write_addresses_count') for row in growth['cases'] if row.get('case')=='ready'),None)==
            next(row['write_addresses_count'] for row in cases if row['case']=='ready')+114,
        'CE startup-contract proof omits accepted self-control or actual rejectedcd3f geometry/write growth')
    return report
def ce_controller_transport(folder,kind,stage,entry,helper,prepared,controller_proof=None,late_proof=None,
    integrity_proof=None,swirl_source_proof=None,swirl_originals=None,ram_clear_proof=None,fault_chain_proof=None,
    raw_shadow_proof=None,raw_image_proof=None,startup_proof=None):
    """Only this explicit final-linked profile may use the former zero tail.

    Old embedded-helper versions keep their full zero-padding/terminal gate.
    The original41152 bytes must remain exact; the new static tail must equal
    the actual bounded controller ELF, including its blank632-byte runtime.
    """
    folder=Path(folder);controller=read_elf(folder/'ce-return.elf')
    code=(folder/'ce-return.bin').read_bytes()
    require(stage[4].get(CE_TRANSPORT_MARKER)==CE_CONTROLLER_ONE and
        controller[4].get(CE_CONTROLLER_MARKER)==CE_CONTROLLER_ONE,
        'CE return controller stage/linked capability differs')
    payload,_=flatten_elf(entry[0]);require(payload[0x1e0:0x1f0]==CE_CONTROLLER_CAP and
        payload.count(CE_CONTROLLER_CAP)==1,'CE return typed entry capability missing/ambiguous')
    require(len(helper)==41152 and sha(helper)==HELPER_PINS[kind]['reference_sha256'],
        'CE return transport changed the accepted immutable helper')
    require(code==physical_image(controller) and 632<=len(code)<=7968 and not len(code)%4,
        'CE return transport differs from its bounded actual linked controller')
    first=controller[4]['__smash_ce_return_begin'][0]
    last=controller[4]['__smash_ce_return_binary_end'][0]
    runtime=controller[4]['_kui_smash_ce_return_runtime']
    offset=runtime[0]-first
    require(first==0xac008300 and last-first==len(code) and runtime[1]==632 and
        0<=offset<=len(code)-632 and not offset%4 and code[offset:offset+632]==bytes(632),
        'CE return static runtime is nonblank or outside the exact controller window')
    require(controller[4]['__smash_ce_return_bss_end'][0]<=0xac00b800 and
        controller[4]['__smash_ce_return_stack_bottom'][0]==0xac00b800 and
        controller[4]['__smash_ce_return_stack_top'][0]==0xac00c000,
        'CE return controller escapes the retired low interval/private stack')
    if controller[4].get(CE_RAW_SHADOW_MARKER)==CE_CONTROLLER_ONE:
        require(controller[4].get('__smash_ce_return_raw_shadow_begin',(None,))[0]==0xac00b400 and
            controller[4].get('__smash_ce_return_raw_shadow_end',(None,))[0]==0xac00b600 and
            controller[4]['__smash_ce_return_bss_end'][0]<=0xac00b400 and
            not any(p[0]==1 and p[4] and p[2]<0xac00b600 and 0xac00b400<p[2]+p[4]
                for p in controller[2]),
            'CE protected raw copy must be an exact NOLOAD512-byte lease below the private stack')
    header=struct.pack('<8I',0x5349554b,0x31525443,1,32,41152,len(code),offset,632)
    expected=helper+header+code+bytes(49152-41184-len(code))
    require(len(prepared)==49152 and prepared==expected,
        'Prepared CE return transport is not the exact raw helper/header/controller/zero tail')
    require(embedded(stage,'__vc2_vmu_blob_start','__vc2_vmu_blob_end')==prepared,
        'CE stage embeds a different complete typed transport')
    from test_smash_ce_return_loader import audit as controller_audit
    from test_smash_ce_return_late import audit as late_audit
    gate=json.loads(Path(controller_proof).read_text()) if controller_proof else \
        controller_audit(folder/'ce-return.elf',folder/'helper/helper.elf',
            folder/'helper/helper.bin',folder/'resident-sci.elf',kind)
    require(gate.get('status')=='PASS' and gate.get('profile')==kind and all(gate.get(key)==sha(path.read_bytes()) for key,path in
        (('elf_sha256',folder/'ce-return.elf'),('helper_elf_sha256',folder/'helper/helper.elf'),
         ('helper_sha256',folder/'helper/helper.bin'),('resident_elf_sha256',folder/'resident-sci.elf'))),
        'CE terminal controller linked proof is stale or incomplete')
    current_execution_sources(gate,'tests/test_smash_ce_return_loader.py')
    require(all(gate.get(key) is True for key in ('actual_entry','actual_idle_view',
        'actual_terminal_quiesce','actual_mmu_off_and_enter','root_context_exact','receipt_exact',
        'phase_video_exact','no_old_renderer','no_old_resume','non_reset_forwarding_preserved')) and
        gate.get('qualified_command')==1,
        'CE terminal controller proof omits its complete linked lifecycle/publication checks')
    outcomes={row.get('kind'):row.get('outcome') for row in gate.get('cases',[])}
    require(all(outcomes.get(kind)=='outer_entry' for kind in
        ('idle','unstarted','busy_continuation','busy_gd')) and
        all(outcomes.get(kind)=='unchanged_original_hook_boundary' for kind in
            ('forward_reset3','forward_disc','forward_unknown')) and
        all(outcomes.get(kind)=='visible_refusal_boundary' for kind in
            ('wrong_caller','bad_receipt','bad_video','bad_runtime','bad_helper',
             'faulted_loan','bad_capsule_crc')),
        'CE terminal controller proof omits idle, pending-completion or authority refusal cases')
    late=json.loads(Path(late_proof).read_text()) if late_proof else late_audit(folder,kind)
    require(late.get('status')=='PASS' and late.get('profile')==kind and all(late.get(key)==sha(path.read_bytes()) for key,path in
        (('stage_elf_sha256',folder/'stage.elf'),('resident_elf_sha256',folder/'resident-sci.elf'),
         ('controller_elf_sha256',folder/'ce-return.elf'),('helper_sha256',folder/'helper/helper.bin'),
         ('prepared_sha256',folder/'helper/prepared.bin'))),
        'CE late transport/zero-tail-before-initializer proof is stale or incomplete')
    current_execution_sources(late,'tests/test_smash_ce_return_late.py')
    extra={}
    for marker in CE_RETURN_FEATURES:
        require(controller[4].get(marker) in (None,CE_CONTROLLER_ONE),'Unknown CE return capability: '+marker)
    require(not (controller[4].get(CE_RAW_IMAGE_MARKER) and controller[4].get(CE_RAW_SHADOW_MARKER)),
        'CE canonical raw image and protected raw shadow cannot be combined')
    if controller[4].get(CE_RAM_CLEAR_MARKER)==CE_CONTROLLER_ONE:
        extra['terminal_ram_clear']=ce_ram_clear_contract(folder,gate,ram_clear_proof,swirl_originals)
    if controller[4].get(CE_TRACE_MARKER)==CE_CONTROLLER_ONE:
        extra['actual_failure_chain']=ce_fault_chain_contract(folder,kind,fault_chain_proof)
    if controller[4].get(CE_RAW_SHADOW_MARKER)==CE_CONTROLLER_ONE:
        extra['protected_raw_copy']=ce_raw_shadow_contract(folder,kind,gate,late,raw_shadow_proof)
    if controller[4].get(CE_RAW_IMAGE_MARKER)==CE_CONTROLLER_ONE:
        extra['canonical_raw_image']=ce_raw_image_contract(folder,kind,gate,late,raw_image_proof)
    if controller[4].get(CE_STARTUP_ABI_MARKER)==CE_CONTROLLER_ONE:
        require(controller[4].get(CE_CRC_BITWISE_MARKER)==CE_CONTROLLER_ONE and
            controller[4].get(CE_RAW_IMAGE_MARKER)==CE_CONTROLLER_ONE,
            'Fixed CE startup ABI requires its exact canonical image and bounded bitwise CRC')
        extra['accepted_startup_contract']=ce_startup_contract(folder,kind,startup_proof)
    if controller[4].get('__kui_smash_ce_return_integrity_mask')==CE_CONTROLLER_ONE:
        from test_smash_ce_return_integrity import audit as integrity_audit
        integrity=json.loads(Path(integrity_proof).read_text()) if integrity_proof else integrity_audit(folder,kind)
        require(integrity.get('status')=='PASS' and integrity.get('profile')==kind and
            {str(Path(p).resolve()):v for p,v in integrity.get('inputs',{}).items()}==
            {str((folder/p).resolve()):sha((folder/p).read_bytes()) for p in
                ('ce-return.elf','helper/helper.elf','helper/helper.bin','resident-sci.elf')},
            'CE failure-mask/CRC proof is stale or incomplete')
        current_execution_sources(integrity,'tests/test_smash_ce_return_integrity.py',
            ('src/loader/ce_return_crc32.h','src/loader/cold_load_crc32.c',
             'src/loader/smash_ce_return_loader.c','src/loader/smash_ce_return_loader.ld'))
        predicate,shared=integrity['predicate'],integrity['shared_crc']
        require(len(predicate.get('isolated_bits',[]))==13 and predicate.get('baseline_mask')==0 and
            predicate.get('all_failures_read_only') is True and predicate.get('all_mutations_resolved') is True and
            shared.get('golden_zlib_equal') is True,
            'CE failure-mask proof omits exact guards, fixed bounds or checksum equivalence')
        bitwise=controller[4].get(CE_CRC_BITWISE_MARKER)==CE_CONTROLLER_ONE
        if bitwise:
            table=b''.join(struct.pack('<I',zlib.crc32(bytes([i]),0xffffffff)^0xffffffff)for i in range(128))
            loaded=b''.join(controller[0][p[1]:p[1]+p[4]]for p in controller[2]if p[0]==1 and p[4])
            sizes=[0,1,2,3,4,5,7,15,16,31,32,33,63,64,65,127,128,129,255,256,257,511,512,513,631,632,633,1023,1024,4096,41152]
            require(integrity.get('inputs_immutable_during_execution') is True and
                shared.get('implementation')=='bitwise-ieee' and shared.get('table_bytes')==0 and
                shared.get('table_symbol') is None and shared.get('no_initialized_byte_table') is True and
                loaded.count(table)==0 and shared.get('polynomial_mutation_refused') is True and
                shared.get('leaf_sha256')==sha(object_bytes(controller,'_kui_cold_load_crc32_leaf')) and
                shared.get('all256_input_bytes') is True and shared.get('byte_alignments')==[0,1,2,3,7] and
                shared.get('lengths')==sizes and shared.get('incremental_split_cases')==22 and
                shared.get('vectors')==256+len(sizes)*15+44+8 and
                {(row.get('bit'),row.get('mask')) for row in predicate['isolated_bits']}==
                    {(bit,hex(1<<bit)) for bit in range(13)} and
                all(row.get('resolved_mask')==0 and row.get('instructions',0)>0 and
                    row.get('resolved_instructions',0)>0 for row in predicate['isolated_bits']) and
                len(predicate.get('fixed_bounds',[]))==5 and
                {row.get('helper_bytes_field') for row in predicate['fixed_bounds']}==
                    {0,41151,41153,0x80000000,0xffffffff} and
                all(row.get('mask')=='0x2' and row.get('read_past_owned_helper') is False for row in predicate['fixed_bounds']) and
                predicate.get('independent_crc_zero_fixups') is True and
                {(row.get('entry'),row.get('mmu'),row.get('ccr')) for row in shared.get('public_wrappers',[])}==
                    {(name,mmu,hex(ccr)) for name in ('retail','cold') for mmu,ccr in ((1,1),(1,0x101),(0,0),(0,0x100))} and
                len(shared['public_wrappers'])==8,
                'CE bitwise CRC proof omits actual table removal, complete goldens, incremental seeds or cache wrappers')
        else:
            require(shared.get('table_bytes')==512 and shared.get('high_bit_fold_mutation_refused') is True,
                'CE folded-byte CRC proof omits its exact table or high-bit mutation refusal')
        if controller[4].get(CE_RAW_IMAGE_MARKER)==CE_CONTROLLER_ONE:
            require(predicate.get('raw_validation_domain')==hex(controller[4]['_kui_smash_ce_return_raw_prefix'][0]) and
                predicate.get('raw_stored_bytes')==260 and predicate.get('raw_validation_bytes')==512 and
                predicate.get('canonical_zero_tail_bytes')==252 and
                predicate.get('retired_live_slot',{}).get('all_live_bytes_changed') is True and
                predicate['retired_live_slot'].get('mask')==0,
                'CE failure-mask proof omits the complete canonical raw image or trusts the retired live slot')
        extra['integrity']=integrity
    if kind==2 and controller[4].get('__kui_smash_swirl_private_return')==CE_CONTROLLER_ONE:
        from test_smash_swirl_return_source import audit as swirl_audit
        require(swirl_source_proof or swirl_originals,'Swirl private return needs its original frontend source proof')
        source=json.loads(Path(swirl_source_proof).read_text()) if swirl_source_proof else swirl_audit(folder,swirl_originals)
        require(source.get('status')=='PASS' and source.get('artifacts_sha256')==
            {p:sha((folder/p).read_bytes()) for p in ('ce-return.elf','resident-sci.elf','stage.elf','helper/prepared.bin')},
            'Swirl frontend proof is stale or incomplete')
        current_execution_sources(source,'tests/test_smash_swirl_return_source.py')
        outcomes={row.get('case'):row.get('outcome') for row in source.get('composed_terminal_cases',[])}
        require(len(outcomes)==9 and outcomes.get('maigo_private')==outcomes.get('firmware_fallback')=='outer_entry' and
            outcomes.get('unknown_private_caller')=='unchanged_old_hook_boundary' and
            all(outcomes.get(p)=='source_guard_refusal' for p in ('private_wrong_fe','private_wrong_cell',
                'fallback_bad_stub','fallback_wrong_fe','fallback_wrong_cell','fallback_wrong_boot_vector')),
            'Swirl proof omits actual private callback, fallback or source-guard refusal')
        extra['original_swirl_private_return']=source
    return dict(status='PASS',typed_tail_bytes=32,immutable_helper_bytes=41152,
        controller_bytes=len(code),runtime_offset=offset,runtime_bytes=632,prepared_bytes=49152,
        prepared_sha256=sha(prepared),controller_elf_sha256=sha(controller[0]),
        controller=gate,late_installation=late,return_to_menu_implemented=True,
        physical_hardware_verified=False,**extra)

def probe_stack(build):
    paths=sorted((build/'ce-return').rglob('*.ci'))
    require(paths,'Missing CE observer compiler stack graphs')
    frames,calls=graph_load(paths,qualified=True,scope='probe')
    def named(n):
        found=[v for v in frames if function_base(v)==n]
        require(len(found)==1,'Missing/ambiguous CE observer frame '+n);return found[0]
    frames['probe:kui_cold_load_call3']=16
    calls['probe:kui_cold_load_call3']=[(named('kui_cold_load_crc32_leaf'),'bound-crc-leaf')]
    frames['probe:terminal-renderer-reserve']=352
    memo={}
    def depth(n,active=()):
        require(n not in active,'Recursive CE observer call')
        if n in memo:return memo[n]
        if n not in frames:
            twins=[v for v in frames if function_base(v)==function_base(n)]
            if twins:return max(depth(v,active) for v in twins)
            require(function_base(n) in LIBRARY,'Unbounded CE observer callee '+n);return 0
        child=0
        for callee,label in calls.get(n,[]):
            if callee=='__indirect_call':
                require(function_base(n)=='kui_smash_ce_return_probe_main' and
                    re.fullmatch(r'smash_ce_return_probe.c:\d+:\d+',label),
                    'Unreviewed CE observer callback '+n+' '+label)
                callee='probe:terminal-renderer-reserve'
            child=max(child,depth(callee,active+(n,)))
        memo[n]=frames[n]+child;return memo[n]
    maximum=depth(named('kui_smash_ce_return_probe_main'))
    require(maximum+64+16<=2048,'CE observer reaches private stack guard')
    return dict(C_and_entire_low_renderer_reserve=maximum,private_bytes=2048,
        library_guard_reserve=64,argument_home_bytes=16,cold_scope_frame_bytes=16,
        graph_sha256={str(p.relative_to(build)):sha(p.read_bytes()) for p in paths})

def session_audit(outer,native,vc2,swirl,originals,allow_dirty,text_profile,
                  native_gin_proof=None,transport_proof=None,installer_proof=None,
                  ce_return_proofs=None,ce_late_proofs=None,ce_integrity_proofs=None,
                  swirl_return_proof=None,swirl_originals=None,ce_ram_clear_proofs=None,ce_fault_chain_proofs=None,
                  ce_raw_shadow_proofs=None,ce_raw_image_proofs=None,ce_startup_proofs=None,
                  native_options_proof=None,genesis_options_source_proof=None):
    """The explicit new GIN/session profile has its own complete linked gates.

    Legacy cold BOOT2 and warm-copy observations are not substituted for the
    new actual source verifier, prefix, arm bridge or receipt transport.
    """
    require(originals is not None,'Native GIN/session qualification requires the external original assets')
    folders=dict(outer=Path(outer),native=Path(native),vc2=Path(vc2),swirl=Path(swirl))
    names=dict(outer='retail-smash-menu.kui',native='retail-smash-native-menu.kui',
        vc2='retail-smash-vc2-child.kui',swirl='retail-smash-swirl-child.kui')
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True))
    require(allow_dirty or not dirty,'Final SESSION1 qualification requires clean source')
    artifacts={};modules={};stages={};entries={};installs={};reports={}
    blank_receipt=b'KUISCR02'+struct.pack('<II',2,96)+bytes(80)
    config=b'KUISCF01'+struct.pack('<II',1,4224)
    for role,folder in folders.items():
        wire=(folder/names[role]).read_bytes();modules[role]=verify(wire)
        entries[role]=read_elf(folder/'entry.elf');stages[role]=read_elf(folder/'stage.elf')
        payload,_=flatten_elf(entries[role][0]);receipt=json.loads((folder/'entry.compile.json').read_text())
        require(payload==wire[64:] and modules[role]['build']==receipt['commit'][:12] and
            (allow_dirty or receipt['commit']==commit) and
            receipt['elf_sha256']==sha(entries[role][0]) and (allow_dirty or not receipt['source_dirty']),
            'SESSION1 package/entry/source provenance differs: '+role)
        require(payload[0x2a0:0x300]==blank_receipt and entries[role][4]['__retail_map'][0]==0x8c011000,
            'SESSION1 fixed blank receipt/map prefix differs: '+role)
        require(payload.count(config)==1,'SESSION1 unique typed config missing: '+role)
        at=payload.index(config);require(payload[at+16:at+4224]==bytes(4208),'On-disk session config is populated')
        require(physical_image(stages[role])==(folder/'stage.bin').read_bytes()==
            embedded(entries[role],'__retail_stage_blob_start','__retail_stage_blob_end'),
            'SESSION1 entry embeds a different stage: '+role)
        for file in ('entry.elf','stage.elf','resident-sci.elf',names[role]):
            artifacts[role+'/'+file]=sha((folder/file).read_bytes())
    require(stages['native'][4].get('__kui_smash_native_gin_adoption')==(1,0,0,0xfff1),
        'SESSION1 native stage does not adopt original GIN')
    original_options=native_original_options(stages['native'])
    reports['native_combo_policy']='original-options' if original_options else 'historical-direct-outer'
    if original_options:
        reports['native_original_options_return']=native_options_contract(folders['native'],originals,native_options_proof)
        reports['genesis_options_source']=genesis_options_contract(originals,genesis_options_source_proof)
    native_payload=(folders['native']/names['native']).read_bytes()[64:]
    require(native_payload[0x1e0:0x1f0]==b'KUINAD01'+struct.pack('<II',1,16) and
        native_payload[0x200:0x214]==b'KUISNGI1'+struct.pack('<III',1,160,3),
        'Native GIN typed capability/blank continuation differs')
    require('__smash_cold_image_seed' not in stages['native'][4],
        'Native GIN profile unexpectedly retains the legacy warm-copy seed')
    ce_markers=[stages[role][4].get(CE_TRANSPORT_MARKER) for role in ('vc2','swirl')]
    require(all(marker in (None,CE_CONTROLLER_ONE) for marker in ce_markers) and
        (all(marker is None for marker in ce_markers) or all(marker==CE_CONTROLLER_ONE for marker in ce_markers)),
        'CE return controller requires both exact opted-in child stages; mixed/unknown capabilities refused')
    ce_controller_profile=ce_markers[0]==CE_CONTROLLER_ONE
    if ce_controller_profile:
        controllers=[read_elf(folders[role]/'ce-return.elf') for role in ('vc2','swirl')]
        ce_feature_cohort(controllers)
    gin=external_or_run(native_gin_proof,'tests/test_smash_native_gin_linked.py',
        ['--build',folders['native'],'--originals',originals])
    require(gin.get('status')=='PASS' and all(gin.get(key)==sha((folders['native']/file).read_bytes()) for key,file in
        (('verifier_elf_sha256','native-gin-verify.elf'),('entry_elf_sha256','entry.elf'),('stage_elf_sha256','stage.elf'))) and
        gin.get('test_sha256')==sha((ROOT/'tests/test_smash_native_gin_linked.py').read_bytes()) and
        gin.get('decoder_sha256')==sha((ROOT/'tests/sh4_integer_fast.c').read_bytes()) and gin.get('inputs_immutable_during_execution'),
        'Native GIN proof is stale or incomplete')
    reports['native_original_GIN']=gin
    transport=external_or_run(transport_proof,'tests/test_smash_card_receipt_transport.py',
        ['--outer',folders['outer'],'--native',folders['native'],'--originals',originals])
    sequence=transport.get('high_pure_sequence_operation',{})
    require(transport.get('schema')=='kui.smash.card-receipt-transport.v2' and
        transport.get('status')=='PASS' and
        all(sequence.get(key)==value for key,value in
            (('previous_owner',2),('next_owner',1),('previous_sequence',1),('next_sequence',2))) and
        transport['artifacts_sha256']=={
        'outer_handoff':sha((folders['outer']/'handoff.elf').read_bytes()),
        'native_return':sha((folders['native']/'return-loader.elf').read_bytes()),
        'native_entry':sha(entries['native'][0]),'outer_stage':sha(stages['outer'][0])} and
        all(sha((ROOT/p).read_bytes())==value for p,value in transport['source_sha256'].items()),
        'SESSION1 receipt transport proof is stale')
    reports['card_receipt_transport']=transport
    if ce_controller_profile:
        operations=transport.get('dedicated_CE_return_operations',[])
        require(len(operations)==2 and {row.get('previous_owner') for row in operations}=={3,4} and
            all(row.get('next_owner')==1 and row.get('previous_sequence')==1 and row.get('next_sequence')==2 and
                row.get('generic_transfer_refused') is True and row.get('dedicated_transfer_executed') is True
                for row in operations),'CE return requires dedicated role-aware receipt operations; generic permission refused')
    worker=read_elf(folders['native']/'native-worker.elf');low=read_elf(folders['native']/'resident-sci.elf')
    require(worker[4]['__kui_native_worker_span'][0]==0x12000,'SESSION1 worker is not72KiB')
    require(embedded(stages['native'],'__native_worker_blob_begin','__native_worker_blob_end')==
        (folders['native']/'native-worker.bin').read_bytes(),'Native stage embeds a different worker')
    from check_vmu_native_stage_stack import check as installer_check
    from check_smash_menu_loader import combined_stacks,handoff
    if installer_proof:
        bound=json.loads(Path(installer_proof).read_text())
        for key,path in (('stage_elf_sha256',folders['native']/'stage.elf'),
            ('resident_elf_sha256',folders['native']/'resident-sci.elf'),
            ('worker_elf_sha256',folders['native']/'native-worker.elf'),
            ('outer_handoff_elf_sha256',folders['outer']/'handoff.elf'),
            ('outer_resident_elf_sha256',folders['outer']/'resident-sci.elf')):
            require(bound.get(key)==sha(path.read_bytes()),'Stale SESSION1 stack envelope: '+key)
        reports['native_installer']=bound['installer'];reports['outer_combined_stack']=bound['outer_combined_stack']
    else:
        reports['native_installer']=installer_check(stages['native'],low,worker=worker,self_test=True)
        reports['outer_combined_stack']=combined_stacks(folders['outer'])
    require(reports['native_installer']['enabled'] and
        reports['native_installer']['smash_child_callbacks']['original_GIN_adoption'],
        'Installer proof does not exercise the new GIN route')
    reports['outer_runtime_geometry']=handoff(folders['outer'])[2]
    reports['return_runtime_geometry']=handoff(folders['native'],returning=True)[2]
    reports['native_worker_bytes']=0x12000
    reports['native_cached_boot_CRC']=cached_boot_crc_audit(folders['native']/'stage.elf')
    reports['native_linked_read_through']=native_read_cache_audit(folders['native']/'stage.elf')
    reports['outer_compact_word_copy']=compact_memcpy_audit(folders['outer']/'resident-sci.elf')
    from test_cold_load_scope import audit as cold_scope
    from test_smash_native_fast_crc import audit as fast_crc
    for role,path in (('forward',folders['outer']/'handoff.elf'),('return',folders['native']/'return-loader.elf')):
        reports[role+'_capsule_integrity']=dict(scope=cold_scope(path),
            CRC=fast_crc(path,helper='_kui_cold_load_crc32_leaf',relay='_kui_cold_load_crc32'))
    for kind,role in ((1,'vc2'),(2,'swirl')):
        folder=folders[role];helper=(folder/'helper/helper.bin').read_bytes();prepared=(folder/'helper/prepared.bin').read_bytes()
        if ce_controller_profile:
            returning=ce_controller_transport(folder,kind,stages[role],entries[role],helper,prepared,
                (ce_return_proofs or {}).get(kind),(ce_late_proofs or {}).get(kind),
                (ce_integrity_proofs or {}).get(kind),swirl_return_proof,swirl_originals,
                (ce_ram_clear_proofs or {}).get(kind),(ce_fault_chain_proofs or {}).get(kind),
                (ce_raw_shadow_proofs or {}).get(kind),(ce_raw_image_proofs or {}).get(kind),
                (ce_startup_proofs or {}).get(kind))
        else:
            require(prepared==helper+bytes(49152-len(helper)) and len(prepared)==49152 and
                sha(helper)==HELPER_PINS[kind]['reference_sha256'],'Accepted CE helper changed: '+role)
            require(embedded(stages[role],'__vc2_vmu_blob_start','__vc2_vmu_blob_end')==helper,
                'SESSION1 CE stage embeds a different accepted helper')
            returning=terminal_audit(folder/'resident-sci.elf',stage=folder/'stage.elf')
        reports[role+'_helper']=dict(body=helper_body(helper,kind,REFERENCE_BUILD),
            GIN=gin_adoption_audit(folder/'entry.elf',folder/'stage.elf',folder/'resident-sci.elf'),
            resume=gin_resume_audit(folder/'stage.elf'),
            return_path=returning,return_to_menu_implemented=ce_controller_profile,
            physical_hardware_verified=False)
        if not ce_controller_profile:reports[role+'_helper']['terminal']=returning
    reports['embedded_helper_GUI_admission']=embedded_helper_audit((folders['vc2']/names['vc2'],folders['swirl']/names['swirl']))
    if text_profile:
        reports['text_clock_native']=phase_clock_audit(folders['native']/'stage.elf')
        reports['text_clock_outer']=phase_clock_audit(folders['outer']/'handoff.elf',require_delta=False)
        require('__smash_phase_text_capability' in stages['native'][4] and
            '_smash_phase_stage_text_write' in stages['native'][4],'SESSION1 TXT writer missing')
    targets={'KUI/apps/games/retail-smash-menu.kui':folders['outer']/names['outer'],
        'KUI/apps/games/retail-smash-native-menu.kui':folders['native']/names['native'],
        'KUI/apps/games/retail-smash-vc2.kui':folders['vc2']/names['vc2'],
        'KUI/apps/games/retail-smash-swirl.kui':folders['swirl']/names['swirl'],
        'KUI/apps/games/smash-vc2-vmu-helper.bin':folders['vc2']/'helper/prepared.bin',
        'KUI/apps/games/smash-swirl-vmu-helper.bin':folders['swirl']/'helper/prepared.bin'}
    installs={name:dict(path=str(path.resolve()),sha256=sha(path.read_bytes())) for name,path in targets.items()}
    return dict(schema='kui.smash.collection-focused.v2',status='PASS',source_commit=commit,source_dirty=dirty,
        text_diagnostic_profile=bool(text_profile),hardware_timing_measured=False,install_files=installs,
        reports=reports,artifacts_sha256=artifacts,modules=modules,collection_session_profile=True,
        no_Background_or_prefetch_for_Smash=True,no_CE_retirement_or_cold_return_claim=not ce_controller_profile,
        CE_return_controller_profile=ce_controller_profile,CE_return_hardware_verified=False,
        limits='Original native GIN and both receipt transports execute linked SH. Physical media, full gameplay, save durability and console elapsed time remain hardware tests.')


def audit(outer,native,vc2,swirl,originals=None,swirl_originals=None,allow_dirty=False,accepted_baseline=None,text_profile=False,
          native_gin_proof=None,transport_proof=None,installer_proof=None,ce_return_proofs=None,ce_late_proofs=None,
          ce_integrity_proofs=None,swirl_return_proof=None,ce_ram_clear_proofs=None,ce_fault_chain_proofs=None,
          ce_raw_shadow_proofs=None,ce_raw_image_proofs=None,ce_startup_proofs=None,
          native_options_proof=None,genesis_options_source_proof=None):
    if read_elf(Path(native)/'stage.elf')[4].get('__kui_smash_native_gin_adoption')==(1,0,0,0xfff1):
        return session_audit(outer,native,vc2,swirl,originals,allow_dirty,text_profile,
            native_gin_proof,transport_proof,installer_proof,ce_return_proofs,ce_late_proofs,
            ce_integrity_proofs,swirl_return_proof,swirl_originals,ce_ram_clear_proofs,ce_fault_chain_proofs,
            ce_raw_shadow_proofs,ce_raw_image_proofs,ce_startup_proofs,
            native_options_proof,genesis_options_source_proof)
    outer,native,vc2,swirl=map(Path,(outer,native,vc2,swirl))
    folders={'outer':outer,'native':native,'vc2':vc2,'swirl':swirl}
    modules={'outer':'retail-smash-menu.kui','native':'retail-smash-native-menu.kui',
        'vc2':'retail-smash-vc2-child.kui','swirl':'retail-smash-swirl-child.kui'}
    if accepted_baseline:
        require(originals is None and swirl_originals is None,'Choose original files or accepted witness, not both')
        cold,witness=accepted_audit(outer,native,accepted_baseline)
        reports={'cold_loading_and_native_return':cold,'accepted_original_asset_witnesses':witness}
    else:
        require(originals is not None and swirl_originals is not None,'Provide original files or --accepted-baseline')
        originals,swirl_originals=Path(originals),Path(swirl_originals)
        reports={'cold_loading_and_native_return':cold_audit(outer,native,originals/'MDE_US.BIN')}
        image=read_elf(outer/'handoff.elf');sample=(originals/'VIRTCOP2/SAMPLE.BIN').read_bytes()
        reports['three_way_forward']=[forward_case(image[0],image[2],image[4],kind,sample) for kind in
            ('ce_vc2','ce_swirl','ce_bad_sample_crc','ce_bad_capsule_crc','ce_wrong_profile','ce_unknown_fallback')]
        gin=(originals/'GINXFER.BIN').read_bytes()
        reports['redirect']=[exercise_redirect(image[0],image[2],image[4],gin,k) for k in
            ('matched_gin','wrong_code_crc','partial_range','invalid_runtime')]
        reports['original_CE_child']=original_audit(gin,(originals/'1ST_READ.BIN').read_bytes(),sample,
            (swirl_originals/'SWIRL/SAMPLE.BIN').read_bytes())
    subprocess.run([sys.executable,str(ROOT/'tests/test_smash_ce_core.py')],cwd=ROOT,check=True,
        stdout=subprocess.DEVNULL)
    reports['host_cores']='PASS ASan/UBSan exact profile/capture/CRC/disjoint wire cases'
    artifacts={};envelopes={};install={};build_ids=set()
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    for role,folder in folders.items():
        package=folder/modules[role];wire=package.read_bytes();envelopes[role]=verify(wire);build_ids.add(envelopes[role]['build'])
        entry=read_elf(folder/'entry.elf');payload,_=flatten_elf(entry[0])
        receipt=json.loads((folder/'entry.compile.json').read_text())
        require(receipt.get('commit')==commit and receipt.get('source_dirty') is False and
            receipt.get('elf_sha256')==sha(entry[0]),'Missing clean current entry link receipt: '+role)
        require(payload==wire[64:],'Package differs from linked entry: '+role)
        require(physical_image(read_elf(folder/'stage.elf'))==(folder/'stage.bin').read_bytes(),
            'Cold stage binary differs from actual linked ELF: '+role)
        require(embedded(entry,'__retail_stage_blob_start','__retail_stage_blob_end')==(folder/'stage.bin').read_bytes(),
            'Entry embeds different cold stage: '+role)
        if not text_profile:
            require('_kui_smash_menu_loader_phase_trace' not in read_elf(outer/'handoff.elf')[4],
                'Normal delivery contains native terminal timing profile')
            require('__smash_phase_trace_seed' not in read_elf(native/'stage.elf')[4],
                'Normal native child contains terminal timing profile')
        for file in ('entry.elf','stage.elf','resident-sci.elf',modules[role]):artifacts[role+'/'+file]=sha((folder/file).read_bytes())
    require(len(build_ids)==1,'Smash modules do not share one build commit')
    worker=read_elf(native/'native-worker.elf')
    require(worker[4]['__kui_native_worker_span'][0]==0x12000,'Native VMU worker is not72KiB')
    reports['native_worker_bytes']=0x12000
    reports['outer_compact_word_copy']=compact_memcpy_audit(outer/'resident-sci.elf')
    reports['native_cached_boot_CRC']=cached_boot_crc_audit(native/'stage.elf')
    reports['native_linked_read_through']=native_read_cache_audit(native/'stage.elf')
    if text_profile:
        stage=read_elf(native/'stage.elf');outer_image=read_elf(outer/'handoff.elf')
        require('__smash_phase_text_capability' in stage[4] and '_smash_phase_stage_text_write' in stage[4]
            and '_kui_smash_phase_file_write' in stage[4] and '_smash_phase_stage_report' not in stage[4],
            'TXT delivery requires its actual file writer and must omit the photo terminal')
        require('_kui_smash_menu_loader_phase_trace' in outer_image[4],
            'TXT delivery omits the original GIN/capsule timestamp source')
        marker=b'KUISTXT1'+struct.pack('<II',1,16)
        require((native/modules['native']).read_bytes()[64:].count(marker)==1,
            'Actual native module lacks exactly one TXT capability')
        reports['text_clock_native']=phase_clock_audit(native/'stage.elf')
        reports['text_clock_outer']=phase_clock_audit(outer/'handoff.elf',require_delta=False)
        commands=([sys.executable,'tests/test_smash_phase_file.py'],
            ['make','-f','Makefile.smash-phase-text-tests'],
            ['make','-f','Makefile.smash-phase-text-prepare-tests'],
            ['make','-f','Makefile.native-vmu-live-fat-memo-tests'],
            [sys.executable,'tests/test_native_vmu_stage_read_cache.py'])
        reports['text_host_checks']=[]
        for command in commands:
            result=subprocess.check_output(command,cwd=ROOT,text=True,stderr=subprocess.STDOUT)
            reports['text_host_checks'].append(dict(command=command,status='PASS',output=result))
        reports['diagnostic_scope']=dict(continues_after_worker_install=True,photo_terminal=False,
            output='KUI/apps/games/smash-native-phases.txt',bytes=4096,
            report_IO_outside_measured_intervals=True,post_install_SGA_BUP_options_timed=False,
            performance_fix_claimed=False)
    native_payload=(native/modules['native']).read_bytes()[64:]
    native_cap=struct.pack('<6I',0x4349554b,0x31305041,1,24,1848288,(~1848288)&0xffffffff)
    require(native_payload[0x1c0:0x1d8]==native_cap,'Native prefix lacks the immutable complete-source capability')
    native_stage=read_elf(native/'stage.elf')
    cold_seed=struct.pack('<8I',0x4349554b,0x31304d49,1,32,0,0,0,0)
    require(native_payload.count(cold_seed)==1 and '__smash_cold_image_seed' in native_stage[4],
        'Native child lacks its unique blank warm-source seed')
    require('_kui_smash_native_menu_full_source_owner' in native_stage[4] and
        '_kui_smash_native_menu_cached_full_source_owner' in native_stage[4],
        'Native child lacks fresh complete-source owner proof and cache wrapper')
    reports['native_warm_source_metadata']=dict(typed_prefix_capability=True,
        unique_blank_seed=True,complete_source_bytes=1848288)
    subprocess.run(['make','test-cold-image','test-smash-complete-source-owner'],cwd=ROOT,
        check=True,stdout=subprocess.DEVNULL)
    reports['native_fresh_complete_SHA_owner_core']='PASS authored fixtures, CRC collision/suffix corruption refusal; no original game data'
    subprocess.run(['make','test-smash-ce-gin'],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
    reports['CE_original_GIN_capture']=gin_handoff_audit(outer/'handoff.elf')

    for kind,folder in ((1,vc2),(2,swirl)):
        helper=(folder/'helper/helper.bin').read_bytes();prepared=(folder/'helper/prepared.bin').read_bytes()
        require(len(prepared)==49152 and prepared==helper+bytes(49152-len(helper)),'Prepared helper mismatch')
        descriptor=ce_profile(helper,kind,embedded=True);payload=(folder/modules['vc2' if kind==1 else 'swirl']).read_bytes()[64:]
        require(payload.count(descriptor)==1,'Missing/mismatched CE profile/helper descriptor')
        stage=read_elf(folder/'stage.elf')
        require(stage[4].get('__kui_smash_ce_gin_adoption',(None,))[0]==1,
            'CE child does not adopt the original GIN continuation')
        reports['CE_'+str(kind)+'_GIN_adoption']=gin_adoption_audit(
            folder/'entry.elf',folder/'stage.elf',folder/'resident-sci.elf')
        reports['CE_'+str(kind)+'_GIN_resume']=gin_resume_audit(folder/'stage.elf')
        require(stage[4]['__kui_smash_ce_child'][0]==kind,'Direct SAMPLE child profile differs')
        blob_at=stage[4]['__vc2_vmu_blob_start'][0]
        descriptor_at=stage[4]['__smash_ce_embedded_helper'][0]
        require(descriptor_at+32==blob_at,'Embedded helper header is not immediately before raw bytes')
        header=embedded(stage,'__smash_ce_embedded_helper','__vc2_vmu_blob_start')
        require(struct.unpack('<8I',header)==(0x5349554b,0x31484543,1,32,kind,len(helper),49152,0),
            'Embedded helper typed metadata differs')
        require(embedded(stage,'__vc2_vmu_blob_start','__vc2_vmu_blob_end')==helper,
            'Module embeds a different CE helper')
        # GUI reconstructs the separate late-load capsule from these exact
        # selected-module bytes. Only physical run fields change per launch.
        late=struct.unpack('<11I',object_bytes(stage,'_kui_vc2_vmu_late_seed'))
        require(late==(0x32554d56,0x44414f4c,2,44,49152,zlib.crc32(prepared),0,0,0,0,0),
            'Late helper seed differs from matched prepared helper')
        high=read_elf(folder/'helper/helper.elf')
        require(physical_image(high)==helper,'CE helper binary differs from actual linked ELF')
        # Unchanged component identity is independent of this stage's build.
        # Keep the actual accepted label: GCC string pooling can otherwise
        # move literals and alter code when a new build label shares a suffix.
        helper_proof=helper_body(helper,kind,REFERENCE_BUILD)
        require(sha(helper)==HELPER_PINS[kind]['reference_sha256'],
            'Normal CE helper must exactly reproduce the accepted whole component')
        low=read_elf(folder/'resident-sci.elf')
        require(low[4].get('__kui_smash_ce_return_probe_mode',(None,))[0]==0,
            'Normal collection delivery must not enable the live CE return observer')
        require('__smash_ce_return_probe_start' not in stage[4] and
            '__smash_ce_return_probe_end' not in stage[4] and '_smash_ce_return_prepared' not in stage[4],
            'Normal cold child retains an unqualified extra low return observer')
        require('_kui_smash_ce_return_admit' not in high[4],'Unqualified CE retirement prototype is linked')
        reports['CE_'+str(kind)]=dict(terminal_return=terminal_audit(folder/'resident-sci.elf',stage=folder/'stage.elf'),
            helper_code_sha256=sha(helper),helper_body=helper_proof,
            helper_component_build=helper_proof['build'],helper_qualified_source_commit=commit,
            helper_prepared_sha256=sha(prepared),profile_sha256=sha(descriptor),CE_return_to_menu_implemented=False)
        artifacts['CE'+str(kind)+'/helper.elf']=sha((folder/'helper/helper.elf').read_bytes())
    reports['embedded_helper_GUI_admission']=embedded_helper_audit((vc2/modules['vc2'],swirl/modules['swirl']))
    targets={'KUI/apps/games/retail-smash-menu.kui':outer/modules['outer'],
        'KUI/apps/games/retail-smash-native-menu.kui':native/modules['native'],
        'KUI/apps/games/retail-smash-vc2.kui':vc2/modules['vc2'],
        'KUI/apps/games/retail-smash-swirl.kui':swirl/modules['swirl'],
        'KUI/apps/games/smash-vc2-vmu-helper.bin':vc2/'helper/prepared.bin',
        'KUI/apps/games/smash-swirl-vmu-helper.bin':swirl/'helper/prepared.bin'}
    for target,path in targets.items():install[target]=dict(path=str(path.resolve()),sha256=sha(path.read_bytes()))
    sources=set(SOURCES)|set(COLD_SOURCES)|set(ORIGINAL_MODELS)|(set(TEXT_SOURCES) if text_profile else set())
    for folder in folders.values():
        for dependency in folder.rglob('*.d'):
            for token in re.findall(r'(?:src|include)/[^\s:]+',dependency.read_text().replace('\\\n',' ')):
                if (ROOT/token).is_file():sources.add(token)
    dirty_rows=subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).splitlines()
    dirty=bool(dirty_rows)
    if allow_dirty:
        require(all(row[3:]=='tools/check_smash_collection_forward.py' for row in dirty_rows),
            'Development exception permits only this qualification tool to be dirty')
    require(not dirty or allow_dirty,'Focused delivery qualification requires a clean source commit')
    require(next(iter(build_ids))==commit[:12],'Smash modules were built before the final commit')
    return dict(schema='kui.smash.collection-focused.v1',status='PASS',source_commit=commit,source_dirty=dirty,
        development_validation_only=bool(allow_dirty),
        source_sha256={p:sha((ROOT/p).read_bytes()) for p in sorted(sources)},install_files=install,
        artifacts_sha256=artifacts,modules=envelopes,reports=reports,
        loading_policy='All owned cold runs batch with instruction cache enabled; every512B CRC retained.',
        no_Background_or_prefetch_for_Smash=True,no_CE_retirement_or_cold_return_claim=True,
        hardware_timing_measured=False,text_diagnostic_profile=bool(text_profile))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('outer','native','vc2','swirl'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--originals',type=Path)
    p.add_argument('--swirl-originals',type=Path)
    p.add_argument('--accepted-baseline',type=Path,help='Pinned ab1 ZIP; reuse only clearly labelled original-asset observations')
    p.add_argument('--report',type=Path,required=True)
    p.add_argument('--allow-dirty',action='store_true',help='Development validation only; final packager still refuses dirty sources')
    p.add_argument('--text-profile',action='store_true',help='Qualify the nonterminal automatic native phase TXT diagnostic')
    p.add_argument('--native-gin-proof',type=Path,help='Reuse an exact ELF/test/decoder-bound native GIN execution report')
    p.add_argument('--transport-proof',type=Path,help='Reuse an exact ELF/source-bound SESSION1 receipt transport report')
    p.add_argument('--installer-proof',type=Path,help='Exact ELF-bound native installer/outer combined-stack envelope')
    p.add_argument('--native-options-proof',type=Path,
        help='Exact native-stage/source-bound original Options combo and reset-only publication proof')
    p.add_argument('--genesis-options-source-proof',type=Path,
        help='Exact original-MDE Save Game eligibility and Return-to-Menu source execution proof')
    p.add_argument('--ce-return-proof',action='append',default=[],metavar='KIND=PATH',
        help='Reuse a final artifact/source-bound CE controller execution report (kind1/2)')
    p.add_argument('--ce-late-proof',action='append',default=[],metavar='KIND=PATH',
        help='Reuse a final artifact/source-bound late transport installation report (kind1/2)')
    p.add_argument('--ce-integrity-proof',action='append',default=[],metavar='KIND=PATH',
        help='Reuse an exact linked 13-bit failure-mask and shared CRC proof (kind1/2)')
    p.add_argument('--ce-ram-clear-proof',action='append',default=[],metavar='KIND=PATH',
        help='Reuse an exact controller/source-bound RAM-clear and original interval proof (kind1/2)')
    p.add_argument('--ce-fault-chain-proof',action='append',default=[],metavar='KIND=PATH',
        help='Reuse an exact linked revision1 failure-to-renderer execution proof (kind1/2)')
    p.add_argument('--ce-raw-shadow-proof',action='append',default=[],metavar='KIND=PATH',
        help='Reuse an exact linked protected raw-reader capture/restore proof (kind1/2)')
    p.add_argument('--ce-raw-image-proof',action='append',default=[],metavar='KIND=PATH',
        help='Reuse an exact linked canonical raw-reader image/restore proof (kind1/2)')
    p.add_argument('--ce-startup-proof',action='append',default=[],metavar='KIND=PATH',
        help='Reuse an exact accepted-startup ABI and pre-NK write-trace proof (kind1/2)')
    p.add_argument('--swirl-return-proof',type=Path,help='Exact original Swirl wrapper/private-callback execution proof')
    a=p.parse_args()
    def role_proofs(rows):
        out={}
        for row in rows:
            kind,separator,path=row.partition('=')
            require(separator and kind in ('1','2') and path and int(kind) not in out,
                'Use one distinct KIND=PATH proof per CE kind1/2')
            out[int(kind)]=Path(path)
        return out
    result=audit(a.outer,a.native,a.vc2,a.swirl,a.originals,a.swirl_originals,a.allow_dirty,a.accepted_baseline,a.text_profile,
        a.native_gin_proof,a.transport_proof,a.installer_proof,role_proofs(a.ce_return_proof),role_proofs(a.ce_late_proof),
        role_proofs(a.ce_integrity_proof),a.swirl_return_proof,role_proofs(a.ce_ram_clear_proof),
        role_proofs(a.ce_fault_chain_proof),role_proofs(a.ce_raw_shadow_proof),role_proofs(a.ce_raw_image_proof),
        role_proofs(a.ce_startup_proof),a.native_options_proof,a.genesis_options_source_proof)
    a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'status':'PASS','report':str(a.report),'install_files':list(result['install_files'])}))
