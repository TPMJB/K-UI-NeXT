#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Common VMU history call budgets and durable-receipt regression audit."""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
INPUTS = (
    'src/core/vmu_history.c', 'src/core/vmu_history_session.c', 'src/core/vmu_library.c',
    'include/kui/vmu_history.h', 'tests/test_vmu_history_session_fast.c',
    'tests/test_vmu_history_batch.c', 'tests/test_vmu_history.c', 'tests/test_vmu_history.py',
    'tests/test_vmu_library.c', 'tests/test_vmu_library.py', 'tests/test_vmu_history_performance.py',
    'tests/test_vmu_history_launch_io.c', 'tests/test_vmu_history_launch_io.py',
    'src/core/diskio.c','include/kui/media.h',
    'tests/test_media_read_cache.c','tests/test_media_read_cache.py',
    'tests/test_vmu_history_receipt_io.c','tests/test_vmu_history_receipt_io.py',
)


def run(command, environment):
    result = subprocess.run(command, cwd=ROOT, env=environment, capture_output=True, text=True, timeout=300)
    if result.returncode:
        raise RuntimeError(f'{command[0]} failed ({result.returncode})\n{result.stdout}{result.stderr}')
    return result.stdout, result.stderr


def audit(full=True):
    import json
    compiler = shlex.split(os.environ.get('HOST_CC', 'cc'))
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie',
             '-ffunction-sections', '-fdata-sections', '-Iinclude', '-I.deps/fatfs/source']
    environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    outputs = {};launch_io=None
    with tempfile.TemporaryDirectory(prefix='kui-history-performance-') as directory:
        for name, sources in (
            ('batch', ['tests/test_vmu_history_batch.c']),
            ('library_batch', ['tests/test_vmu_history_batch.c']),
            ('receipt', ['tests/test_vmu_history_session_fast.c', 'src/core/vmu_game_card.c', 'src/core/data.c']),
        ):
            binary = str(Path(directory) / name)
            extra=['-DKUI_SMASH_LAUNCH_TIMING=1'] if name in ('batch','library_batch') else []
            if name=='library_batch':extra.append('-DKUI_VMU_LIBRARY_BATCH_TEST=1')
            run([*compiler, *flags, *extra, *sources, '-Wl,--gc-sections', '-o', binary], environment)
            outputs[name] = run([binary], environment)[0].strip()
    outputs['metadata_cache'] = run(['python3','tests/test_media_read_cache.py'],environment)[0].strip()
    if full:
        outputs['real_fatfs_history'] = run(['python3', 'tests/test_vmu_history.py'], environment)[0].strip()
        library_out, library_err = run(['python3', 'tests/test_vmu_library.py'], environment)
        if 'skipped' in library_err.lower():
            raise AssertionError('library geometry/corruption tests must execute, not skip')
        outputs['real_fatfs_library'] = (library_out + library_err).strip()
        from test_vmu_history_launch_io import audit as launch_audit
        launch_io=launch_audit()
    result = {
        'ok': True, 'scope': 'shared VMU library/history path for every game; no title gate',
        'history_128k_read_calls_per_pass': {'previous_4KiB_windows': 32, 'after': 4,
            'window_bytes': 32768, 'heap_failure_window_bytes': 4096, 'verification_passes': 2},
        'same_card_pending_begin': {'checkpoint_calls_before': 2, 'checkpoint_calls_after': 1,
                                    'receipt_allocation_and_publication_before': 2,
                                    'receipt_allocation_and_publication_after': 1},
        'different_card_receipts_independently_recovered': True,
        'old_pending_generation_retained_until_verified_replacement': True,
        'measured_console_time': False, 'full_real_fatfs_regressions_executed': full,
        'complete_session_launch_io': launch_io,
        'verified_history_records_reused_only_within_exclusive_operation': True,
        'outputs': outputs,
        'source_sha256': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in INPUTS},
    }
    return result


if __name__ == '__main__':
    import json
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--focused', action='store_true', help='run deterministic call budgets only')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    result = audit(full=not args.focused)
    print(json.dumps(result, indent=2) if args.json else '\n'.join(result['outputs'].values()))
