#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the production embedded-helper finder and padded owner checks."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def audit(modules=()):
    if len(modules) not in (0, 2):
        raise ValueError('Supply both actual VC2 and Swirl modules')
    with tempfile.TemporaryDirectory(prefix='smash-ce-embedded-') as folder:
        executable = Path(folder) / 'test-helper'
        command = [os.environ.get('CC', 'cc'), '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wpedantic', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie',
                   '-no-pie', '-ffunction-sections', '-fdata-sections', '-Iinclude', '-Ifatfs/source',
                   'tests/test_smash_ce_helper.c', 'src/core/smash_ce_helper.c', 'src/core/smash_ce_profile.c',
                   'src/core/runtime_image.c', 'src/core/retail_image.c', 'src/core/data.c',
                   '-Wl,--gc-sections', '-o', str(executable)]
        subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        result = subprocess.run([str(executable), *map(str, modules)], cwd=ROOT, check=True,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})
    return dict(status='PASS', actual_modules=len(modules), production_core=True,
                no_external_helper_lookup=True, output=result.stdout.strip())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('modules', nargs='*', type=Path)
    print(audit(parser.parse_args().modules)['output'])
