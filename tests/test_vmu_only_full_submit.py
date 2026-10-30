#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual full-submit control and reject incompatible selectors."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SELECTORS={
    'KUI_NATIVE_VMU_ONLY':1,'KUI_NATIVE_VMU_ONLY_FULL_SUBMIT':1,
    'KUI_VMU_NATIVE_PERSISTENT_PROBE':1,'KUI_VMU_NATIVE_CACHED_CODE':1,
    'KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION':1,'KUI_NATIVE_SPLIT_CONTROLLER_GENERAL':1,
    'KUI_NATIVE_GENERAL_LEAN_LAYOUT':1,'KUI_VMU_EXPERIMENTAL':1,
}
SOURCES=[
    'src/core/vmu_native_heap.c','src/core/vmu_native_runtime.c','src/core/vmu_native_layout.c',
    'src/core/vmu_native_live.c','src/core/vmu_native_lifetime.c','src/core/vmu_native_profile.c',
    'src/core/vmu_native_discovery.c','src/core/vmu_native_patch.c','src/core/hash.c','src/core/data.c',
    'src/core/vmu_durable.c','src/core/vmu_store.c','src/core/vmu_emulation.c','src/core/retail_image.c',
    'src/loader/vmu_readonly_overlay.c','src/loader/vmu_volatile_chain.c','src/loader/sd_reader.c',
    'src/loader/vmu_backing.c','src/dreamcast/sci_sd_storage.c',
]
cc=shlex.split(os.environ.get('HOST_CC','cc'))
with tempfile.TemporaryDirectory(prefix='kui-vmu-only-full-submit-') as tmp:
    directory=Path(tmp)
    config=directory/'same-build'/'vmu-only-config'
    for makefile,expected in [('Makefile.vmu-native-vmu-only-full-submit',1),
                              ('Makefile.vmu-native-vmu-only',0),
                              ('Makefile.vmu-native-vmu-only-full-submit',1)]:
        subprocess.run(['make','--no-print-directory','-s','-f',makefile,
            'BUILD='+str(config.parent),str(config)],check=True,cwd=ROOT)
        assert f'FULL_SUBMIT={expected}\n' in config.read_text(),config.read_text()
    full_config=config.parent/'vmu-only-full-submit-config'
    subprocess.run(['make','--no-print-directory','-s','-f','Makefile.vmu-native-vmu-only-full-submit',
        'BUILD='+str(config.parent),str(full_config)],check=True,cwd=ROOT)
    assert full_config.read_text()=='KUI_NATIVE_VMU_ONLY_FULL_SUBMIT=1\n'
    baseline={k:v for k,v in SELECTORS.items() if k!='KUI_NATIVE_VMU_ONLY_FULL_SUBMIT'}
    subprocess.run([*cc,'-std=c11','-Werror','-Iinclude',
        *(f'-D{k}={v}' for k,v in baseline.items()),'-x','c','-','-fsyntax-only'],
        input='#include "kui/native_isolation.h"\n_Static_assert(KUI_NATIVE_VMU_ONLY_FULL_SUBMIT==0,"Control must default off");\n',
        text=True,check=True,cwd=ROOT)
    for changes,accepted in [({},True),({'KUI_NATIVE_VMU_ONLY_FULL_SUBMIT':2},False),
                             ({'KUI_NATIVE_VMU_ONLY':0},False)]:
        values={**SELECTORS,**changes}
        result=subprocess.run([*cc,'-std=c11','-Werror','-Iinclude',
            *(f'-D{k}={v}' for k,v in values.items()),'-x','c','-','-fsyntax-only'],
            input='#include "kui/native_isolation.h"\n',text=True,capture_output=True,cwd=ROOT)
        assert (result.returncode==0)==accepted,result.stderr
    binary=directory/'full-submit'
    subprocess.run([*cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-ffunction-sections','-fdata-sections','-fsanitize=address,undefined',
        '-fno-omit-frame-pointer','-fno-pie','-no-pie','-Iinclude','-Isrc/loader',
        *(f'-D{k}={v}' for k,v in SELECTORS.items()),
        'tests/test_vmu_only_full_submit.c',*SOURCES,'-Wl,--gc-sections','-o',str(binary)],
        check=True,cwd=ROOT)
    subprocess.run([str(binary)],check=True,cwd=ROOT,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
