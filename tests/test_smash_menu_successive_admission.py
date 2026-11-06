#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the real fresh semantic admission path on successive cold starts."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCES=('tests/test_smash_menu_successive_admission.c','src/loader/vmu_persist_stage.c',
    'src/core/vmu_durable.c','src/core/vmu_persist_session.c','src/core/vmu_fs_admission.c',
    'src/core/vmu_persist_profile.c','src/core/vmu_store.c','src/core/vmu_image.c',
    'src/core/hash.c','src/core/retail_image.c')
class SuccessiveAdmissionTest(unittest.TestCase):
    def test_live_semantics_and_current_snapshot(self):
        with tempfile.TemporaryDirectory(prefix='kui-smash-successive-admission-') as folder:
            binary=Path(folder)/'admission'
            command=[*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
                '-Wall','-Wextra','-Werror','-pedantic','-ffunction-sections','-fdata-sections',
                '-Wl,--gc-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                '-fno-pie','-no-pie','-Iinclude','-Isrc/loader',
                '-DKUI_VMU_READONLY_PROOF=4','-DKUI_VMU_EXPERIMENTAL=0',*SOURCES,'-o',str(binary)]
            built=subprocess.run(command,cwd=ROOT,text=True,capture_output=True,timeout=120)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],cwd=ROOT,text=True,capture_output=True,timeout=60,
                env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            print(run.stdout,end='')
if __name__=='__main__':unittest.main()
