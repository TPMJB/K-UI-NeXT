#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare the real native readers under identical finite service schedules."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASELINE = '74958b387827'
REJECTED = '1920659698ae'
SOURCES = ('retail_async.c', 'retail_async.h', 'sci_stream.c', 'sci_stream.h')


def run(output, diagnostic=False):
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env['ASAN_OPTIONS'] = 'detect_leaks=0'
    rows = {}
    for label, revision in (('baseline', BASELINE), ('rejected', REJECTED), ('candidate', None)):
        directory = output / label
        directory.mkdir(exist_ok=True)
        for name in SOURCES:
            data = (subprocess.check_output(['git', 'show', revision + ':src/loader/' + name], cwd=ROOT)
                    if revision else (ROOT / 'src/loader' / name).read_bytes())
            (directory / name).write_bytes(data)
        flags = ['cc', '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                 '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                 '-DKUI_SCI_SD_NO_BLOCK=1', '-DKUI_GENERAL_SCI_RESUME_FIX=1',
                 '-DKUI_RETAIL_NATIVE_DISC_CHECK=1', '-DKUI_RETAIL_NATIVE_DRIVE_STATE=1',
                 '-DKUI_RETAIL_ASYNC_TEST=1', '-DKUI_SCI_STREAM_TEST=1',
                 '-DKUI_RETAIL_ASYNC=1', '-DKUI_RETAIL_GD_ASYNC=1', '-DKUI_RETAIL_TRANSPORT=1']
        if label != 'baseline':
            flags.append('-DKUI_RETAIL_NATIVE_HYBRID=1')
        if label=='candidate' and diagnostic:
            flags.append('-DKUI_NATIVE_READER_DIAGNOSTIC=1')
        flags += ['-I' + str(directory), '-Iinclude', '-Isrc/loader',
                  str(directory / 'retail_async.c'), str(directory / 'sci_stream.c'),
                  'src/core/retail_cursor.c', 'src/core/retail_image.c', 'src/core/retail_gd.c',
                  'tests/test_retail_async_service_cadence.c', '-o', str(directory / 'cadence')]
        subprocess.run(flags, cwd=ROOT, check=True)
        result = subprocess.run([str(directory / 'cadence')], cwd=ROOT, env=env,
                                text=True, capture_output=True, check=True)
        (output / (label + '.jsonl')).write_text(result.stdout)
        rows[label] = [json.loads(line) for line in result.stdout.splitlines()]
    a, b, c = (rows[name] for name in ('baseline', 'rejected', 'candidate'))
    assert len(a) == len(b) == len(c) == 50
    delayed = []
    examples = []
    for old, bad, new in zip(a, b, c):
        assert old['exact_output'] and bad['exact_output'] and new['exact_output']
        target = 25 if new['how'] else 20
        if new.get('hot_irq'):
            assert old['blocks_in_one_irq'] == 256 and bad['blocks_in_one_irq'] == 1
            assert new['blocks_in_one_irq'] == 8
            continue
        assert all(old[k] == bad[k] == new[k] for k in ('how', 'cooked', 'delay', 'schedule'))
        assert all(new[k] <= target for k in ('request_max', 'check_max', 'exec_max', 'irq_max'))
        if old['delay']:
            # The conservative candidate must retain the exact original
            # service capacity in these delayed-DMA schedules, not merely
            # finish eventually under an unlimited IRQ helper.
            assert new['rounds'] == old['rounds'], (old, new)
            delayed.append(new)
        if old['how'] == 0 and old['cooked'] == 1 and old['delay'] == 1000:
            examples.append(dict(schedule=old['schedule'], baseline_rounds=old['rounds'],
                                 rejected_rounds=bad['rounds'], candidate_rounds=new['rounds']))
    assert len(delayed) == 32 and any(e['rejected_rounds'] > e['baseline_rounds'] for e in examples)
    report = dict(schema='kui.native.service-cadence.comparison.v1',
                  baseline_commit=BASELINE, rejected_commit=REJECTED,
                  schedules_per_reader=50, delayed_schedules_matching_baseline=len(delayed),
                  all_outputs_exact=True, original_catchup_preserved=True,
                  hot_irq_blocks=dict(baseline=256, rejected=1, candidate_normal=8, candidate_eager=8),
                  examples=examples, console_timing_measured=False,
                  reader_diagnostic_enabled=diagnostic,
                  limitation='Rounds are synthetic caller opportunities, not video frames or milliseconds.')
    (output / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/native-reader-cadence')
    parser.add_argument('--diagnostic',action='store_true')
    args=parser.parse_args()
    run(args.output.resolve(),args.diagnostic)
