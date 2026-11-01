#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the production BREAD adapter with KOS-compatible retry ownership."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='vmu-read-bounded-') as temporary:
    output=Path(temporary)/'reader'
    subprocess.run([*shlex.split(os.environ.get('CC','cc')),
        '-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',
        '-Itests/vmu_read_bounded_stubs','-Iinclude','tests/test_vmu_read_bounded.c',
        'src/dreamcast/vmu_read_bounded.c','src/core/hash.c','src/core/data.c',
        '-Wl,--wrap=kui_crc32','-o',str(output)],cwd=ROOT,check=True)
    subprocess.run([str(output)],cwd=ROOT,check=True,
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:detect_stack_use_after_return=1'})
