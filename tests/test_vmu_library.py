#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only VMU catalog and name durability on real FAT32/exFAT volumes."""
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCES = (
    'tests/test_vmu_library.c', 'src/core/vmu_library.c',
    'src/core/vmu_collection.c', 'src/core/vmu_game_card_service.c',
    'src/core/vmu_game_card.c', 'src/core/vmu_game_assign.c',
    'src/core/vmu_game_candidates.c',
    'src/core/vmu_fs_admission.c', 'src/core/vmu_persist_profile.c',
    'src/core/vmu_image.c', 'src/core/vmu_store.c', 'src/core/retail_image.c',
    'src/core/hash.c', 'src/core/data.c', 'src/core/destination.c',
    'src/core/game_cover.c', 'src/core/vmu_description.c',
    'src/core/destination_file.c', 'src/core/diskio.c',
    'src/core/clock.c', 'src/core/command.c',
    '.deps/fatfs/source/ff.c', '.deps/fatfs/source/ffunicode.c',
)


@unittest.skipUnless(shutil.which('mkfs.fat') and shutil.which('mkfs.exfat'),
                     'needs mkfs.fat and mkfs.exfat')
class VmuLibrary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix='kui-vmu-library-')
        cls.root = Path(cls.work.name)
        cls.binary = cls.root/'library'
        subprocess.run([
            *shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-O1', '-g',
            '-Wall', '-Wextra', '-Werror', '-Wpedantic',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-fno-pie', '-no-pie', '-ffunction-sections', '-fdata-sections',
            '-Wl,--gc-sections', '-I', str(ROOT/'include'),
            '-I', str(ROOT/'.deps/fatfs/source'),
            *[str(ROOT/name) for name in SOURCES], '-o', str(cls.binary),
        ], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def check_volume(self, kind, cluster=None):
        filesystem = self.root/'volume.fs'
        with filesystem.open('wb') as out:
            out.truncate(64*1024*1024)
        command = ['mkfs.fat', '-F', '32', '-s', '1', '-h', '2048'] if kind == 'fat32' else [
            'mkfs.exfat', '-c', cluster]
        subprocess.run([*command, str(filesystem)], check=True, capture_output=True)
        payload = filesystem.read_bytes()
        mbr = bytearray(512)
        struct.pack_into('<B3xB3xII', mbr, 446, 0, 0x0c if kind == 'fat32' else 7,
                         2048, len(payload)//512)
        mbr[510:512] = b'\x55\xaa'
        image = self.root/'disk.img'
        image.write_bytes(mbr+bytes(2048*512-512)+payload+bytes(1024*1024))
        result = subprocess.run([str(self.binary), str(image)], capture_output=True,
                                text=True, timeout=45,
                                env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.assertIn('VMU library', result.stdout)
        self.assertIn('write boundaries passed', result.stdout)
        print(result.stdout.strip())

    def test_fat32(self):
        self.check_volume('fat32')

    def test_exfat_4k(self):
        self.check_volume('exfat', '4K')

    def test_exfat_128k(self):
        self.check_volume('exfat', '128K')


if __name__ == '__main__':
    unittest.main()
