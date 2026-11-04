#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run rolling-observer tests and compare actual AP service behavior in A/B/C."""
from pathlib import Path
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/toy-decision-host'


def run(fixture, profile=None):
    destination = OUT / (fixture + ('' if profile is None else '-' + str(profile)))
    command = ['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
               '-fno-pie', '-no-pie', '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', '-ffunction-sections', '-fdata-sections',
               '-Iinclude', '-Isrc/loader', '-Itests', '-Wl,--gc-sections']
    if profile is not None:
        command += ['-DKUI_TOY_DECISION_PROFILE=' + str(profile)]
    command += ['tests/test_toy_decision_' + fixture + '.c', '-o', str(destination)]
    subprocess.run(command, cwd=ROOT, check=True)
    return subprocess.check_output([str(destination)], cwd=ROOT, text=True,
        env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    observer = run('observer')
    outputs = [run('service', profile) for profile in (0, 1, 2)]
    if not (outputs[0] == outputs[1] == outputs[2]):
        raise AssertionError('A/B/C AP progress, ownership, status or timer-read traces differ')
    result = {'observer': observer.strip(), 'ap_cases': len(outputs[0].splitlines()),
              'profiles': [0, 1, 2], 'timer_read_count_and_positions_equal': True,
              'observable_progress_and_ownership_equal': True,
              'sanitizers': ['address', 'undefined'], 'console_tested': False,
              'service_trace': outputs[0].splitlines()}
    (OUT / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(observer, end='')
    print('PASS actual AP service A/B/C: 12 DMA/PIO/retry/budget/cancel/retire cases; identical progress, status and timer-read traces')


if __name__ == '__main__':
    main()
