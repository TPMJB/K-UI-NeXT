#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the complete persistent fixture: legacy, exact 112 KiB and 72 KiB.

This calls its original main, including durable recovery, ownership mutations,
hook witnesses and no-false-ACK cases. General production builds explicitly
reject excluded diagnostic modes; the legacy run retains their full lifecycle.
Only the scoped ownership/MMIO boundaries supplied by the C fixture are models.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def make_variables():
    names = ('HOST_CC', 'PASSIVE_HOST_FLAGS', 'VMU_ONLY_HOST_FLAGS',
             'VMU_LITE_HOST_FLAGS', 'COMPAT_WORKER_HOST_SRC')
    recipe = ('compact-persistent-vars:\n\t' + ''.join(
        '$(info COMPACT_' + name + '=$(' + name + '))' for name in names) + '@:\n')
    result = subprocess.run(['make', '--no-print-directory', '-s',
        '-f', 'Makefile.vmu-native-vmu-lite', '--eval', recipe,
        'compact-persistent-vars'], cwd=ROOT, text=True, capture_output=True, check=True)
    values = {}
    for line in result.stdout.splitlines():
        if line.startswith('COMPACT_'):
            name, value = line.removeprefix('COMPACT_').split('=', 1)
            values[name] = shlex.split(value)
    if set(values) != set(names) or any(not values[name] for name in names):
        raise RuntimeError('Could not resolve production host build variables:\n' + result.stdout)
    return values


def run_variant(name, selectors, values, directory):
    output = directory / ('persistent-' + name)
    command = [*values['HOST_CC'], *values['PASSIVE_HOST_FLAGS'],
        '-fno-sanitize-recover=all', *selectors,
        'tests/test_vmu_native_persistent.c', *values['COMPAT_WORKER_HOST_SRC'],
        '-Wl,--gc-sections', '-o', str(output)]
    log = '[' + name + '] compile: ' + shlex.join(command) + '\n'
    compiled = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    log += compiled.stdout + compiled.stderr
    if compiled.returncode:
        return name, False, log
    executed = subprocess.run([str(output)], cwd=ROOT, text=True, capture_output=True,
        env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
    log += '[' + name + '] execute: ' + str(output) + '\n' + executed.stdout + executed.stderr
    passed = executed.returncode == 0 and 'Native persistent bridge:' in executed.stdout
    return name, passed, log


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, help='Keep the three host executables here')
    parser.add_argument('--log', type=Path, help='Write exact compiler commands and complete outputs')
    parser.add_argument('--jobs', type=int, choices=(1, 2, 3), default=3)
    args = parser.parse_args()
    values = make_variables()
    variants = (
        ('legacy-cached', ['-DKUI_VMU_EXPERIMENTAL=1', '-DKUI_VMU_NATIVE_CACHED_CODE=1']),
        ('vmu-only-112k', values['VMU_ONLY_HOST_FLAGS']),
        ('vmu-lite-72k', values['VMU_LITE_HOST_FLAGS']),
    )
    with tempfile.TemporaryDirectory(prefix='kui-compact-persistent-') as temporary:
        directory = args.build_dir.resolve() if args.build_dir else Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        with ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = [executor.submit(run_variant, name, flags, values, directory)
                       for name, flags in variants]
            results = [future.result() for future in futures]
    combined = ''.join(log for _, _, log in results)
    if args.log:
        args.log.parent.mkdir(parents=True, exist_ok=True)
        args.log.write_text(combined)
    for name, passed, log in results:
        print('[' + name + '] ' + ('PASS' if passed else 'FAIL'))
        print(log, end='')
    if not all(passed for _, passed, _ in results):
        return 1
    print('Complete persistent worker main passed in legacy, exact 112 KiB and 72 KiB builds under ASAN/UBSAN')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
