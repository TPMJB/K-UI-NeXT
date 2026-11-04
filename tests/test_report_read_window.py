#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise stage-only metadata batching with an independent sector oracle."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class ReportReadWindowTests(unittest.TestCase):
    def test_bounded_two_way_cache(self):
        with tempfile.TemporaryDirectory(prefix='kui-report-window-') as tmp:
            exe = Path(tmp) / 'window'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-g',
                '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                '-Iinclude', 'tests/test_report_read_window.c', 'src/loader/report_stage_cache.c',
                '-o', str(exe)], cwd=ROOT, check=True)
            result = subprocess.run([str(exe)], cwd=ROOT, check=True, capture_output=True, text=True)
            self.assertIn('finite budgets passed', result.stdout)
            print(result.stdout.strip())
if __name__ == '__main__':
    unittest.main()
