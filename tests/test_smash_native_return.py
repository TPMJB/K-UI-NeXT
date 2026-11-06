#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute production menu-worker retirement over the existing native fixture."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCES=('src/core/vmu_native_heap.c','src/core/vmu_native_runtime.c','src/core/vmu_native_layout.c',
    'src/core/vmu_native_live.c','src/core/vmu_native_lifetime.c','src/core/vmu_native_profile.c',
    'src/core/vmu_native_discovery.c','src/core/vmu_native_patch.c','src/core/hash.c','src/core/data.c',
    'src/core/vmu_durable.c','src/core/vmu_store.c','src/core/vmu_emulation.c','src/core/retail_image.c',
    'src/loader/vmu_readonly_overlay.c','src/loader/vmu_volatile_chain.c','src/loader/sd_reader.c',
    'src/loader/vmu_backing.c','src/dreamcast/sci_sd_storage.c')
DEFINES=('KUI_VMU_EXPERIMENTAL','KUI_VMU_NATIVE_CACHED_CODE','KUI_NATIVE_VMU_ONLY',
    'KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION','KUI_NATIVE_SPLIT_CONTROLLER_GENERAL',
    'KUI_NATIVE_GENERAL_LEAN_LAYOUT','KUI_NATIVE_VMU_LITE','KUI_VMU_VIRTUAL_A1','KUI_SMASH_NATIVE_MENU')

class RetirementTest(unittest.TestCase):
    def test_production_worker(self):
        with tempfile.TemporaryDirectory(prefix='kui-smash-retirement-') as folder:
            binary=Path(folder)/'worker-retirement'
            command=[*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
                '-Wall','-Wextra','-Werror','-Wpedantic','-ffunction-sections','-fdata-sections',
                '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
                '-Iinclude','-Isrc/loader',*['-D'+name+'=1' for name in DEFINES],
                'tests/test_smash_native_return.c',*SOURCES,'-Wl,--gc-sections','-o',str(binary)]
            built=subprocess.run(command,cwd=ROOT,text=True,capture_output=True,timeout=120)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],cwd=ROOT,text=True,capture_output=True,timeout=60,
                env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            print(run.stdout,end='')

if __name__=='__main__':unittest.main()
