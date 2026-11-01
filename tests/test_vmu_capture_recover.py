#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise public recovery and retained strict capture without any SD image."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SOURCES=(
    'src/core/command.c','src/core/data.c','src/core/diskio.c','src/core/clock.c',
    'src/core/destination.c','src/core/destination_file.c','src/core/storage_probe.c',
    '.deps/fatfs/source/ff.c','.deps/fatfs/source/ffunicode.c',
    'src/core/vmu_collection.c','src/core/vmu_image.c','src/core/vmu_image_subset.c',
    'src/core/vmu_save_snapshot.c','src/core/vmu_image_pack.c','src/core/vmu_image_merge.c',
    'src/core/vmu_description.c','src/core/vmu_library.c','src/core/vmu_game_assign.c',
    'src/core/vmu_game_card.c','src/core/vmu_persist_profile.c','src/core/retail_image.c',
    'src/core/game_cover.c','src/core/vmu_game_candidates.c','src/core/vmu_import_plan.c',
    'src/core/hash.c','src/apps/vmu.c','tests/test_vmu_capture_recover.c')
CASES=(
    'read-recovery-healthy','read-recovery-once','read-recovery-last',
    'read-recovery-between-passes','read-recovery-recovered-then-changed',
    'strict-between-passes','read-recovery-retained-change','read-recovery-timeout',
    'read-recovery-malformed','read-recovery-cancel','read-recovery-identity',
    'read-recovery-short','read-recovery-oversize','read-recovery-prior-oversize',
    'metadata-change','dirty-metadata','orphan-allocation','missing-source',
    'invalid-slot','invalid-size')
with tempfile.TemporaryDirectory(prefix='vmu-capture-recover-') as temporary:
    binary=Path(temporary)/'capture'
    subprocess.run([*shlex.split(os.environ.get('CC','cc')),
        '-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
        '-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-DKUI_VMU_CAPTURE_TEST_WRAP_CRC=1','-Iinclude','-I.deps/fatfs/source',
        '-Ifirmware/kui-wifi/components/kwlink/include','-Itests/vmu_stubs','-Isrc/dreamcast',
        *SOURCES,'-Wl,--wrap=kui_crc32,--wrap=kui_vmu_image_pack','-o',str(binary)],
        cwd=ROOT,check=True)
    for case in CASES:
        result=subprocess.run([str(binary),case],cwd=ROOT,text=True,capture_output=True,
            env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
        if result.returncode:
            print(result.stdout,end='');print(result.stderr,end='')
            raise SystemExit(f'Public capture case failed: {case} ({result.returncode})')
        assert f'PASS independent public whole-save capture: {case}' in result.stdout
        print(f'PASS independent public whole-save capture: {case}',flush=True)
print(f'PASS {len(CASES)} public capture API cases under ASan/UBSan; no source or SD writes')
