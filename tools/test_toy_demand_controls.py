#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the real Toy demand service, fingerprint and terminal-report fixtures."""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/toy-demand-host'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    results = []
    for fixture, profiles in [('service', (1, 2, 3)), ('fingerprint', (1, 3)),
                              ('report', (1, 2, 3))]:
        for profile in profiles:
            destination = OUT / (fixture + '-' + str(profile))
            command = ['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                       '-fno-pie', '-no-pie',
                       '-ffunction-sections', '-fdata-sections', '-Iinclude',
                       '-Isrc/loader', '-Itests',
                       '-DKUI_TOY_DEMAND_TEST_PROFILE=' + str(profile),
                       'tests/test_toy_demand_' + fixture + '.c',
                       '-Wl,--gc-sections', '-o', str(destination)]
            subprocess.run(command, cwd=ROOT, check=True)
            # These fixtures allocate no heap. The managed executor blocks
            # LeakSanitizer's /proc thread walk; address/UB checks remain on.
            environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
            output = subprocess.check_output([str(destination)], cwd=ROOT, text=True,
                                             env=environment)
            print(output, end='')
            results.append({'fixture': fixture, 'profile': profile,
                            'sanitizers': ['address', 'undefined'], 'result': 'PASS',
                            'command': command, 'output': output.strip()})
    (OUT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
