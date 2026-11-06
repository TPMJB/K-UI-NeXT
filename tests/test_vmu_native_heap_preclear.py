#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sanitized cold preheap-clear proof against supplied original startups."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
if len(sys.argv)!=3:
    raise SystemExit('Supply original Time Stalkers and EGG startup files')
with tempfile.TemporaryDirectory(prefix='kui-preheap-clear-') as work:
    output=Path(work)/'preclear'
    subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
        '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
        '-fno-omit-frame-pointer','-fno-pie','-no-pie','-Iinclude',
        'tests/test_vmu_native_heap_preclear.c','src/core/vmu_native_heap.c','src/core/hash.c','src/core/data.c',
        '-o',str(output)],cwd=ROOT,check=True)
    subprocess.run([str(output),*[str(Path(path).resolve()) for path in sys.argv[1:]]],
        cwd=ROOT,check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
