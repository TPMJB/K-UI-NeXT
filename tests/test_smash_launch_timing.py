#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fresh production UI accounting/sidecar check; import audit() for packaging."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
INPUTS = (
    'include/kui/smash_launch_timing.h', 'src/apps/smash_launch_timing.c',
    'include/kui/smash_phase_clock.h', 'src/core/smash_phase_clock.c',
    'src/apps/games_retail.c', 'src/apps/games_native_cache.inc',
    'src/apps/smash_menu_capsule_prepare.inc', 'src/apps/smash_menu_capsule_pair_prepare.inc',
    'src/apps/smash_menu_capsule_file_prepare.inc', 'src/dreamcast/main.c',
    'src/core/vmu_game_card_service.c', 'src/core/vmu_history_session.c',
    'Makefile.dc', 'tests/test_smash_launch_timing.c', 'tests/test_smash_launch_timing.py',
)


def audit():
    compiler = os.environ.get('HOST_CC', 'cc')
    if not shutil.which(compiler):
        raise RuntimeError(f'host compiler unavailable: {compiler}')
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
             '-Iinclude', '-I.deps/fatfs/source', '-Isrc/dreamcast']
    environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    with tempfile.TemporaryDirectory(prefix='kui-ui-timing-') as temporary:
        folder = Path(temporary)
        exe, disabled = folder / 'timing-test', folder / 'disabled.o'
        subprocess.run([compiler, *flags, '-DKUI_SMASH_LAUNCH_TIMING=0', '-c',
                        'src/apps/smash_launch_timing.c', '-o', str(disabled)], cwd=ROOT, check=True,
                       capture_output=True, text=True)
        symbols = subprocess.run(['nm', str(disabled)], cwd=ROOT, check=True,
                                 capture_output=True, text=True).stdout
        if 'kui_smash_launch_timing' in symbols:
            raise AssertionError('disabled object retained timing code or its marker')
        subprocess.run([compiler, *flags, '-DKUI_SMASH_LAUNCH_TIMING=1',
                        '-DKUI_SMASH_PHASE_CLOCK_HOST_TEST=1',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie',
                        'tests/test_smash_launch_timing.c', 'src/apps/smash_launch_timing.c',
                        'src/core/smash_phase_clock.c', 'src/core/data.c', '-o', str(exe)],
                       cwd=ROOT, check=True, capture_output=True, text=True)
        output = subprocess.run([str(exe)], cwd=ROOT, env=environment, check=True,
                                capture_output=True, text=True).stdout.strip()
        symbols = subprocess.run(['nm', str(exe)], cwd=ROOT, check=True,
                                 capture_output=True, text=True).stdout
        if 'kui_smash_launch_timing_profile' not in symbols:
            raise AssertionError('enabled profile marker missing')
    return {
        'ok': True, 'scope': 'production UI timing accounting and mounted sidecar, synthetic clock/FatFs',
        'result': output, 'sanitizers': ['address', 'undefined'],
        'disabled_marker_absent': True, 'enabled_marker_present': True,
        'checks': ['native-route gating', 'controller timestamp', 'non-overlapping major sum',
                   'nested spans excluded from major sum', 'unavailable hardware clock labels',
                   'report I/O excluded', 'mounted writer owns no mount/connect', 'CRC/readback',
                   'failure/cancellation/unsafe-destination suppression', 'short I/O and sync failures'],
        'source_sha256': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in INPUTS},
        'measured_console_time': False,
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json', action='store_true')
    arguments = parser.parse_args()
    result = audit()
    print(json.dumps(result, indent=2) if arguments.json else result['result'])
