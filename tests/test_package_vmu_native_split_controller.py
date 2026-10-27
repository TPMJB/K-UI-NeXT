#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Split delivery selection cannot silently include another isolation mode."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]


class SplitControllerSelectionTests(unittest.TestCase):
    def test_split_requires_demand_and_excludes_other_controls(self):
        argv=[sys.executable,str(ROOT/'tools/package_vmu_native_isolation_delivery.py')]
        for option in ('--reader-build','--vmu-build','--linked-report','--gui','--host-log','--output'):
            argv += [option,'unused']
        for flags in ([],['--demand-only','--cached-metadata'],
                      ['--demand-only','--maple-bypass'],
                      ['--demand-only','--maple-bypass','--maple-delay','--submission-delay','--post-start-delay']):
            with self.subTest(flags=flags):
                result=subprocess.run([*argv,'--split-controller',*flags],cwd=ROOT,text=True,capture_output=True)
                self.assertEqual(result.returncode,2)
                self.assertIn('--split-controller requires --demand-only and excludes other isolation controls',
                              result.stderr)


if __name__=='__main__':unittest.main()
