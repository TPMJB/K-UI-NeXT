#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile and execute independent VMU merge fixtures under sanitizers."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class VmuImageMerge(unittest.TestCase):
    def test_lossless_merge(self):
        with tempfile.TemporaryDirectory(prefix="kui-vmu-merge-") as temporary:
            binary = str(Path(temporary) / "merge")
            command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O1", "-g",
                       "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-fno-pie", "-no-pie",
                       "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(ROOT / "include")]
            command += [str(ROOT / source) for source in ("tests/test_vmu_image_merge.c",
                        "src/core/vmu_image_merge.c", "src/core/vmu_image_pack.c", "src/core/vmu_image.c")]
            subprocess.run([*command, "-o", binary], check=True)
            result = subprocess.run([binary], capture_output=True, text=True,
                                    env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS lossless VMU merge", result.stdout)


if __name__ == "__main__":
    unittest.main()
