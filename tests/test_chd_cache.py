#!/usr/bin/env python3
"""Portable CHD-to-GDI staging exporter and exhaustive callback fault tests."""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CHDCache(unittest.TestCase):
    def test_export_and_fault_boundaries(self):
        with tempfile.TemporaryDirectory(prefix="kui-chd-cache-") as temporary:
            executable = Path(temporary) / "cache-test"
            compiler = shlex.split(os.environ.get("CC", "cc"))
            sanitizers = shlex.split(os.environ.get("KUI_CHD_CACHE_SANITIZERS", "-fsanitize=address,undefined -fno-omit-frame-pointer"))
            command = [*compiler, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                       "-Iinclude", *sanitizers, "tests/test_chd_cache.c", "src/core/chd_cache.c",
                       "src/core/hash.c", "src/core/data.c", "-o", str(executable)]
            build = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stderr.decode())
            result = subprocess.run([str(executable)], cwd=ROOT, capture_output=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            lines = result.stdout.decode().splitlines()
            self.assertTrue(lines[-1].startswith("CHD cache checks passed:"), result.stdout.decode())
            actual = {int(row.split()[1]): row.split()[2] for row in lines if row.startswith("track ")}
            for track, size in enumerate((32 * 2352, 17 * 2352, 33 * 2048)):
                data = bytes(((i * 37 + (i >> 7) * 11 + track * 53) ^ ((i >> 12) * 19)) & 255 for i in range(size))
                self.assertEqual(actual[track], hashlib.sha256(data).hexdigest())
            descriptor = b"3\n1 0 4 2352 track01.bin 0\n2 182 0 2352 track02.bin 0\n3 45000 4 2048 track03.bin 0\n"
            self.assertIn("gdi " + hashlib.sha256(descriptor).hexdigest(), lines)


if __name__ == "__main__":
    unittest.main()
