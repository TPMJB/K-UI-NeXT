#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real FAT32/exFAT checkpoint/restore cuts with a private FatFs formatter."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def main():
    spec=importlib.util.spec_from_file_location('vmu_library_fixture',ROOT/'tests/test_vmu_library.py')
    library=importlib.util.module_from_spec(spec);spec.loader.exec_module(library)
    config=(ROOT/'config/ffconf.h').read_text();old='#define FF_USE_MKFS 0';assert config.count(old)==1
    with tempfile.TemporaryDirectory(prefix='kui-vmu-history-') as directory:
        folder=Path(directory);header=folder/'ffconf-test.h';header.write_text(config.replace(old,'#define FF_USE_MKFS 1'))
        flags=[*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
            '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie',
            '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Iinclude','-I.deps/fatfs/source']
        codec=folder/'record'
        subprocess.run([*flags,'tests/test_vmu_history_record.c','src/core/vmu_history_record.c',
            'src/core/vmu_game_card.c','src/core/retail_image.c','src/core/hash.c','src/core/data.c',
            '-o',str(codec)],cwd=ROOT,check=True)
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
        subprocess.run([str(codec)],cwd=ROOT,env=env,check=True)
        sources=[name for name in library.SOURCES if name!='tests/test_vmu_library.c']
        binary=folder/'history'
        subprocess.run([*flags,'-include',str(header),'tests/test_vmu_history.c',
            'src/core/vmu_history.c','src/core/vmu_history_record.c','src/core/vmu_history_session.c',
            *sources,'-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary)],cwd=ROOT,env=env,check=True,timeout=240)


if __name__=='__main__':main()
