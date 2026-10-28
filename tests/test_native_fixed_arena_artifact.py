#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The fixed-arena route of an actual linked dual stage.

Set KUI_NATIVE_DUAL_ARTIFACT_DIR to a dual build directory, for example the
BUILD of Makefile.vmu-native-split-controller-stream-guard.
"""
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_native_fixed_arena import check


class FixedArenaArtifact(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        directory = os.environ.get("KUI_NATIVE_DUAL_ARTIFACT_DIR")
        if not directory or not (Path(directory) / "stage.elf").exists():
            raise unittest.SkipTest("set KUI_NATIVE_DUAL_ARTIFACT_DIR to a dual build")
        cls.proof = check(directory, self_test=True)

    def test_arm_trampoline_and_dispatch(self):
        proof = self.proof
        self.assertTrue(proof["enabled"])
        # Both readers, three seeds: one accepted arm and 25 refusals each.
        self.assertEqual(proof["cases"], {"armed": 6, "refused": 150, "entered": 6})
        self.assertEqual(proof["relay_dispatch"], ["initializer", "fixed"])
        self.assertEqual(proof["trampoline"]["bytes"], 0x34)

    def test_mutants(self):
        self.assertGreaterEqual(self.proof["mutants_rejected"], 14)
        # Only the inlined trampoline-return test forced to its one reachable outcome.
        self.assertLessEqual(len(self.proof["equivalent_forced_outcomes"]), 1)


if __name__ == "__main__":
    unittest.main()
