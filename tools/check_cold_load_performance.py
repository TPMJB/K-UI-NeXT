#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused linked qualification of common cold-cache policy and capsule calls."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from runtime_package import flatten_elf
from test_cold_load_scope import audit as scope_audit
from test_smash_native_fast_crc import audit as fast_crc
from check_vc2_stage_cached_relay import audit as relay_audit
from check_smash_menu_loader import combined_stacks,return_stack
from test_smash_menu_loader_runtime import exercise as forward_case
from test_smash_menu_return_runtime import exercise as return_case
from smash_native_menu_resident import inspect as inspect_reader
from check_native_vmu_only import callgraph_stacks
from check_vmu_native_persistent import sdk_coherence_admission

SOURCES=(
    'include/kui/cold_load.h','src/loader/cold_load_scope.S','src/loader/cold_load_crc32.c',
    'src/loader/stage_fast_crc32.h','src/loader/retail_stage.S','src/loader/retail_stage.c',
    'src/loader/smash_menu_loader.c','src/loader/smash_menu_return_loader.c',
    'Makefile.smash-menu-loader','Makefile.smash-native-menu','tools/check_cold_load_performance.py',
    'tests/test_cold_load_scope.py','tests/test_smash_native_fast_crc.py',
    'tests/test_smash_menu_loader_runtime.py','tests/test_smash_menu_return_runtime.py',
    'tests/test_smash_menu_loader_entry.py','tools/check_vc2_stage_cached_relay.py',
    'tools/check_smash_menu_loader.py','tools/check_native_vmu_only.py',
    'tools/check_vmu_native_worker_gateway.py','tools/check_vmu_native_persistent.py',
    'tests/test_native_standalone_foreground_gin.py','tests/test_vmu_native_sdk_poll_execution.py',
    'tests/test_vmu_native_heap_execution.py','tools/profile_vmu_native_release_sh.py')


def sha(data):return hashlib.sha256(data).hexdigest()


def audit(outer,child,mde):
    outer,child=Path(outer),Path(child)
    original=Path(mde).read_bytes()
    assert sha(original)=='04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'
    reports={}
    for name,elf in (('forward',outer/'handoff.elf'),('return',child/'return-loader.elf')):
        reports[name]=dict(scope=scope_audit(elf),
            full_byte_crc=fast_crc(elf,helper='_kui_cold_load_crc32_leaf',relay='_kui_cold_load_crc32'))
    for name,folder in (('outer',outer),('child',child)):
        stage=read_elf(folder/'stage.elf')
        assert stage[4].get('__kui_retail_cold_fast_crc')==(1,0,0,0xfff1)
        reports[name+'_common_relay']=relay_audit(folder/'stage.elf',marker_name='__kui_retail_cold_instruction_cache')
    image=read_elf(outer/'handoff.elf')
    reports['forward']['runtime_cases']=[forward_case(image[0],image[2],image[4],kind)
        for kind in ('shining_force','bad_capsule_crc','nonnative_fallback')]
    reports['forward']['stack']=combined_stacks(outer)
    image=read_elf(child/'return-loader.elf')
    cases=[return_case(image[0],image[2],image[4],original,kind)
        for kind in ('sf_return','bad_capsule_crc')]
    reports['return']['runtime_cases']=cases
    worker=read_elf(child/'native-worker.elf')
    low=inspect_reader((child/'resident-sci.elf').read_bytes())['low']
    worker_stacks=callgraph_stacks(child,worker,low,1,sdk_coherence_admission(worker))
    reports['return']['stack']=return_stack(child,{'cases':cases},{'stack_audit':worker_stacks})
    capsule_bytes={}
    for name,folder in (('forward',child),('return',outer)):
        payload,_=flatten_elf((folder/'entry.elf').read_bytes())
        size=(len(payload)+511)&~511
        capsule_bytes[name]=dict(padded_bytes=size,SD_blocks_with_CRC16=size//512,
            previous_crc_table_reads=size*2,current_crc_table_reads=size,
            bulk_reader_instruction_cache=True,mutable_operand_cache=False)
    artifacts={name+'/'+file:sha((folder/file).read_bytes())
        for name,folder,files in (('outer',outer,('stage.elf','entry.elf','handoff.elf','resident-sci.elf')),
                                  ('child',child,('stage.elf','entry.elf','return-loader.elf','resident-sci.elf','native-worker.elf')))
        for file in files}
    return dict(schema='kui.cold-load-performance.v1',status='PASS',reports=reports,
        work_counts=capsule_bytes,artifacts_sha256=artifacts,
        source_sha256={name:sha((ROOT/name).read_bytes()) for name in SOURCES},
        hardware_timing_measured=False,no_game_or_worker_RAM_added=True,
        live_resident_CRC_unchanged=True,every_SD_block_CRC_retained=True,
        limits='Linked ABI/cache/CRC/stack and bounded loader behavior. Actual console elapsed times remain hardware measurements.')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--outer',type=Path,required=True)
    p.add_argument('--child',type=Path,required=True);p.add_argument('--mde',type=Path,required=True)
    p.add_argument('--report',type=Path);a=p.parse_args()
    result=audit(a.outer,a.child,a.mde);text=json.dumps(result,indent=2)+'\n'
    if a.report:a.report.write_text(text)
    print(text,end='')
