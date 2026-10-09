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
    ('block-boundaries', ['tests/test_toy_pilot_block_boundaries.c',
                          'src/core/toy_pilot.c', 'src/core/hash.c'], []),
    ('blocks-worker', ['tests/test_toy_pilot_blocks_worker.c',
                       'src/core/toy_pilot.c', 'src/core/hash.c'], []),
    ('driver-load', ['tests/test_toy_pilot_driver_load.c'], []),
    ('admission', ['tests/test_toy_pilot_admission.c'], []),
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
        extra = ['-Wno-unused-function'] if name in ('block-boundaries', 'blocks-worker',
                                                    'shared-worker', 'async-audio-engine',
                                                    'async-audio-worker') else []
        subprocess.run(shlex.split(args.cc) + flags + extra + ['-D' + d for d in definitions] +
                       sources + ['-Wl,--gc-sections', '-o', str(target)], cwd=ROOT, check=True)
        subprocess.run([str(target.resolve())], cwd=ROOT, env=environment, check=True)
    for check in ('scratch', 'gd_chunk', 'report'):
        command = [sys.executable, str(ROOT / ('tests/test_toy_pilot_' + check + '.py')), '--cc', args.cc]
        if args.no_sanitizers:
            command.append('--no-sanitizers')
        subprocess.run(command, cwd=ROOT, env=environment, check=True)
    subprocess.run([sys.executable, str(ROOT / 'tests/test_toy_pilot_package.py')],
                   cwd=ROOT, env=environment, check=True)
    print(f'{len(SUITES)+4} Toy pilot regression suites passed')


if __name__ == '__main__':
    main()
