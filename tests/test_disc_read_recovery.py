#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Adversarial caller-publication checks for the actual private boot reader."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='kui-disc-read-recovery-') as folder:
    binary=Path(folder)/'disc-read-recovery'
    subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-O1','-g',
        '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
        '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie',
        '-Iinclude','-I.deps/fatfs/source','-Isrc/dreamcast',
        'tests/test_disc_read_recovery.c','src/core/disc_boot.c','src/core/game_metadata.c',
        'src/core/disc_launch.c','src/core/hash.c','src/core/data.c',
        '-Wl,--wrap=realloc','-o',str(binary)],cwd=ROOT,check=True)
    subprocess.run([str(binary)],cwd=ROOT,check=True,
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
