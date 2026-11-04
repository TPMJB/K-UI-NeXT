#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Withdrawn release entrypoints must stop before any build or output work."""
from contextlib import ExitStack
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import package_update_test
import update_release
from update_release_safety import RELEASE_BLOCK_REASON


class WithdrawnReleaseTests(unittest.TestCase):
    def test_build_stops_before_source_build_or_output(self):
        with tempfile.TemporaryDirectory() as temporary, ExitStack() as stack:
            base = Path(temporary)
            for name in ("source_state", "owned_output", "build_base", "build_modules",
                         "build_legacy", "build_package"):
                stack.enter_context(patch.object(update_release, name,
                    side_effect=AssertionError("Reached release work: " + name)))
            with self.assertRaises(ValueError) as refused:
                update_release.build_release(base / "missing" / "release",
                    legacy_dir=base / "missing-legacy")
            self.assertEqual(str(refused.exception), RELEASE_BLOCK_REASON)
            self.assertEqual(list(base.iterdir()), [])

    def test_packet_stops_before_admission_or_output(self):
        with tempfile.TemporaryDirectory() as temporary, ExitStack() as stack:
            base = Path(temporary)
            for name in ("admitted_release", "verify", "digest"):
                stack.enter_context(patch.object(package_update_test, name,
                    side_effect=AssertionError("Reached packet work: " + name)))
            stack.enter_context(patch.object(package_update_test.subprocess, "run",
                side_effect=AssertionError("Ran a packet build or test")))
            with self.assertRaises(ValueError) as refused:
                package_update_test.package_test(base / "missing-a", base / "missing-b",
                    base / "missing" / "test.zip", legacy_sources=base / "missing-sources.json")
            self.assertEqual(str(refused.exception), RELEASE_BLOCK_REASON)
            self.assertEqual(list(base.iterdir()), [])

    def test_command_lines_report_block_without_creating_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            commands = (
                ("update_release.py", "--output", str(base / "release"),
                 "--legacy-dir", str(base / "missing-legacy")),
                ("package_update_test.py", "--baseline", str(base / "missing-a"),
                 "--candidate", str(base / "missing-b"), "--output", str(base / "test.zip")),
            )
            for arguments in commands:
                with self.subTest(tool=arguments[0]):
                    result = subprocess.run([sys.executable, "-B", str(ROOT / "tools" / arguments[0]),
                        *arguments[1:]], cwd=ROOT, text=True, capture_output=True, check=False)
                    self.assertEqual(result.returncode, 1)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr.strip(), RELEASE_BLOCK_REASON)
                    self.assertEqual(list(base.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
