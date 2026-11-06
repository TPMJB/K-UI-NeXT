#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the production persistence pipeline with old/new read adapters."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
SOURCES=(
 'tests/test_native_vmu_stage_read_cache.c','src/loader/smash_fat_read_cache.c',
 'src/loader/vc2_ce_vmu_stage_cache.c','src/loader/vmu_persist_stage.c',
 'src/core/vmu_durable.c','src/core/vmu_persist_session.c','src/core/vmu_fs_admission.c',
 'src/core/vmu_persist_profile.c','src/core/vmu_store.c','src/core/vmu_image.c',
 'src/core/hash.c','src/core/retail_image.c')
def main():
    with tempfile.TemporaryDirectory(prefix='kui-native-card-cache-') as folder:
        for fs in ('exfat','fat32'):
            target=Path(folder)/fs
            command=[*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
                '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
                '-fno-sanitize-recover=all','-fno-pie','-no-pie','-ffunction-sections',
                '-fdata-sections','-Iinclude','-Isrc/loader']
            if fs=='fat32':command.append('-DKUI_STAGE_CACHE_FAT32=1')
            subprocess.run([*command,*SOURCES,'-Wl,--gc-sections','-o',str(target)],cwd=ROOT,check=True)
            subprocess.run([str(target)],cwd=ROOT,check=True,
                env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
if __name__=='__main__':main()
