#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile and execute the exact bounded Evolution saved-frame collector."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class NativeWaitCapture(unittest.TestCase):
    def test_exact_collector(self):
        with tempfile.TemporaryDirectory(prefix='kui-e1-wait-') as directory:
            output=Path(directory)/'capture'
            command=[*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1',
                '-Wall','-Wextra','-Werror','-Wpedantic','-Wno-unused-function',
                '-fsanitize=undefined','-I',str(ROOT/'include'),
                str(ROOT/'tests/test_vmu_native_wait_capture.c'),
                str(ROOT/'src/core/vmu_native_profile.c'),'-o',str(output)]
            subprocess.run(command,cwd=ROOT,check=True)
            result=subprocess.run([str(output)],cwd=ROOT,check=True,text=True,capture_output=True)
            self.assertIn('checks PASS',result.stdout)
            print(result.stdout.strip())

if __name__=='__main__': unittest.main()
