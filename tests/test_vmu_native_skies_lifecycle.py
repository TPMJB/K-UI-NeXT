#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare 72/112 KiB split/full uniform-RX callback/save lifecycles."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SOURCES=('src/core/vmu_native_heap.c','src/core/vmu_native_runtime.c',
    'src/core/vmu_native_layout.c','src/core/vmu_native_live.c','src/core/vmu_native_lifetime.c',
    'src/core/vmu_native_profile.c','src/core/vmu_native_discovery.c','src/core/vmu_native_patch.c',
    'src/core/hash.c','src/core/data.c','src/core/vmu_durable.c','src/core/vmu_store.c',
    'src/core/vmu_emulation.c','src/core/retail_image.c','src/loader/vmu_readonly_overlay.c',
    'src/loader/vmu_volatile_chain.c','src/loader/sd_reader.c','src/loader/vmu_backing.c',
    'src/dreamcast/sci_sd_storage.c')
FLAGS=('-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
    '-ffunction-sections','-fdata-sections','-fsanitize=address,undefined',
    '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie',
    '-Iinclude','-Isrc/loader','-DKUI_VMU_EXPERIMENTAL=1',
    '-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1','-DKUI_VMU_NATIVE_CACHED_CODE=1',
    '-DKUI_NATIVE_VMU_ONLY=1','-DKUI_NATIVE_SPLIT_CONTROLLER_ISOLATION=1',
    '-DKUI_NATIVE_SPLIT_CONTROLLER_GENERAL=1','-DKUI_NATIVE_GENERAL_LEAN_LAYOUT=1')

with tempfile.TemporaryDirectory(prefix='kui-skies-lifecycle-') as temporary:
    results=[]
    for span,lite,enabled in ((0x12000,1,0),(0x12000,1,1),(0x1c000,0,0),(0x1c000,0,1)):
        variant=f'{span//1024}k-'+('full' if enabled else 'split')
        output=Path(temporary)/('lifecycle-'+variant)
        subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),*FLAGS,
            '-DKUI_NATIVE_VMU_LITE='+str(lite),
            '-DKUI_SKIES_TEST_LEASE_BYTES='+str(span),
            '-DKUI_NATIVE_VMU_ONLY_FULL_SUBMIT='+str(enabled),
            'tests/test_vmu_native_skies_lifecycle.c',*SOURCES,'-Wl,--gc-sections','-o',str(output)],
            cwd=ROOT,check=True)
        result=subprocess.run([str(output)],cwd=ROOT,text=True,capture_output=True,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        if result.returncode:
            raise RuntimeError('Lifecycle variant '+variant+' failed:\n'+result.stdout+result.stderr)
        print(result.stdout,end='')
        match=re.search(r'lease=(\d+) mode=(\d+) polls=(\d+) callbacks=(\d+) durable_roundtrips=(\d+) digest=([0-9a-f]{8})',result.stdout)
        assert match and tuple(map(int,match.group(1,2)))==(span,enabled) and tuple(map(int,match.group(3,4,5)))==(54,54,6),result.stdout
        results.append(match[6])
    assert len(set(results))==1, 'Allocation size or submit mode changed physical RX or durable card bytes'
print('All four 72/112 KiB split/full variants produced byte-identical physical/virtual responses and durable saved blocks')
