#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the pilot's host regressions without touching ordinary build output."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SUITES = (
    ('bus', ['tests/test_toy_pilot_bus.c', 'src/loader/toy_pilot_bus.c'], ['KUI_TOY_PILOT_BUS_TEST']),
    ('lease', ['tests/test_toy_pilot_lease.c', 'src/loader/toy_pilot_lease.c'], ['KUI_TOY_PILOT_LEASE_TEST']),
    ('gd', ['tests/test_toy_pilot_gd.c', 'src/core/retail_gd.c'], []),
    ('gd-core', ['tests/test_retail_gd.c', 'src/core/retail_gd.c'], []),
    ('model', ['tests/test_toy_pilot.c', 'src/core/toy_pilot.c'], []),
    ('ring-evidence', ['tests/test_toy_pilot_ring.c'], []),
    ('ring-worker', ['tests/test_toy_pilot_ring_worker.c', 'src/core/toy_pilot.c',
                     'src/core/hash.c'], []),
    ('synthetic-source', ['tests/test_toy_pilot_synthetic_source.c',
                          'src/core/toy_pilot.c', 'src/core/hash.c'],
                         ['KUI_TOY_PILOT_SYNTHETIC_SOURCE=1']),
    ('block-boundaries', ['tests/test_toy_pilot_block_boundaries.c',
                          'src/core/toy_pilot.c', 'src/core/hash.c'], []),
    ('blocks-worker', ['tests/test_toy_pilot_blocks_worker.c',
                       'src/core/toy_pilot.c', 'src/core/hash.c'], []),
    ('driver-load', ['tests/test_toy_pilot_driver_load.c'], []),
    ('admission', ['tests/test_toy_pilot_admission.c'], []),
    ('cache-baseline', ['tests/test_toy_pilot_cache.c'],
                       ['KUI_TOY_TEST_EXPECT_PRIVATE=0', 'KUI_TOY_TEST_EXPECT_NATIVE=0']),
    ('cache-private', ['tests/test_toy_pilot_cache.c'],
                      ['KUI_TOY_PILOT_PRIVATE_P2=1', 'KUI_TOY_PILOT_NATIVE_CACHE=0',
                       'KUI_TOY_TEST_EXPECT_PRIVATE=1', 'KUI_TOY_TEST_EXPECT_NATIVE=0']),
    ('cache-native', ['tests/test_toy_pilot_cache.c'],
                     ['KUI_TOY_PILOT_PRIVATE_P2=1', 'KUI_TOY_PILOT_NATIVE_CACHE=1',
                      'KUI_TOY_TEST_EXPECT_PRIVATE=1', 'KUI_TOY_TEST_EXPECT_NATIVE=1']),
    ('native-cache-publication', ['tests/test_toy_pilot_native_cache.c',
                                  'src/loader/toy_pilot_bus.c',
                                  'src/loader/toy_pilot_lease.c'],
                                 ['KUI_TOY_PILOT_BUS_TEST', 'KUI_TOY_PILOT_LEASE_TEST',
                                  'KUI_TOY_PILOT_PRIVATE_P2=1',
                                  'KUI_TOY_PILOT_CACHE_TEST=1']),
    ('worker', ['tests/test_toy_pilot_worker.c', 'src/loader/toy_pilot_worker.c',
                'src/core/toy_pilot.c', 'src/core/hash.c', 'src/core/data.c'], ['KUI_TOY_PILOT_WORKER_TEST']),
    ('gd-status', ['tests/test_toy_pilot_gd_status.c', 'src/core/retail_gd.c'], []),
    ('gd-dispatch', ['tests/test_toy_pilot_gd_dispatch.c', 'src/loader/toy_pilot_gd.c',
                    'src/core/retail_gd.c'], []),
    ('pause', ['tests/test_toy_pilot_pause.c'], []),
    ('startup', ['tests/test_toy_pilot_startup.c', 'src/loader/toy_pilot_gd.c',
                 'src/core/retail_gd.c'], []),
    ('native-lifecycle', ['tests/test_toy_pilot_native_lifecycle.c', 'src/loader/toy_pilot_gd.c',
                          'src/core/retail_gd.c'], []),
    ('flow', ['tests/test_toy_pilot_flow.c', 'src/loader/toy_pilot_gd.c',
              'src/loader/toy_pilot_pause.c', 'src/core/retail_gd.c',
              'src/core/toy_pilot.c', 'src/core/hash.c'], ['KUI_TOY_PILOT_PAUSE_TEST']),
    ('shared-sci', ['tests/test_toy_pilot_sci.c', 'src/loader/toy_pilot_sci.c',
                    'src/core/retail_cursor.c', 'src/core/retail_image.c',
                    'src/core/retail_gd.c'], ['KUI_TOY_PILOT_SCI_TEST=1',
                     'KUI_TOY_PILOT_SHARED_SCI=1', 'KUI_RETAIL_GD_ASYNC=1']),
    ('shared-sci-stream', ['tests/test_sci_stream.c', 'src/loader/sci_stream.c'],
                         ['KUI_SCI_STREAM_TEST=1', 'KUI_TOY_PILOT_SHARED_SCI=1']),
    ('shared-gd', ['tests/test_toy_pilot_gd_async.c', 'src/loader/toy_pilot_gd.c',
                   'src/core/retail_gd.c'], ['KUI_TOY_PILOT_SHARED_SCI=1',
                   'KUI_RETAIL_GD_ASYNC=1', 'KUI_RETAIL_GD_REJECTION_DETAILS=1']),
    ('shared-worker', ['tests/test_toy_pilot_shared_worker.c', 'src/core/toy_pilot.c',
                       'src/core/hash.c'], ['KUI_TOY_PILOT_SHARED_SCI=1']),
    ('async-audio-gd', ['tests/test_toy_pilot_gd_async.c', 'src/loader/toy_pilot_gd.c',
                         'src/core/retail_gd.c'], ['KUI_TOY_PILOT_SHARED_SCI=1',
                         'KUI_TOY_PILOT_ASYNC_CDDA=1', 'KUI_RETAIL_GD_ASYNC=1',
                         'KUI_RETAIL_GD_REJECTION_DETAILS=1']),
    ('async-audio-engine', ['tests/test_toy_pilot_sci_async_audio.c',
                             'src/loader/toy_pilot_sci.c', 'src/core/retail_cursor.c',
                             'src/core/retail_image.c', 'src/core/retail_gd.c'],
                            ['KUI_TOY_PILOT_SCI_TEST=1', 'KUI_TOY_PILOT_SHARED_SCI=1',
                             'KUI_TOY_PILOT_ASYNC_CDDA=1', 'KUI_RETAIL_GD_ASYNC=1']),
    ('async-audio-worker', ['tests/test_toy_pilot_async_audio_worker.c',
                             'src/core/toy_pilot.c', 'src/core/hash.c'],
                            ['KUI_TOY_PILOT_SHARED_SCI=1', 'KUI_TOY_PILOT_ASYNC_CDDA=1']),
    ('async-audio-stream', ['tests/test_toy_pilot_sci_async_integration.c',
                             'src/loader/toy_pilot_sci.c', 'src/core/retail_cursor.c',
                             'src/core/retail_image.c', 'src/core/retail_gd.c'],
                            ['KUI_SCI_STREAM_TEST=1', 'KUI_TOY_PILOT_SCI_TEST=1',
                             'KUI_TOY_PILOT_SHARED_SCI=1', 'KUI_TOY_PILOT_ASYNC_CDDA=1',
                             'KUI_RETAIL_GD_ASYNC=1']),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/toy-pilot/host-tests')
    parser.add_argument('--no-sanitizers', action='store_true')
    args = parser.parse_args()
    args.build_dir.mkdir(parents=True, exist_ok=True)
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
             '-fno-pie', '-no-pie', '-ffunction-sections', '-fdata-sections',
             '-Iinclude', '-Isrc/loader']
    if not args.no_sanitizers:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    for name, sources, definitions in SUITES:
        target = args.build_dir / ('test-' + name)
        # The independent boundary test shares a larger physical-consumer
        # fixture; uncalled fixture helpers are intentionally retained.
        extra = ['-Wno-unused-function'] if name in ('synthetic-source', 'block-boundaries', 'blocks-worker',
                                                    'shared-worker', 'async-audio-engine',
                                                    'async-audio-worker') else []
        subprocess.run(shlex.split(args.cc) + flags + extra + ['-D' + d for d in definitions] +
                       sources + ['-Wl,--gc-sections', '-o', str(target)], cwd=ROOT, check=True)
        subprocess.run([str(target.resolve())], cwd=ROOT, env=environment, check=True)
    # Compile the same admitted test source with only the profile changed.
    # Require its explicit configuration diagnostic, not an unrelated error.
    invalid = subprocess.run(shlex.split(args.cc) + flags +
                             ['-DKUI_TOY_PILOT_PRIVATE_P2=0',
                              '-DKUI_TOY_PILOT_NATIVE_CACHE=1',
                              '-DKUI_TOY_TEST_EXPECT_PRIVATE=0',
                              '-DKUI_TOY_TEST_EXPECT_NATIVE=1',
                              'tests/test_toy_pilot_cache.c', '-Wl,--gc-sections',
                              '-o', str(args.build_dir / 'test-cache-invalid')],
                             cwd=ROOT, capture_output=True, text=True)
    if invalid.returncode == 0 or ('Native Toy copy-back requires the isolated '
                                   'private-state profile') not in invalid.stderr:
        raise RuntimeError('native cache without private P2 did not fail with the '
                           'required profile diagnostic:\n' + invalid.stderr)
    print('Toy invalid cache profile: native copy-back without private P2 rejected')
    for definitions, diagnostic in (
        (['KUI_TOY_PILOT_SYNTHETIC_SOURCE=2'], 'Toy synthetic source must be 0 or 1'),
        (['KUI_TOY_PILOT_SYNTHETIC_SOURCE=1', 'KUI_TOY_PILOT_SHARED_SCI=1'],
         'Toy synthetic source requires the retained synchronous SCI profile'),
        (['KUI_TOY_PILOT_SYNTHETIC_SOURCE=1', 'KUI_TOY_PILOT_ASYNC_CDDA=1'],
         'Toy synthetic source requires the retained synchronous SCI profile'),
    ):
        invalid = subprocess.run(shlex.split(args.cc) + flags +
                                 ['-D' + value for value in definitions] +
                                 ['-x', 'c', '-c', '-o', os.devnull, '-'],
                                 input='#include "kui/toy_pilot.h"\n', cwd=ROOT,
                                 capture_output=True, text=True)
        if invalid.returncode == 0 or diagnostic not in invalid.stderr:
            raise RuntimeError('Synthetic source configuration admitted or failed '
                               'without required diagnostic:\n' + invalid.stderr)
    print('Toy synthetic source profile: invalid/shared/async combinations rejected')
    for check in ('scratch', 'gd_chunk', 'report'):
        command = [sys.executable, str(ROOT / ('tests/test_toy_pilot_' + check + '.py')), '--cc', args.cc]
        if args.no_sanitizers:
            command.append('--no-sanitizers')
        subprocess.run(command, cwd=ROOT, env=environment, check=True)
    subprocess.run([sys.executable, str(ROOT / 'tests/test_toy_pilot_package.py')],
                   cwd=ROOT, env=environment, check=True)
    subprocess.run([sys.executable, str(ROOT / 'tests/test_toy_pilot_cache_audit.py')],
                   cwd=ROOT, env=environment, check=True)
    subprocess.run([sys.executable, str(ROOT / 'tests/test_toy_pilot_p2_layout.py')],
                   cwd=ROOT, env=environment, check=True)
    print(f'{len(SUITES)+7} Toy pilot regression suites passed')


if __name__ == '__main__':
    main()
