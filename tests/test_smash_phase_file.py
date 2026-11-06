#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual FatFs read-only mapping and raw report writes on FAT32/exFAT."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
from test_games_library_images import fatfs_source, private_formatter

ROOT=Path(__file__).resolve().parents[1]
def run(*command):
    subprocess.run(command,cwd=ROOT,check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
def main():
    source=fatfs_source()
    flags=[*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
        '-ffunction-sections','-fdata-sections','-Iinclude','-Isrc/loader','-I'+str(source)]
    common=['tests/test_smash_phase_file.c','src/core/smash_menu_capsule.c',str(source/'ff.c'),str(source/'ffunicode.c')]
    with tempfile.TemporaryDirectory(prefix='kui-smash-phase-file-') as tmp:
        folder=Path(tmp);seed=folder/'seed';test=folder/'test'
        run(*flags,'-DKUI_PHASE_FILE_SEED=1',*common,'-Wl,--gc-sections','-o',str(seed))
        run(*flags,'-DKUI_SMASH_PHASE_FILE_HOST_TEST=1','-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1',
            *common,'src/loader/smash_phase_file.c','src/loader/smash_phase_string.c','-Wl,--gc-sections','-o',str(test))
        formatter=private_formatter(folder)
        for kind,layout in (('fat32','one-run'),('fat32','two-runs'),('exfat','one-run')):
            image=folder/(kind+'-'+layout+'.img');metadata=folder/(kind+'-'+layout+'.meta')
            run(str(formatter),kind,str(image));run(str(seed),str(image),str(metadata),layout);run(str(test),str(image),str(metadata))
if __name__=='__main__':main()
