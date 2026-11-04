#!/usr/bin/env python3
"""Sanitize the pure production rolling-window implementation."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class DemandWindowTests(unittest.TestCase):
    def test_production_window(self):
        with tempfile.TemporaryDirectory(prefix="kui-demand-window-") as temp:
            output=Path(temp)/"window"
            subprocess.run([os.environ.get("CC","cc"),"-std=c11","-Wall","-Wextra","-Werror",
                "-fsanitize=undefined","-fno-sanitize-recover=all","-Iinclude",
                "tests/test_retail_demand_window.c","-o",str(output)],cwd=ROOT,check=True,
                capture_output=True,text=True)
            result=subprocess.run([str(output)],cwd=ROOT,check=True,capture_output=True,text=True)
            self.assertIn("independent contract checks OK",result.stdout)


if __name__=="__main__":unittest.main()
