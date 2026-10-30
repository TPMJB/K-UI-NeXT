#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run actual GD/capture logic and the resident's exact read adapter body."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/loader/retail_resident.c').read_text()
def function(begin):
    start=source.index(begin)
    return source[start:source.index('\n}\n',start)+3]
read=function('static int read_sectors(void *unused, uint32_t lba, uint32_t count,')
capture=function('static void capture_native_fault(uint32_t function, uint32_t current_param)')
cc=shlex.split(os.environ.get('HOST_CC','cc'))
flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
       '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',
       '-Iinclude','-Isrc/loader','-DKUI_NATIVE_VMU_ONLY=1']
with tempfile.TemporaryDirectory(prefix='kui-native-vmu-io-') as tmp:
    out=Path(tmp)
    (out/'native_vmu_io_adapter_under_test.h').write_text(read+'\n'+capture)
    for name,sources in [('gd',['tests/test_native_vmu_io_diagnostic.c','src/core/retail_gd.c']),
                         ('adapter',['tests/test_native_vmu_io_adapter.c'])]:
        binary=out/name
        subprocess.run([*cc,*flags,'-I',str(out),*sources,'-o',str(binary)],check=True,cwd=ROOT)
        subprocess.run([str(binary)],check=True,cwd=ROOT,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
