#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the actual synchronous return raw entry against storage boundaries."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='kui-smash-return-raw-') as directory:
    binary=Path(directory)/'raw'
    subprocess.run((*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
        '-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
        '-Iinclude','-Isrc/loader','tests/test_smash_menu_return_raw.c','-o',str(binary)),cwd=ROOT,check=True)
    subprocess.run((str(binary),),cwd=ROOT,check=True,
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
