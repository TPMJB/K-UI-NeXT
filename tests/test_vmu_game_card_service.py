#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Game-card service on host FatFs over partitioned FAT32 and exFAT images
made by mkfs.fat and mkfs.exfat, including the user's 128 KiB exFAT clusters."""
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ('tests/test_vmu_game_card_service.c', 'src/core/vmu_game_card_service.c',
           'src/core/vmu_game_card.c', 'src/core/vmu_game_assign.c', 'src/core/vmu_fs_admission.c', 'src/core/vmu_persist_profile.c',
           'src/core/vmu_image.c', 'src/core/retail_image.c', 'src/core/hash.c', 'src/core/data.c',
           'src/core/diskio.c', 'src/core/clock.c', 'src/core/command.c',
           '.deps/fatfs/source/ff.c', '.deps/fatfs/source/ffunicode.c')
OFFSET = 2048
FS_BYTES = 64 * 1024 * 1024


def partitioned(filesystem, kind):
    """One MBR partition at sector 2048 holding the filesystem, like an SD card."""
    data = filesystem.read_bytes()
    mbr = bytearray(512)
    struct.pack_into('<B3xB3xII', mbr, 446, 0, 0x0c if kind == 'fat32' else 0x07,
                     OFFSET, len(data) // 512)
    mbr[510:512] = b'\x55\xaa'
    return bytes(mbr) + bytes(OFFSET * 512 - 512) + data + bytes(1024 * 1024)


@unittest.skipUnless(shutil.which('mkfs.fat') and shutil.which('mkfs.exfat'), 'needs mkfs.fat and mkfs.exfat')
class GameCardService(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix='kui-game-card-')
        cls.dir = Path(cls.work.name)
        cls.tool = cls.dir / 'service'
        cc = shlex.split(os.environ.get('CC', 'cc'))
        subprocess.run([*cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I', str(ROOT / 'include'), '-I', str(ROOT / '.deps/fatfs/source'),
                        *[str(ROOT / s) for s in SOURCES], '-o', str(cls.tool)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def run_image(self, kind, *arguments):
        filesystem = self.dir / f'{kind}.fs'
        with filesystem.open('wb') as out:
            out.truncate(FS_BYTES)
        if kind == 'fat32':
            command = ['mkfs.fat', '-F', '32', '-s', '1', '-h', str(OFFSET), '-n', 'KUITEST', *arguments]
        else:
            command = ['mkfs.exfat', '-L', 'KUITEST', *arguments]
        subprocess.run([*command, str(filesystem)], check=True, capture_output=True)
        image = self.dir / f'{kind}.img'
        image.write_bytes(partitioned(filesystem, kind))
        run = subprocess.run([str(self.tool), str(image)], capture_output=True, text=True,
                             env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
        self.assertEqual(run.returncode, 0, run.stderr)
        return run.stdout

    def test_fat32(self):
        self.assertIn('(FAT32)', self.run_image('fat32'))

    def test_exfat_4k_clusters(self):
        self.assertIn('(exFAT)', self.run_image('exfat', '-c', '4K'))

    def test_exfat_128k_clusters(self):
        self.assertIn('(exFAT)', self.run_image('exfat', '-c', '128K'))


if __name__ == '__main__':
    unittest.main()
