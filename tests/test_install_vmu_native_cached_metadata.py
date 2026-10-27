#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The combined CLI keeps the proven publication and restore machinery."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
import install_vmu_native_cached_metadata as installer
import test_install_vmu_native_isolation as fixtures


class CachedMetadataInstallTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.IsolationInstallTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.package, self.card = self.fixture.package, self.fixture.card
        self.metadata = self.fixture.metadata
        self.metadata.update(cached_metadata=True, public_variant='combined')
        for name, entry in self.metadata['variants'].items():
            combined = name == 'reader-only'
            entry.update(cached_metadata=combined, public_variant='combined' if combined else name,
                         virtual_a2=True, reader='New (X)' if combined else 'Standard',
                         maple_c_callbacks_enabled=True, controller_tick_off=combined,
                         prefetch_enabled=False, e1_wait_capture=False, maple_bypass=False,
                         maple_delay=False, support_hooks_retained=True,
                         heap_reclaim_coherence_guards_retained=True)
            self.write_layout(name)
        self.write_metadata()
        shutil.copyfile(ROOT/'tools/install_vmu_native_cached_metadata.py', self.package/'install.py')
        shutil.copyfile(ROOT/'tools/install_vmu_native_isolation.py',
                        self.package/'tools/install_vmu_native_isolation.py')

    def write_metadata(self):
        self.fixture.write_metadata()

    def write_layout(self, name):
        entry = self.metadata['variants'][name]
        layout = {key: value for key, value in entry.items()
                  if key not in ('directory', 'destination', 'linked_layout_sha256', 'reader')}
        layout['variant'] = name
        raw = (json.dumps(layout, indent=2)+'\n').encode()
        (self.package/entry['directory']/'variant-layout.json').write_bytes(raw)
        entry['linked_layout_sha256'] = hashlib.sha256(raw).hexdigest()

    def run_variant(self, variant):
        return installer.install(self.card, self.package, variant,
                                 self.fixture.profile.relative_to(self.card))

    def cli(self, variant):
        return subprocess.run([sys.executable, str(self.package/'install.py'), str(self.card),
            '--profile', str(self.fixture.profile.relative_to(self.card)), '--variant', variant],
            cwd=self.fixture.root, text=True, capture_output=True)

    def test_combined_switch_and_exact_restore_preserve_prepared_card(self):
        result = self.run_variant('combined')
        self.assertEqual(result['variant'], 'combined')
        self.assertIs(result['cached_metadata'], True)
        self.assertIs(result['virtual_a2'], True)
        self.assertIs(result['maple_c_callbacks_enabled'], True)
        self.assertIs(result['controller_tick_off'], True)
        state = json.loads((self.card/'KUI/native-isolation/state.json').read_text())
        self.assertEqual(state['variant'], 'reader-only')
        self.assertTrue(self.run_variant('combined')['already_installed'])
        self.assertIs(self.run_variant('vmu-only')['cached_metadata'], False)
        result = self.run_variant('restore')
        self.assertIs(result['restored_exact_original_modules'], True)
        self.assertIsNone(result['virtual_a2'])
        for path in installer.MODULES.values():
            self.assertEqual((self.card/path).read_bytes(), self.fixture.original[str(path)])
        self.fixture.assert_protected()

    def test_untyped_or_wrong_mode_metadata_refuses_before_card_writes(self):
        before = self.fixture.snapshot()
        expected = dict(cached_metadata=True, virtual_a2=True, maple_c_callbacks_enabled=True,
                        controller_tick_off=True, prefetch_enabled=False, e1_wait_capture=False,
                        maple_bypass=False, maple_delay=False, support_hooks_retained=True)
        entry = self.metadata['variants']['reader-only']
        for field, correct in expected.items():
            for wrong in (not correct, int(correct), None):
                with self.subTest(field=field, value=wrong):
                    entry[field] = wrong
                    self.write_layout('reader-only')
                    self.write_metadata()
                    with self.assertRaisesRegex(ValueError, 'capabilities differ'):
                        self.run_variant('combined')
                    self.assertEqual(self.fixture.snapshot(), before)
            entry[field] = correct
        self.write_layout('reader-only')
        self.metadata['cached_metadata'] = 1
        self.write_metadata()
        with self.assertRaisesRegex(ValueError, 'requires the combined'):
            self.run_variant('combined')
        self.assertEqual(self.fixture.snapshot(), before)

    def test_layout_digest_and_typed_module_binding_are_required(self):
        before = self.fixture.snapshot()
        path = self.package/'variants/reader-only/variant-layout.json'
        original = path.read_bytes()
        path.write_bytes(original+b' ')
        with self.assertRaisesRegex(ValueError, 'linked module binding'):
            self.run_variant('combined')
        self.assertEqual(self.fixture.snapshot(), before)
        layout = json.loads(original)
        for change in ({'module_sha256': '0'*64}, {'virtual_a2': 1},
                       {'maple_c_callbacks_enabled': False}, {'controller_tick_off': False},
                       {'public_variant': 'reader-only'}):
            with self.subTest(change=change):
                raw = json.dumps(dict(layout, **change)).encode()
                path.write_bytes(raw)
                self.metadata['variants']['reader-only']['linked_layout_sha256'] = hashlib.sha256(raw).hexdigest()
                self.write_metadata()
                with self.assertRaisesRegex(ValueError, 'linked module binding'):
                    self.run_variant('combined')
                self.assertEqual(self.fixture.snapshot(), before)

    def test_public_cli_reports_combined_and_rejects_internal_name(self):
        result = self.cli('combined')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"variant": "combined"', result.stdout)
        self.assertIn('Virtual A2 is ON', result.stdout)
        self.assertIn('Real Maple C callbacks are enabled', result.stdout)
        self.assertIn('Controller TICK and read-ahead are OFF', result.stdout)
        self.assertNotIn('reader-only', result.stdout)
        before = self.fixture.snapshot()
        result = self.cli('reader-only')
        self.assertEqual(result.returncode, 2)
        self.assertIn('invalid choice', result.stderr)
        self.assertEqual(self.fixture.snapshot(), before)
        self.assertEqual(self.cli('restore').returncode, 0)
        self.fixture.assert_protected()


if __name__ == '__main__':
    unittest.main()
