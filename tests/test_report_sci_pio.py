#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual terminal byte-PIO callbacks against an MMIO/SD wire model."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReportSciPioTests(unittest.TestCase):
    def test_mmio_protocol_and_cleanup(self):
        with tempfile.TemporaryDirectory(prefix='kui-report-pio-') as tmp:
            exe = Path(tmp) / 'report-pio'
            subprocess.run([
                os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=undefined', '-fno-sanitize-recover=all', '-DKUI_REPORT_SCI_PIO_TEST=1',
                '-Iinclude', '-Isrc/loader', 'tests/test_report_sci_pio.c',
                'src/loader/report_sci_pio.c', '-o', str(exe),
            ], cwd=ROOT, check=True)
            result = subprocess.run([str(exe)], cwd=ROOT, check=True, capture_output=True, text=True)
            self.assertIn('terminal report SCI byte PIO:', result.stdout)
            self.assertIn('checks passed', result.stdout)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
