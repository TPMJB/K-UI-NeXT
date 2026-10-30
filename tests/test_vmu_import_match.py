#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pure matcher through admitted synthetic VMU images and VMS metadata."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ImportMatch(unittest.TestCase):
    def test_admitted_metadata_families(self):
        with tempfile.TemporaryDirectory(prefix='kui-vmu-import-match-') as temporary:
            binary = Path(temporary) / 'test'
            subprocess.run([*shlex.split(os.environ.get('CC', 'cc')), '-std=c11', '-O1', '-g',
                            '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            '-I', str(ROOT / 'include'),
                            *[str(ROOT / source) for source in (
                                'tests/test_vmu_import_match.c', 'src/core/vmu_import_match.c',
                                'src/core/vmu_import_plan.c', 'src/core/vmu_image_subset.c', 'src/core/vmu_image.c')],
                            '-o', str(binary)], check=True, capture_output=True, text=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True,
                                 env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('Representative 29-file metadata inventory:', run.stdout)
            print(run.stdout, end='')


if __name__ == '__main__':
    unittest.main()
