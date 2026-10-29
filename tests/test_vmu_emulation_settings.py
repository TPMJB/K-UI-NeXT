#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Global emulation preference codec, fault cuts and actual mounted FatFs."""
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class EmulationSettings(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix='kui-vmu-settings-')
        cls.dir = Path(cls.work.name)
        cls.env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
        cls.flags = [*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-O1', '-g',
                     '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-fsanitize=address,undefined',
                     '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                     '-I', str(ROOT / 'include'), '-I', str(ROOT / '.deps/fatfs/source')]

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def test_codec_and_file_faults(self):
        binary = self.dir / 'settings'
        sources = ('tests/test_vmu_emulation_settings.c', 'src/core/vmu_emulation_settings.c', 'src/core/data.c')
        subprocess.run([*self.flags, *[str(ROOT / p) for p in sources], '-o', str(binary)], check=True)
        run = subprocess.run([str(binary)], capture_output=True, text=True, env=self.env)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn('checks passed', run.stdout)

    @unittest.skipUnless(shutil.which('mkfs.fat') and shutil.which('mkfs.exfat'), 'needs mkfs.fat/mkfs.exfat')
    def test_real_filesystems_and_write_boundaries(self):
        binary = self.dir / 'settings-image'
        sources = ('tests/vmu_emulation_settings_image.c', 'src/core/vmu_emulation_settings.c',
                   'src/core/data.c', 'src/core/diskio.c', 'src/core/command.c', 'src/core/clock.c',
                   '.deps/fatfs/source/ff.c', '.deps/fatfs/source/ffunicode.c')
        subprocess.run([*self.flags, *[str(ROOT / p) for p in sources], '-o', str(binary)], check=True)
        for kind, cluster in (('fat32', None), ('exfat', '4K'), ('exfat', '128K')):
            with self.subTest(filesystem=kind, cluster=cluster):
                filesystem = self.dir / 'volume.fs'
                with filesystem.open('wb') as out:
                    out.truncate(64 * 1024 * 1024)
                command = ['mkfs.fat', '-F', '32', '-s', '1', '-h', '2048'] if kind == 'fat32' else [
                    'mkfs.exfat', '-c', cluster]
                subprocess.run([*command, str(filesystem)], capture_output=True, check=True)
                payload = filesystem.read_bytes(); mbr = bytearray(512)
                struct.pack_into('<B3xB3xII', mbr, 446, 0, 0x0c if kind == 'fat32' else 7, 2048, len(payload)//512)
                mbr[510:512] = b'\x55\xaa'
                image = self.dir / 'disk.img'
                image.write_bytes(mbr + bytes(2048*512-512) + payload + bytes(1024*1024))
                run = subprocess.run([str(binary), str(image)], capture_output=True, text=True, env=self.env)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertIn('write boundaries passed', run.stdout)


if __name__ == '__main__':
    unittest.main()
