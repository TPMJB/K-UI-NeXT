#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent automatic-import matcher ambiguity, grouping and bounds tests."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AdversarialMatcher(unittest.TestCase):
    def test_independent_contract(self):
        with tempfile.TemporaryDirectory(prefix="kui-vmu-match-adversarial-") as temporary:
            binary = Path(temporary) / "test"
            subprocess.run([
                *shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O1", "-g",
                "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                "-Iinclude", "tests/test_vmu_import_match_adversarial.c",
                "src/core/vmu_import_match.c", "-o", str(binary),
            ], cwd=ROOT, check=True)
            subprocess.run([str(binary)], check=True, timeout=60,
                           env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))


if __name__ == "__main__":
    unittest.main()
