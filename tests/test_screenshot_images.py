#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise screenshot publication on real FatFs FAT32/exFAT RAM images.

The private host configuration enables FatFs's formatter. No installed mkfs
program, production configuration change, or filesystem image is required.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def main():
    config=(ROOT/'config/ffconf.h').read_text()
    old='#define FF_USE_MKFS 0'
    assert config.count(old)==1
    with tempfile.TemporaryDirectory(prefix='kui-screenshot-images-') as folder:
        work=Path(folder)
        header=work/'test-ffconf.h';header.write_text(config.replace(old,'#define FF_USE_MKFS 1'))
        binary=work/'screenshots-image'
        subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
            '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie',
            '-include',str(header),'-Iinclude','-I.deps/fatfs/source',
            'tests/screenshots_image.c','src/core/screenshot.c','src/apps/screenshot_file.c',
            '.deps/fatfs/source/ff.c','.deps/fatfs/source/ffunicode.c','-o',str(binary)],
            cwd=ROOT,check=True)
        subprocess.run([str(binary)],cwd=ROOT,check=True,timeout=60,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))


if __name__=='__main__':main()
