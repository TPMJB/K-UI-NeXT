#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise menu backups on disposable actual FAT32/exFAT images under ASan."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from test_games_library_images import private_formatter

ROOT=Path(__file__).resolve().parents[1]
CASES=('healthy','unused-noise','raw-unused-noise','selected-recovery','all-files-recovery','whole-recovery-stable',
       'whole-recovery-source-wave','whole-recovery-alternating','whole-recovery-timeout',
       'whole-recovery-malformed','whole-recovery-identity','whole-recovery-metadata-pre',
       'whole-recovery-metadata-post','whole-recovery-retained-ram','whole-recovery-cancel',
       'metadata-change','stale-list','write-fail','sync-fail','readback-fail',
       'readback-corrupt','cancel-write')

def run(*args):
    result=subprocess.run(args,cwd=ROOT,text=True,capture_output=True,
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
    if result.returncode:
        print(result.stdout,end='');print(result.stderr,end='')
        raise SystemExit(f'Menu backup failed: {args} ({result.returncode})')
    return result.stdout

with tempfile.TemporaryDirectory(prefix='kui-menu-vmu-backup-') as temporary:
    folder=Path(temporary);binary=ROOT/'build/test-vmu-menu-backup';formatter=private_formatter(folder)
    run('make','-s','build/test-vmu-menu-backup')
    for kind in ('fat32','exfat'):
        seed=folder/f'{kind}-seed.img';run(str(formatter),kind,str(seed));run(str(binary),str(seed),'seed')
        for case in CASES:
            disk=folder/f'{kind}-case.img';shutil.copyfile(seed,disk)
            assert f'PASS menu VMU backup: {case}' in run(str(binary),str(disk),case)
            disk.unlink();print(f'PASS {kind} menu VMU backup: {case}',flush=True)
print(f'PASS {len(CASES)*2} menu backup filesystem cases under ASan/UBSan')
