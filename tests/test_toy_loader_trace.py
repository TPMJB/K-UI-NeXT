#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay actual GD/adapter with trace off/on, then verify telemetry corners.

Storage durations and PVR values are modeled inputs. No console throughput,
visible frame-rate, assembly SR restoration or IRQ fidelity is implied.
"""
import argparse
import os
from pathlib import Path
import re
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
             '-fno-pie', '-no-pie', '-ffunction-sections', '-fdata-sections',
             '-Iinclude', '-Isrc/loader', '-DKUI_TOY_PILOT_PRIVATE_P2=1',
             '-DKUI_TOY_PILOT_GD_FIXED_STEP=2', '-DKUI_TOY_LOADER_TRACE_HOST_TEST=1']
    if not args.no_sanitizers:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    signatures = []
    with tempfile.TemporaryDirectory(prefix='kui-toy-loader-trace-') as temporary:
        for enabled in (0, 1):
            binary = Path(temporary) / ('trace-' + str(enabled))
            subprocess.run(shlex.split(args.cc) + flags +
                           ['-DKUI_TOY_PILOT_LOADER_TRACE=' + str(enabled),
                            'tests/test_toy_loader_trace.c', 'src/core/retail_gd.c',
                            'src/loader/toy_pilot_gd.c', '-Wl,--gc-sections', '-o', str(binary)],
                           cwd=ROOT, check=True)
            result = subprocess.run([str(binary)], cwd=ROOT, env=env,
                                    check=False, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError('Production trace fixture failed:\n' + result.stdout + result.stderr)
            print(result.stdout, end='')
            signatures.append(re.findall(r'^PROTOCOL calls=\d+ digest=[0-9a-f]{16}$',
                                         result.stdout, re.MULTILINE))
        if len(signatures[0]) != 1 or signatures[0] != signatures[1]:
            raise RuntimeError('Trace off/on production protocol replay differs: ' + repr(signatures))
        for definitions, expected in (
            (['KUI_TOY_PILOT_LOADER_TRACE=2'], 'Toy loader trace must be 0 or 1'),
            (['KUI_TOY_PILOT_LOADER_TRACE=1', 'KUI_TOY_PILOT_PRIVATE_P2=0'],
             'Toy loader trace requires retained private P2 synchronous two-sector real audio'),
            (['KUI_TOY_PILOT_LOADER_TRACE=1', 'KUI_TOY_PILOT_SHARED_SCI=1'],
             'Toy loader trace requires retained private P2 synchronous two-sector real audio'),
            (['KUI_TOY_PILOT_LOADER_TRACE=1', 'KUI_TOY_PILOT_ASYNC_CDDA=1'],
             'Toy loader trace requires retained private P2 synchronous two-sector real audio'),
            (['KUI_TOY_PILOT_LOADER_TRACE=1', 'KUI_TOY_PILOT_SYNTHETIC_SOURCE=1'],
             'Toy loader trace requires retained private P2 synchronous two-sector real audio'),
            (['KUI_TOY_PILOT_LOADER_TRACE=1', 'KUI_TOY_PILOT_GD_FIXED_STEP=3'],
             'Toy loader trace requires retained private P2 synchronous two-sector real audio'),
            (['KUI_TOY_PILOT_LOADER_TRACE=1', 'KUI_TOY_PILOT_NATIVE_CACHE=1'],
             'Toy loader trace requires retained private P2 synchronous two-sector real audio'),
            (['KUI_TOY_PILOT_LOADER_TRACE=1', 'KUI_SCI_DMA_REUSE_TDRE=1'],
             'Toy loader trace requires retained private P2 synchronous two-sector real audio'),
        ):
            config_flags = [flag for flag in flags if not flag.startswith('-DKUI_TOY_PILOT_')]
            defaults = {'KUI_TOY_PILOT_PRIVATE_P2': '1', 'KUI_TOY_PILOT_GD_FIXED_STEP': '2'}
            defaults.update(dict(value.split('=', 1) for value in definitions))
            invalid = subprocess.run(shlex.split(args.cc) + config_flags +
                                     ['-D' + key + '=' + value for key, value in defaults.items()] +
                                     ['-x', 'c', '-c', '-o', os.devnull, '-'],
                                     input='#include "kui/toy_pilot.h"\n#include "kui/toy_loader_trace.h"\n',
                                     cwd=ROOT, capture_output=True, text=True)
            if invalid.returncode == 0 or expected not in invalid.stderr:
                raise RuntimeError('Invalid trace profile admitted or wrong diagnostic: ' + invalid.stderr)
        trace_source = (ROOT / 'src/loader/toy_loader_trace.c').read_text()
        report_source = (ROOT / 'src/loader/toy_loader_trace_report.c').read_text()
        fixture_source = (ROOT / 'tests/test_toy_loader_trace.c').read_text()
        mutations = (
            ('mailbox-refusal-boundary', trace_source,
             's->pending && s->command==r4 && s->token==(uint32_t)result && s->count && !s->error',
             's->command==r4', False, '!view()->active_phase && !view()->accepted_play_requests'),
            ('unconsumed-check-ack', trace_source, '!s->command && !s->pending &&',
             '!s->pending &&', False, 'complete_acknowledge.ticks_max==27u'),
            ('histogram-inclusive-boundary', trace_source, 'ticks>ceilings[bin]',
             'ticks>=ceilings[bin]', False, 'm->samples==14u && m->histogram[0]==1u'),
            ('reset-before-capture', report_source, 'stopped_display=*display;',
             '(void)kui_toy_pilot_request(KUI_TOY_PILOT_RESET,0u,0u,0u); stopped_display=*display;',
             True, 'destroy_on_reset==2u'),
        )
        for name, original, before, after, report_mutation, diagnostic in mutations:
            if original.count(before) != 1:
                raise RuntimeError('Production mutation target is missing/ambiguous: ' + name)
            replacement = Path(temporary) / (name + '.c')
            replacement.write_text(original.replace(before, after, 1))
            trace_path = replacement if not report_mutation else ROOT / 'src/loader/toy_loader_trace.c'
            report_path = replacement if report_mutation else ROOT / 'src/loader/toy_loader_trace_report.c'
            test_source = fixture_source.replace('../src/loader/toy_loader_trace.c', str(trace_path)).replace(
                '../src/loader/toy_loader_trace_report.c', str(report_path))
            test = Path(temporary) / (name + '-fixture.c')
            test.write_text(test_source)
            binary = Path(temporary) / (name + '-fixture')
            subprocess.run(shlex.split(args.cc) + flags + ['-DKUI_TOY_PILOT_LOADER_TRACE=1',
                           str(test), 'src/core/retail_gd.c', 'src/loader/toy_pilot_gd.c',
                           '-Wl,--gc-sections', '-o', str(binary)], cwd=ROOT, check=True)
            result = subprocess.run([str(binary)], cwd=ROOT, env=env,
                                    capture_output=True, text=True)
            if not result.returncode or diagnostic not in result.stderr:
                raise RuntimeError('Mutation did not fail its intended assertion: ' + name + '\n' +
                                   result.stdout + result.stderr)
        print('Toy loader trace negative mutations: refused mailbox, unconsumed CHECK, inclusive histogram and early RESET rejected')
    print('Toy loader trace production differential, telemetry and report suites passed')


if __name__ == '__main__':
    main()
