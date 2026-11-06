#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reuse authenticated original-asset observations, never fabricate game data.

Current linked code is independently exercised below. Historical observations
remain explicitly historical even when their scalar/source contracts match.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[1]
BASELINE_COMMIT='ab1b145b33b4b4cf252f61d7f5cfb06a55ac5f87'
BASELINE_ZIP_SHA256='38f0170b0ce41c5a84d436dadebacf8ccc2d42f72b5dee9616eea0cdec1cb24c'
ORIGINAL_MODELS=(
 'tests/test_smash_ce_child_gin.py','tests/test_smash_menu_handoff_gin.py',
 'tests/test_smashpack_native_engine_gin.py','tests/test_native_standalone_foreground_gin.py',
 'tests/test_smash_menu_loader_runtime.py','tests/test_smash_menu_return_runtime.py',
 'include/kui/smash_ce_child.h','src/core/smash_menu_return.c',
 'src/loader/smash_menu_return_loader.c')
def sha(data):return hashlib.sha256(data).hexdigest()
def require(ok,message):
    if not ok:raise ValueError(message)
def baseline(path):
    wire=Path(path).read_bytes()
    require(sha(wire)==BASELINE_ZIP_SHA256,'Accepted witness ZIP is not the pinned delivered ab1 archive')
    with zipfile.ZipFile(path) as archive:
        report=json.loads(archive.read('evidence/smash-linked.json'))
    require(report.get('schema')=='kui.smash.collection-focused.v1' and report.get('status')=='PASS' and
        report.get('source_commit')==BASELINE_COMMIT and report.get('source_dirty') is False,
        'Accepted witness is not the clean pinned ab1 qualification')
    sources={}
    for name in ORIGINAL_MODELS:
        old=subprocess.check_output(['git','show',BASELINE_COMMIT+':'+name],cwd=ROOT)
        current=(ROOT/name).read_bytes()
        require(current==old,'Original-asset scalar witness source changed: '+name)
        sources[name]=sha(current)
    # Typed-record discovery was hardened since ab1. It is rerun in current
    # host/GUI checks; the original argv/IP capture contract must be identical.
    name='src/core/smash_ce_child.c'
    old=subprocess.check_output(['git','show',BASELINE_COMMIT+':'+name],cwd=ROOT)
    current=(ROOT/name).read_bytes()
    before=b'size_t kui_smash_ce_child_find('
    after=b'unsigned kui_smash_ce_child_identify('
    require(old.split(before)[0]==current.split(before)[0] and
        old.split(after)[1]==current.split(after)[1],
        'Original CE capture/owner contract changed')
    return report,dict(schema='kui.smash.accepted-original-witness.v1',
        archive_sha256=BASELINE_ZIP_SHA256,source_commit=BASELINE_COMMIT,
        original_asset_cases_reexecuted=False,current_source_sha256=sources,
        unchanged_CE_capture_contract_sha256=sha(current.split(after)[1]),
        original_CE_child=report['reports']['original_CE_child'],
        historical_three_way_forward=report['reports']['three_way_forward'],
        historical_redirect=report['reports']['redirect'],
        historical_native_return=report['reports']['cold_loading_and_native_return']['reports']['return']['runtime_cases'],
        historical_artifacts_sha256=report['artifacts_sha256'],
        limits='Original private executable cases are authenticated prior observations, not reruns against current modules. Current module behavior is tested separately with explicitly authored fixtures; no hardware or full game execution claim.')

def audit(outer,child,path):
    from check_vmu_native_worker_gateway import read_elf
    from runtime_package import flatten_elf
    from test_cold_load_scope import audit as scope_audit
    from test_smash_native_fast_crc import audit as fast_crc
    from check_vc2_stage_cached_relay import audit as relay_audit
    from check_smash_menu_loader import combined_stacks,return_stack
    from test_smash_menu_loader_runtime import exercise as forward_case
    from smash_native_menu_resident import inspect as inspect_reader
    from check_native_vmu_only import callgraph_stacks
    from check_vmu_native_persistent import sdk_coherence_admission
    from test_smash_cold_image_handoff import audit as warm_audit
    from test_smash_full_source_scope import audit as source_scope_audit
    prior,witness=baseline(path)
    reports={}
    for name,elf in (('forward',outer/'handoff.elf'),('return',child/'return-loader.elf')):
        reports[name]=dict(scope=scope_audit(elf),
            full_byte_crc=fast_crc(elf,helper='_kui_cold_load_crc32_leaf',relay='_kui_cold_load_crc32'))
    for name,folder in (('outer',outer),('child',child)):
        stage=read_elf(folder/'stage.elf')
        require(stage[4].get('__kui_retail_cold_fast_crc')==(1,0,0,0xfff1),'Cold full CRC marker missing')
        reports[name+'_common_relay']=relay_audit(folder/'stage.elf',marker_name='__kui_retail_cold_instruction_cache')
    image=read_elf(outer/'handoff.elf')
    reports['forward']['runtime_cases']=[forward_case(image[0],image[2],image[4],kind)
        for kind in ('shining_force','bad_capsule_crc','nonnative_fallback')]
    reports['forward']['stack']=combined_stacks(outer)
    reports['forward']['authored_warm_handoff']=warm_audit(outer/'handoff.elf')
    reports['native_full_source_cache_wrapper']=source_scope_audit(child/'stage.elf')
    worker=read_elf(child/'native-worker.elf')
    low=inspect_reader((child/'resident-sci.elf').read_bytes())['low']
    worker_stacks=callgraph_stacks(child,worker,low,1,sdk_coherence_admission(worker))
    observed=prior['reports']['cold_loading_and_native_return']['reports']['return']['runtime_cases']
    stack=return_stack(child,{'cases':observed},{'stack_audit':worker_stacks})
    stack['accepted_baseline_original_helper_observed_bytes']=stack.pop('actual_return_C_and_original_helpers_observed_bytes')
    stack['original_helper_observation_reexecuted']=False
    stack['observation_source_archive_sha256']=BASELINE_ZIP_SHA256
    reports['return']['current_compiler_and_callback_stack']=stack
    reports['return']['original_asset_runtime_cases_reexecuted']=False
    counts={}
    for name,folder in (('forward',child),('return',outer)):
        payload,_=flatten_elf((folder/'entry.elf').read_bytes());size=(len(payload)+511)&~511
        counts[name]=dict(padded_bytes=size,SD_blocks_with_CRC16=size//512,
            bulk_reader_instruction_cache=True,mutable_operand_cache=False)
    return dict(schema='kui.cold-load-performance.accepted-originals.v1',status='PASS',
        reports=reports,work_counts=counts,every_SD_block_CRC_retained=True,
        original_asset_cases_reexecuted=False,hardware_timing_measured=False),witness
