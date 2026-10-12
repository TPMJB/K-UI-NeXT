#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise production build/header refusal of unsafe startup budget settings."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SAFE = {
    'KUI_TOY_PILOT_DATA_SCHEDULE_MODE': 2,
    'KUI_TOY_PILOT_PRIVATE_P2': 1,
    'KUI_TOY_PILOT_NATIVE_CACHE': 0,
    'KUI_TOY_PILOT_SHARED_SCI': 0,
    'KUI_TOY_PILOT_ASYNC_CDDA': 0,
    'KUI_TOY_PILOT_SYNTHETIC_SOURCE': 0,
    'KUI_TOY_PILOT_GD_FIXED_STEP': 2,
    'KUI_SCI_DMA_REUSE_TDRE': 0,
    'KUI_TOY_PILOT_DATA_PAYLOAD_MODE': 0,
    'KUI_TOY_PILOT_LOADER_TRACE': 1,
    'KUI_TOY_PILOT_DATA_PROBE': 1,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    source = '#include "kui/toy_loader_schedule.h"\nint main(void){return 0;}\n'
    checked = 0

    def compile_profile(profile, changes, expected):
        nonlocal checked
        definitions = {**SAFE, 'KUI_TOY_PILOT_DATA_BUDGET_PROFILE': profile, **changes}
        command = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsyntax-only',
                   '-I' + str(ROOT / 'include'), '-x', 'c', '-']
        command += ['-D' + name + '=' + str(value) for name, value in definitions.items()]
        result = subprocess.run(command, input=source, text=True, capture_output=True)
        if (result.returncode == 0) != expected:
            raise RuntimeError('Header admission result differs: ' + str(definitions) + '\n' + result.stderr)
        checked += 1

    for profile in range(5):
        compile_profile(profile, {}, True)
    for profile in range(1, 5):
        for name, value in (('KUI_TOY_PILOT_DATA_SCHEDULE_MODE', 0),
                            ('KUI_TOY_PILOT_DATA_SCHEDULE_MODE', 1),
                            ('KUI_TOY_PILOT_DATA_SCHEDULE_MODE', 3),
                            ('KUI_TOY_PILOT_PRIVATE_P2', 0),
                            ('KUI_TOY_PILOT_NATIVE_CACHE', 1),
                            ('KUI_TOY_PILOT_SHARED_SCI', 1),
                            ('KUI_TOY_PILOT_ASYNC_CDDA', 1),
                            ('KUI_TOY_PILOT_SYNTHETIC_SOURCE', 1),
                            ('KUI_TOY_PILOT_GD_FIXED_STEP', 3),
                            ('KUI_SCI_DMA_REUSE_TDRE', 1),
                            ('KUI_TOY_PILOT_DATA_PAYLOAD_MODE', 1),
                            ('KUI_TOY_PILOT_LOADER_TRACE', 0),
                            ('KUI_TOY_PILOT_DATA_PROBE', 0)):
            compile_profile(profile, {name: value}, False)
    for invalid in (-1, 5, 255):
        compile_profile(invalid, {}, False)
    # Use the real Makefile's configuration target: no cross compiler or
    # binary production is involved in these admission/refusal checks.
    with tempfile.TemporaryDirectory(prefix='toy-budget-config-') as directory:
        for profile in range(1, 5):
            common = ['make', '-s', '-f', 'Makefile.toy_pilot', 'BUILD=' + directory,
                      'BUILD_ID=123456789abc', 'PRIVATE_P2=1', 'LOADER_TRACE=1',
                      'DATA_PROBE=1', 'DATA_SCHEDULE_MODE=2', 'DATA_BUDGET_PROFILE=' + str(profile)]
            result = subprocess.run(common + [directory + '/build-config'], cwd=ROOT,
                                    text=True, capture_output=True)
            if result.returncode:
                raise RuntimeError(result.stderr)
            config = (Path(directory) / 'build-config').read_text().splitlines()
            if config.count('DATA_BUDGET_PROFILE=' + str(profile)) != 1:
                raise RuntimeError('Budget profile was not bound exactly in build-config')
            checked += 1
            result = subprocess.run(common + ['DATA_SCHEDULE_MODE=1', directory + '/build-config'],
                                    cwd=ROOT, text=True, capture_output=True)
            if result.returncode == 0 or 'DATA budget profiles require' not in result.stderr:
                raise RuntimeError('Makefile admitted a budget on the wrong engine')
            checked += 1
    print(f'DATA budget configuration suites passed ({checked} production admission/refusal checks)')


if __name__ == '__main__':
    main()
