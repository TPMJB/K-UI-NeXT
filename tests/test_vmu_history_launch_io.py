#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real FAT32/exFAT device-I/O budgets for a complete pending-card launch.

Optionally compare a previous source file, while compiling all other code and
using the same actual filesystem fixture. Console elapsed time is not inferred.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def audit(reference=None,reference_library=None,reference_diskio=None):
    spec=importlib.util.spec_from_file_location('vmu_library_fixture',ROOT/'tests/test_vmu_library.py')
    library=importlib.util.module_from_spec(spec);spec.loader.exec_module(library)
    config=(ROOT/'config/ffconf.h').read_text();old='#define FF_USE_MKFS 0';assert config.count(old)==1
    outputs={}
    with tempfile.TemporaryDirectory(prefix='kui-history-launch-io-') as directory:
        folder=Path(directory);header=folder/'ffconf-test.h';header.write_text(config.replace(old,'#define FF_USE_MKFS 1'))
        flags=[*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
            '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie',
            '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Iinclude','-I.deps/fatfs/source',
            '-include',str(header)]
        sources=[name for name in library.SOURCES if name!='tests/test_vmu_library.c']
        variants=[('current',ROOT/'src/core/vmu_history.c',None,None)]
        if reference or reference_library or reference_diskio:
            variants.insert(0,('reference',Path(reference).resolve() if reference else ROOT/'src/core/vmu_history.c',
                              Path(reference_library).resolve() if reference_library else None,
                              Path(reference_diskio).resolve() if reference_diskio else None))
        for label,source,library_source,diskio_source in variants:
            binary=folder/label
            variant_sources=[str(library_source) if name=='src/core/vmu_library.c' and library_source else name for name in sources]
            if diskio_source:
                variant_sources=[str(diskio_source) if name=='src/core/diskio.c' else name for name in variant_sources]
                stub=folder/'reference-cache-stub.c'
                stub.write_text('#include "kui/media.h"\n'
                    'bool kui_media_read_cache_begin(void *memory,size_t bytes) {(void)memory;(void)bytes;return false;}\n'
                    'void kui_media_read_cache_end(void) {}\n')
                variant_sources.append(str(stub))
            subprocess.run([*flags,*(['-DKUI_HISTORY_REUSE_BUDGET=1'] if label=='current' else []),
                '-Wl,--wrap=kui_vmu_library_check_allocation',
                'tests/test_vmu_history_launch_io.c',str(source),'src/core/vmu_history_record.c',
                'src/core/vmu_history_session.c',*variant_sources,'-o',str(binary)],cwd=ROOT,check=True)
            run=subprocess.run([str(binary)],cwd=ROOT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',
                UBSAN_OPTIONS='halt_on_error=1'),capture_output=True,text=True,check=True,timeout=180)
            outputs[label]=[json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')]
        if reference or reference_library or reference_diskio:
            for before,after in zip(outputs['reference'],outputs['current'],strict=True):
                assert (before['filesystem'],before['actual_cluster'],before['scenario'])==(
                    after['filesystem'],after['actual_cluster'],after['scenario'])
                assert before['disk_sha256']==after['disk_sha256'], 'Publication/retention bytes changed'
                assert after['read_sectors']<=before['read_sectors'] and after['reads']<=before['reads']
                assert (before['writes'],before['write_sectors'],before['syncs'])==(
                    after['writes'],after['write_sectors'],after['syncs']), 'Durable publications changed'
                assert before['retained_image_read_sectors']>=512 and after['retained_image_read_sectors']==512
            large=[(before,after) for before,after in zip(outputs['reference'],outputs['current'],strict=True)
                   if after['actual_cluster']==131072]
            assert len(large)==2 and all(after['reads']<before['reads'] for before,after in large)
            if reference_diskio:
                assert all(after['allocation_reads']<before['allocation_reads'] and
                    after['allocation_walks']==before['allocation_walks'] for before,after in zip(
                        outputs['reference'],outputs['current'],strict=True)), 'Every complete ownership walk must remain'
                assert all(after['retained_image_read_calls']==before['retained_image_read_calls'] for before,after in zip(
                    outputs['reference'],outputs['current'],strict=True)), 'Image verification must not use the metadata cache'
            else:
                assert all(after['multi_sector_read_calls']<before['multi_sector_read_calls'] and
                           after['single_sector_read_calls']<=before['single_sector_read_calls']
                           for before,after in large)
            if reference_library:
                assert all(before['retained_image_read_sectors']==after['retained_image_read_sectors']==512 and
                    before['retained_image_read_calls']==64 and after['retained_image_read_calls']==8
                    for before,after in large), 'Batching must retain both complete image passes'
    return dict(status='PASS',scope='shared complete session-begin path; real FatFs/device callbacks',
        read_command_evidence='single/multi-sector device callback counts; console SD elapsed time is not inferred',
        tested_scenarios=['unchanged','changed'],tested_cluster_bytes={'FAT32':[512],'exFAT':[4096,131072]},
        measured_console_time=False,verification_passes_per_trusted_image=2,
        cross_invocation_corruption_device_fault_cancel_and_identity_refused=True,results=outputs,
        source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in (
            'src/core/vmu_history.c','src/core/vmu_history_session.c','src/core/vmu_library.c','src/core/diskio.c','include/kui/media.h',
            'tests/test_vmu_history_launch_io.c','tests/test_vmu_history_launch_io.py')},
        reference_source_sha256=hashlib.sha256(Path(reference).read_bytes()).hexdigest() if reference else None,
        reference_library_source_sha256=hashlib.sha256(Path(reference_library).read_bytes()).hexdigest() if reference_library else None,
        reference_diskio_source_sha256=hashlib.sha256(Path(reference_diskio).read_bytes()).hexdigest() if reference_diskio else None)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--reference-source',type=Path)
    parser.add_argument('--reference-library-source',type=Path)
    parser.add_argument('--reference-diskio-source',type=Path)
    parser.add_argument('--output',type=Path);a=parser.parse_args();result=audit(a.reference_source,a.reference_library_source,a.reference_diskio_source)
    text=json.dumps(result,indent=2)+'\n'
    if a.output:a.output.write_text(text)
    print(text,end='')
