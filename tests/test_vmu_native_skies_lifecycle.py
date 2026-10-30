#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare split/full submit over identical uniform-RX callback/save lifecycles."""
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
    for enabled in (0,1):
        output=Path(temporary)/('lifecycle-'+str(enabled))
        subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),*FLAGS,
            '-DKUI_NATIVE_VMU_ONLY_FULL_SUBMIT='+str(enabled),
            'tests/test_vmu_native_skies_lifecycle.c',*SOURCES,'-Wl,--gc-sections','-o',str(output)],
            cwd=ROOT,check=True)
        result=subprocess.run([str(output)],cwd=ROOT,text=True,capture_output=True,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
        if result.returncode:
            raise RuntimeError('Lifecycle mode '+str(enabled)+' failed:\n'+result.stdout+result.stderr)
        print(result.stdout,end='')
        match=re.search(r'mode=(\d+) polls=(\d+) callbacks=(\d+) durable_roundtrips=(\d+) digest=([0-9a-f]{8})',result.stdout)
        assert match and int(match[1])==enabled and tuple(map(int,match.group(2,3,4)))==(54,54,6),result.stdout
        results.append(match[5])
    assert results[0]==results[1], 'Split/full submit changed physical RX or durable card bytes'
print('Split and full submit produced byte-identical physical/virtual responses and durable saved blocks')
