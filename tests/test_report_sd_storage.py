#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the actual CMD24-only report protocol against a byte-level SD model."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReportSdStorageTests(unittest.TestCase):
    def test_cmd24_status_crc_and_refusals(self):
        with tempfile.TemporaryDirectory(prefix='kui-report-storage-') as tmp:
            exe = Path(tmp) / 'report-storage'
            subprocess.run([
                os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Iinclude', '-Isrc/loader',
                'tests/test_report_sd_storage.c', 'src/loader/report_sd_storage.c', '-o', str(exe),
            ], cwd=ROOT, check=True)
            result = subprocess.run([str(exe)], cwd=ROOT, check=True, capture_output=True, text=True)
            self.assertIn('terminal report CMD24 protocol model passed', result.stdout)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
