#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Per-game wizard publication over actual FAT32 and exFAT filesystems."""
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('library_filesystem_fixture',ROOT/'tests/test_vmu_library.py')
library=importlib.util.module_from_spec(spec);spec.loader.exec_module(library)
SOURCES=('tests/test_vmu_wizard_filesystems.c',*library.SOURCES[1:],
    'src/apps/vmu_wizard.c','src/core/vmu_image_pack.c','src/core/vmu_image_subset.c',
    'src/core/vmu_image_merge.c','src/core/vmu_import_plan.c','src/core/vmu_import_match.c')


class WizardFilesystems(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for tool in ('mkfs.fat','mkfs.exfat'):
            if not shutil.which(tool):raise RuntimeError('Required real filesystem tool is missing: '+tool)
        cls.work=tempfile.TemporaryDirectory(prefix='kui-wizard-filesystems-')
        cls.root=Path(cls.work.name);cls.binary=cls.root/'wizard'
        additions=tuple(str(p.relative_to(ROOT)) for base in ('src/core','src/apps')
            for p in sorted((ROOT/base).glob('vmu_game_candidates*.c')))
        sources=tuple(dict.fromkeys((*SOURCES,*additions)))
        subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O1','-g',
            '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
            '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie',
            '-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DKUI_VMU_GAME_CARDS=1',
            '-Iinclude','-I.deps/fatfs/source','-Isrc/dreamcast',*sources,'-o',str(cls.binary)],cwd=ROOT,check=True)

    @classmethod
    def tearDownClass(cls):cls.work.cleanup()

    def volume(self,kind,cluster=None):
        filesystem=self.root/'volume.fs'
        with filesystem.open('wb') as out:out.truncate(64*1024*1024)
        command=['mkfs.fat','-F','32','-s','1','-h','2048'] if kind=='fat32' else ['mkfs.exfat','-c',cluster]
        subprocess.run([*command,str(filesystem)],check=True,capture_output=True)
        payload=filesystem.read_bytes();mbr=bytearray(512)
        struct.pack_into('<B3xB3xII',mbr,446,0,0x0c if kind=='fat32' else 7,2048,len(payload)//512)
        mbr[510:512]=b'\x55\xaa';image=self.root/'disk.img'
        image.write_bytes(mbr+bytes(2048*512-512)+payload+bytes(1024*1024))
        result=subprocess.run([str(self.binary),str(image)],cwd=ROOT,text=True,capture_output=True,
            timeout=60,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('VMU wizard real filesystems',result.stdout);print(result.stdout.strip())

    def test_fat32(self):self.volume('fat32')
    def test_exfat_4k(self):self.volume('exfat','4K')
    def test_exfat_128k(self):self.volume('exfat','128K')


if __name__=='__main__':unittest.main()
