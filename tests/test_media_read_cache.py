#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Scope and media-failure checks for the real production disk adapter."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix='kui-media-read-cache-') as directory:
        binary = Path(directory) / 'read-cache'
        subprocess.run([
            *shlex.split(os.environ.get('HOST_CC', 'cc')), '-std=c11', '-O1', '-g',
            '-Wall', '-Wextra', '-Werror', '-Wpedantic',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
            '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
            '-Iinclude', '-I.deps/fatfs/source',
            'tests/test_media_read_cache.c', 'src/core/diskio.c', 'src/core/data.c',
            '-o', str(binary),
        ], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True,
                       env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0',
                                UBSAN_OPTIONS='halt_on_error=1'), timeout=30)


if __name__ == '__main__':
    main()
