#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the production FMV handoff predicates and output control under sanitizers."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    directory = ROOT / 'build/fmv-host'
    directory.mkdir(parents=True, exist_ok=True)
    tests = {
        'pcm_silence': [],
        'toy_idle': ['src/core/toy_pilot.c'],
        'toy_rearm': [],
        'toy_handoff': [],
    }
    for name, extra in tests.items():
        output = directory / name
        command = ['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-ffunction-sections', '-fdata-sections', '-fno-pie', '-no-pie',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                   '-Wl,--gc-sections', '-Iinclude', '-Isrc/loader',
                   'tests/test_fmv_' + name + '.c', *extra, '-o', str(output)]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(output)], cwd=ROOT, check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})


if __name__ == '__main__':
    main()
