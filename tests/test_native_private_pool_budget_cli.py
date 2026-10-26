#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise refusal, source preservation and exact external capacity evidence."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'tools/prove_native_private_pool_budget.py'
EXTERNAL = os.environ.get('KUI_NATIVE_LODOSS_STARTUP')


class PrivatePoolCLI(unittest.TestCase):
    def invoke(self, *arguments, optimized=False):
        return subprocess.run([sys.executable, *(['-O'] if optimized else []),
                               str(TOOL), *map(str, arguments)],
                              capture_output=True, text=True, check=False)

    def test_help_requires_no_source_or_compiler(self):
        result = self.invoke('--help')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('--image', result.stdout)

    def test_unknown_input_refused_without_output_or_modification(self):
        with tempfile.TemporaryDirectory() as directory:
            image, output = Path(directory) / 'original.bin', Path(directory) / 'proof.json'
            before = b'Unrelated owner input remains unchanged.'
            image.write_bytes(before)
            result = self.invoke('--image', image, '--output', output)
            self.assertEqual(result.returncode, 2)
            self.assertIn('exact external', result.stderr)
            self.assertFalse(output.exists())
            self.assertEqual(image.read_bytes(), before)

    def test_original_cannot_be_selected_as_output(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / 'original.bin'
            before = b'Original source must not become a JSON report.'
            image.write_bytes(before)
            result = self.invoke('--image', image, '--output', image)
            self.assertEqual(result.returncode, 2)
            self.assertIn('must differ', result.stderr)
            self.assertEqual(image.read_bytes(), before)

    def test_assertion_free_execution_refused(self):
        result = self.invoke('--image', 'not-opened.bin', optimized=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('assertions enabled', result.stderr)

    @unittest.skipUnless(EXTERNAL, 'Supply KUI_NATIVE_LODOSS_STARTUP for original-instruction evidence')
    def test_original_four_variants_and_compiled_plan_binding(self):
        image = Path(EXTERNAL)
        before = hashlib.sha256(image.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'proof.json'
            result = self.invoke('--image', image, '--output', output)
            self.assertEqual(result.returncode, 0, result.stderr)
            proof = json.loads(output.read_bytes())
            self.assertEqual(proof['status'], 'PASS')
            self.assertEqual(len(proof['cases']), 4)
            self.assertEqual(len(proof['refusal_cases']), 10)
            self.assertEqual([case['scene_allocator_return'] != 0 for case in proof['cases']],
                             [True, False, False, True])
            self.assertTrue(all(case['worker_payload_preserved'] and
                                case['worker_header_preserved'] for case in proof['cases'] if case['lease'] is not None))
            self.assertEqual(hashlib.sha256(image.read_bytes()).hexdigest(), before)
            specification = importlib.util.spec_from_file_location('private_pool_proof', TOOL)
            module = importlib.util.module_from_spec(specification)
            specification.loader.exec_module(module)
            altered = dict(proof['plan'])
            altered['reserve_after'] += 32
            with self.assertRaisesRegex(AssertionError, 'compiled budget plan differs'):
                module.prove(image, expected_plan=altered)
            self.assertEqual(hashlib.sha256(image.read_bytes()).hexdigest(), before)


if __name__ == '__main__':
    unittest.main()
