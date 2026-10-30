#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run production durable preparation for the two VMU-only transports."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sources=[
    'tests/test_vmu_only_persist_stage.c','src/loader/vmu_persist_stage.c',
    'src/core/vmu_durable.c','src/core/vmu_persist_session.c','src/core/vmu_fs_admission.c',
    'src/core/vmu_persist_profile.c','src/core/vmu_store.c','src/core/vmu_image.c',
    'src/core/hash.c','src/core/retail_image.c',
]
with tempfile.TemporaryDirectory(prefix='kui-vmu-only-prepare-') as tmp:
    binary=Path(tmp)/'prepare'
    subprocess.run([
        os.environ.get('HOST_CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',
        '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-DKUI_NATIVE_VMU_ONLY=1','-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1',
        '-DKUI_NATIVE_SPLIT_CONTROLLER_ISOLATION=1','-DKUI_NATIVE_SPLIT_CONTROLLER_GENERAL=1',
        '-DKUI_NATIVE_GENERAL_LEAN_LAYOUT=1','-DKUI_VMU_NATIVE_CACHED_CODE=1',
        '-DKUI_VMU_EXPERIMENTAL=0','-DKUI_VMU_READONLY_PROOF=4',
        '-I',str(ROOT/'include'),'-I',str(ROOT/'src/loader'),
        *(str(ROOT/s) for s in sources),'-o',str(binary),
    ],check=True)
    subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
