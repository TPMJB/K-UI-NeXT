#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare six production capsule-file operations on real FatFs volumes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ('tests/test_smash_capsule_launch_io.c', 'src/core/smash_menu_capsule.c',
           'src/core/diskio.c', 'src/core/data.c', 'src/core/clock.c', 'src/core/hash.c',
           '.deps/fatfs/source/ff.c', '.deps/fatfs/source/ffunicode.c')


def audit():
    with tempfile.TemporaryDirectory(prefix='kui-capsule-launch-io-') as temporary:
        work = Path(temporary)
        config = (ROOT / 'config/ffconf.h').read_text()
        old = '#define FF_USE_MKFS 0'
        assert config.count(old) == 1
        header = work / 'ffconf-test.h'
        header.write_text(config.replace(old, '#define FF_USE_MKFS 1'))
        binary = work / 'capsule-launch-io'
        subprocess.run([*shlex.split(os.environ.get('HOST_CC', 'cc')),
                        '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-ffunction-sections', '-fdata-sections',
                        '-Wl,--gc-sections', '-Iinclude', '-I.deps/fatfs/source', '-include', str(header),
                        *SOURCES, '-o', str(binary)], cwd=ROOT, check=True)
        result = subprocess.run([str(binary)], cwd=ROOT, check=True, capture_output=True, text=True,
                                env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'halt_on_error=1'})
    rows = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(rows) == 18
    comparisons = []
    for baseline in rows[:9]:
        candidate = next(row for row in rows[9:] if all(row[key] == baseline[key]
                         for key in ('filesystem', 'cluster_bytes', 'scenario')))
        for key in ('read_sectors', 'write_sectors', 'syncs', 'disk_sha256',
                    'payload_read_sectors', 'payload_write_sectors'):
            assert baseline[key] == candidate[key], (key, baseline, candidate)
        assert candidate['reads'] <= baseline['reads']
        assert candidate['writes'] <= baseline['writes']
        if baseline['filesystem'] == 'exFAT' and baseline['cluster_bytes'] == 131072:
            assert candidate['reads'] < baseline['reads']
        comparisons.append(dict(filesystem=baseline['filesystem'], cluster_bytes=baseline['cluster_bytes'],
                                scenario=baseline['scenario'], reads_before=baseline['reads'],
                                reads_after=candidate['reads'], writes_before=baseline['writes'],
                                writes_after=candidate['writes'], disk_sha256=candidate['disk_sha256']))
    inputs = ('src/apps/smash_menu_capsule_file_prepare.inc', *SOURCES,
              'tests/test_smash_capsule_launch_io.py', 'config/ffconf.h')
    return dict(schema='kui.smash.capsule-launch-io.v1', status='PASS',
                actual_fatfs=True, physical_device_callback_counts=True, measured_console_time=False,
                geometries=['FAT32/512', 'exFAT/4096', 'exFAT/131072'],
                six_file_sizes=[246272, 94720, 94720, 63488, 50688, 50688],
                helper_immutable_bytes=49152, scenarios=['fresh', 'reuse', 'changed'],
                payload_range_counters_available_for_preexisting_files_only=True,
                whole_disk_bytes_and_sector_counts_and_syncs_identical=True,
                comparisons=comparisons, raw_results=rows,
                source_sha256={name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in inputs})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    record = audit()
    text = json.dumps(record, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
