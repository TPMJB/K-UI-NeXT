#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the real physical-disc persistence path on independent FAT32/exFAT cards."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
COMMON=[
    'src/loader/vmu_persist_stage.c','src/core/vmu_durable.c','src/core/vmu_persist_session.c',
    'src/core/vmu_fs_admission.c','src/core/vmu_persist_profile.c','src/core/vmu_store.c',
    'src/core/vmu_image.c','src/core/hash.c','src/core/retail_image.c',
]
with tempfile.TemporaryDirectory(prefix='kui-disc-vmu-prepare-') as tmp:
    for fixture in ('test_disc_vmu_persist_stage','test_disc_vmu_persist_stage_fat32',
                    'test_vmu_persist_stage'):
        binary=Path(tmp)/fixture
        subprocess.run([
            os.environ.get('HOST_CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',
            '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
            '-I',str(ROOT/'include'),'-I',str(ROOT/'src/loader'),
            str(ROOT/'tests'/f'{fixture}.c'),*(str(ROOT/s) for s in COMMON),'-o',str(binary),
        ],check=True)
        subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
