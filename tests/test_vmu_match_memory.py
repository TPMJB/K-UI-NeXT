#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Confirmed VMU save matches survive remounts and incomplete publications."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_games_library_images import private_formatter

ROOT = Path(__file__).resolve().parents[1]
CASES = ('keys', 'roundtrip', 'corrections', 'stale-part',
         'staging-directory', 'published-directory', 'first-write-fail',
         'write-fail', 'write-short', 'sync-fail', 'readback-fail',
         'readback-corrupt', 'rename-fail', 'unlink-fail', 'final-read-fail',
         'corrupt-newer', 'corrupt-older', 'truncated',
         'same-generation-conflict', 'wrong-key', 'reserved')


def run(*args):
    result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True,
                            env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0',
                                 'UBSAN_OPTIONS': 'halt_on_error=1'})
    if result.returncode:
        print(result.stdout, end='')
        print(result.stderr, end='')
        raise SystemExit(f'VMU match memory failed: {args} ({result.returncode})')
    return result.stdout


with tempfile.TemporaryDirectory(prefix='kui-vmu-match-memory-') as temporary:
    folder = Path(temporary)
    formatter = private_formatter(folder)
    run('make', '-s', 'build/test-vmu-match-memory')
    for kind in ('fat32', 'exfat'):
        seed = folder / f'{kind}-seed.img'
        run(str(formatter), kind, str(seed))
        for case in CASES:
            disk = folder / f'{kind}-case.img'
            shutil.copyfile(seed, disk)
            assert f'PASS VMU match memory: {case}' in run(
                str(ROOT / 'build/test-vmu-match-memory'), str(disk), case)
            disk.unlink()
            print(f'PASS {kind} VMU match memory: {case}', flush=True)
print(f'PASS {len(CASES) * 2} VMU match memory cases under ASan/UBSan')
