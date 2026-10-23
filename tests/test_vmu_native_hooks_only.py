#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual linked no-card completion and immutable-mode/config assignment."""
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from check_vmu_native_persistent import (
    AbsentCompletion, audit_absent_completion, check_worker_publication,
)
from check_vmu_native_worker_gateway import read_elf, symbol_bytes


class HooksOnlyArtifact(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        directory = Path(os.environ.get('KUI_NATIVE_ARTIFACT_DIR', ROOT / 'build/evolution-hooks-dev'))
        if not (directory / 'native-worker.elf').exists():
            raise unittest.SkipTest('build hooks-only worker or set KUI_NATIVE_ARTIFACT_DIR')
        cls.worker = read_elf(directory / 'native-worker.elf')
        if AbsentCompletion.helper not in cls.worker[4]:
            raise unittest.SkipTest('artifact predates hooks-only completion')
        cls.stage = read_elf(directory / 'stage.elf')
        objdump = ROOT / '.deps/sh-elf/bin/sh-elf-objdump'
        cls.disassembly = subprocess.check_output([str(objdump), '-d', str(directory / 'stage.elf')], text=True)

    def test_actual_helper_selector_and_negative_mutations(self):
        report = audit_absent_completion(self.worker, self_test=True)
        self.assertEqual(report['helper_instruction_cases'], 240)
        self.assertEqual(report['selector_instruction_cases'], 64)
        self.assertGreaterEqual(report['mutation_rejections'], 4)
        self.assertTrue(report['actual_callees_only'])
        self.assertTrue(report['no_backend_or_sd_call'])
        self.assertTrue(report['no_parent_advertisement'])
        self.assertTrue(report['only_filtered_a1_four_byte_headers_changed'])

    def test_all_32_headers_and_saturated_counters(self):
        for command in (1, 2, 9):
            AbsentCompletion(self.worker, 1, command, 32, 31, 'ok').run().check()
            AbsentCompletion(self.worker, 1, command, 32, 31, 'conflict').run().check()

    def test_actual_getter_return_store_dominates_first_arm(self):
        report = check_worker_publication(self.stage, True, self.disassembly, self_test=True)
        self.assertTrue(report['runtime_diagnostics_return_stored_in_exact_config'])
        self.assertTrue(report['runtime_diagnostics_store_dominates_arm'])
        self.assertGreaterEqual(report['initialization_witness_mutation_rejections'], 9)

    def test_new_helper_cannot_escape_its_exact_integer_callees(self):
        import struct
        _, body, _ = symbol_bytes(self.worker, AbsentCompletion.helper, 2)
        old = struct.pack('<I', self.worker[4]['_readonly_overlay_le32'][0])
        new = struct.pack('<I', self.worker[4]['_kui_vmu_native_persistent_sd_transaction'][0])
        self.assertEqual(body.count(old), 1)
        with self.assertRaises((ValueError, AssertionError, KeyError)):
            AbsentCompletion(self.worker, 0, 1, 2, 1, 'ok', body=body.replace(old, new)).run().check()


if __name__ == '__main__':
    unittest.main()
