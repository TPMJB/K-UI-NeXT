#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded durable scan reports through production FatFs on FAT32/exFAT."""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile
from test_games_library_images import private_formatter, fatfs_source

ROOT=Path(__file__).resolve().parents[1]
CASES=("missing","roundtrip","maximum","empty","show","alternate","truncated","corrupt",
       "read-fail","write-fail","short-write","sync-fail","close-fail","verify-fail","both-corrupt",
       "equal-conflict","overflow","first-write-recovery")


def main():
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
    with tempfile.TemporaryDirectory(prefix='kui-scan-log-images-') as folder:
        base=Path(folder)
        formatter=private_formatter(base)
        binary=base/'games-scan-log-image'
        subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
            '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie',
            '-I'+str(ROOT/'include'),'-I'+str(fatfs_source()),
            str(ROOT/'tests/games_scan_log_image.c'),str(ROOT/'src/apps/games_scan_log.c'),
            str(ROOT/'src/core/data.c'),str(fatfs_source()/'ff.c'),str(fatfs_source()/'ffunicode.c'),
            '-Wl,--wrap=f_open,--wrap=f_close,--wrap=f_read,--wrap=f_write,--wrap=f_sync,--wrap=f_mkdir',
            '-o',str(binary)],cwd=ROOT,env=env,check=True)
        for filesystem in ('fat32','exfat'):
            clean=base/(filesystem+'-clean.img')
            subprocess.run([str(formatter),filesystem,str(clean)],env=env,check=True,stdout=subprocess.PIPE,text=True)
            for case in CASES:
                image=base/(filesystem+'-'+case+'.img');shutil.copyfile(clean,image)
                result=subprocess.run([str(binary),str(image),case],env=env,check=True,
                    stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30)
                assert case=='missing' or 'PASS '+case in result.stdout,result.stdout
                print('PASS '+filesystem+' '+case,flush=True)
                image.unlink()
    print(f'PASS {len(CASES)*2} scan-log filesystem scenarios',flush=True)


if __name__=='__main__':main()
