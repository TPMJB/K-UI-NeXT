#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and run the production shared-card lifecycle storage regression."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCES = (
    'tests/test_smash_menu_successive_card.c',
    'src/core/vmu_durable.c',
    'src/core/vmu_store.c',
    'src/core/vmu_emulation.c',
    'src/loader/sd_reader.c',
    'src/loader/vmu_backing.c',
    'src/dreamcast/sci_sd_storage.c',
    'src/core/hash.c',
    'src/core/retail_image.c',
    'src/core/data.c',
)


class SuccessiveSharedCardTest(unittest.TestCase):
    def test_production_backend_across_fresh_workers(self):
        compiler = shlex.split(os.environ.get('HOST_CC', 'cc'))
        self.assertTrue(compiler, 'HOST_CC must name a compiler')
        with tempfile.TemporaryDirectory(prefix='kui-smash-successive-card-') as folder:
            binary = Path(folder) / 'successive-card'
            command = [*compiler, '-std=c11', '-O1', '-fno-pie', '-no-pie',
                '-Wall', '-Wextra', '-Werror', '-pedantic',
                '-fsanitize=address,undefined', '-g', '-Iinclude', '-Isrc/loader',
                '-DKUI_VMU_READONLY_PROOF=4', '-DKUI_VMU_EXPERIMENTAL=0',
                *SOURCES, '-o', str(binary)]
            built = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], cwd=ROOT, text=True, capture_output=True,
                env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'}, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout, end='')


if __name__ == '__main__':
    unittest.main()
