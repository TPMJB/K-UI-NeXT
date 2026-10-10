#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate production DATA controls with exact ownership/cache mocks.

The model verifies alias lifetime, bounded publication, original callback
arguments, failure handling, and conditional PIO copy. It does not establish
real cache/MMIO timing, transfer speed, or Dreamcast game compatibility.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--no-sanitizers', action='store_true')
    args = parser.parse_args()
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
             '-fno-pie', '-no-pie', '-ffreestanding', '-fno-builtin', '-Iinclude',
             '-DKUI_TOY_LOADER_PAYLOAD_CONTROL_HOST_TEST=1',
             '-DKUI_TOY_PILOT_PRIVATE_P2=1', '-DKUI_TOY_PILOT_LOADER_TRACE=1',
             '-DKUI_TOY_PILOT_DATA_PROBE=1']
    if not args.no_sanitizers:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    compiler = shlex.split(args.cc)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    with tempfile.TemporaryDirectory(prefix='kui-toy-payload-control-') as temporary:
        for mode in (0, 1, 2):
            binary = Path(temporary) / ('payload-' + str(mode))
            subprocess.run(compiler + flags + ['-DKUI_TOY_PILOT_DATA_PAYLOAD_MODE=' + str(mode),
                           'tests/test_toy_loader_payload_control.c',
                           'src/loader/toy_loader_payload_control.c', '-o', str(binary)],
                           cwd=ROOT, check=True)
            subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
            image_binary = Path(temporary) / ('payload-image-' + str(mode))
            subprocess.run(compiler + flags + ['-Isrc/loader',
                           '-DKUI_RETAIL_FAST_IO=1',
                           '-DKUI_TOY_PILOT_DATA_PAYLOAD_MODE=' + str(mode),
                           'tests/test_toy_loader_payload_control_image.c',
                           'src/loader/toy_loader_payload_control.c',
                           'src/core/retail_image.c', 'src/loader/sd_reader.c',
                           '-o', str(image_binary)], cwd=ROOT, check=True)
            subprocess.run([str(image_binary)], cwd=ROOT, env=env, check=True)
        for mode, probe, trace, private, expected in (
                (-1, 1, 1, 1, 'DATA payload mode must be zero'),
                (3, 1, 1, 1, 'DATA payload mode must be zero'),
                (1, 0, 1, 1, 'DATA payload controls require'),
                (2, 1, 0, 1, 'DATA payload controls require'),
                (1, 1, 1, 0, 'DATA payload controls require')):
            result = subprocess.run(compiler + ['-std=c11', '-Iinclude',
                '-DKUI_TOY_PILOT_DATA_PAYLOAD_MODE=' + str(mode),
                '-DKUI_TOY_PILOT_DATA_PROBE=' + str(probe),
                '-DKUI_TOY_PILOT_LOADER_TRACE=' + str(trace),
                '-DKUI_TOY_PILOT_PRIVATE_P2=' + str(private),
                '-x', 'c', '-c', '-o', os.devnull, '-'],
                input='#include "kui/toy_loader_payload_control.h"\n',
                cwd=ROOT, capture_output=True, text=True)
            if not result.returncode or expected not in result.stderr:
                raise RuntimeError('Invalid payload control profile admitted: ' + result.stderr)
    print('Toy loader payload control suites passed (modes 0/1/2)')


if __name__ == '__main__':
    main()
