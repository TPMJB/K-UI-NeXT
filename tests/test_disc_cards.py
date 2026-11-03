#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Physical disc-card management with actual FAT32/exFAT and synthetic drive."""
import importlib.util
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
from test_games_library_images import private_formatter

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('disc_library_fixture',ROOT/'tests/test_vmu_library.py')
library=importlib.util.module_from_spec(spec);spec.loader.exec_module(library)
SOURCES=('tests/test_disc_cards.c',*library.SOURCES[1:], 'src/apps/disc_cards.c',
    'src/core/vmu_image_pack.c','src/core/vmu_import_plan.c','src/core/vmu_import_match.c',
    'src/core/vmu_image_subset.c')
CASES=('identify','wince-identify','create','link-active','link-archive','defaults','pagination','legacy-copy','disc-swap','no-disc',
       'storage-swap','cancel','stale-token','wrong-row','invalid-path','source-changed','catalogue-stale','retry',
       'legacy-default-create','legacy-default-link','sharing-removed','sharing-added','legacy-identity-changed',
       'pending-copy-identity','pending-copy-payload')

def run(*args):
    result=subprocess.run(args,cwd=ROOT,text=True,capture_output=True,
        env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
    if result.returncode:
        print(result.stdout,end='');print(result.stderr,end='')
        raise SystemExit(f'Disc-card check failed: {args} ({result.returncode})')
    return result.stdout

with tempfile.TemporaryDirectory(prefix='kui-disc-cards-') as temporary:
    base=Path(temporary);binary=base/'disc-cards';formatter=private_formatter(base)
    additions=tuple(str(p.relative_to(ROOT)) for p in sorted((ROOT/'src/core').glob('vmu_game_candidates*.c')))
    sources=tuple(dict.fromkeys((*SOURCES,*additions)))
    run(*shlex.split(os.environ.get('CC','cc')),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
        '-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
        '-DKUI_VMU_GAME_CARDS=1','-Iinclude','-I.deps/fatfs/source','-Isrc/dreamcast',*sources,'-o',str(binary))
    for kind in ('fat32','exfat'):
        raw=base/f'{kind}-raw.fs';run(str(formatter),kind,str(raw));payload=bytearray(raw.read_bytes())
        if kind=='fat32':
            struct.pack_into('<I',payload,28,2048)
            backup=struct.unpack_from('<H',payload,50)[0]
            struct.pack_into('<I',payload,backup*512+28,2048)
        mbr=bytearray(512);struct.pack_into('<B3xB3xII',mbr,446,0,0x0c if kind=='fat32' else 7,2048,len(payload)//512)
        mbr[510:512]=b'\x55\xaa';seed=mbr+bytes(2048*512-512)+payload+bytes(1024*1024)
        disk=base/'disc.img'
        for case in CASES:
            disk.write_bytes(seed)
            assert f'PASS disc cards {case}' in run(str(binary),str(disk),case)
            print(f'PASS {kind} disc cards: {case}',flush=True)
print(f'PASS {len(CASES)*2} physical disc-card filesystem cases under ASan/UBSan')
