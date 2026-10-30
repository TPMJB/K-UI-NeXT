#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Skies mode switches retain the previous test's loader and receipt bytes."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
import install_skies_vmu_compare as installer
import install_native_vmu_only as original_core
import personalize_vmu_native_persistent as personalizer
import test_install_native_vmu_only as base_tests
from test_install_native_vmu_only import module_bytes


class SkiesCompareInstall(unittest.TestCase):
    snapshot = base_tests.VmuOnlyInstallTests.snapshot
    write_metadata = base_tests.VmuOnlyInstallTests.write_metadata
    assert_protected = base_tests.VmuOnlyInstallTests.assert_protected

    def setUp(self):
        base_tests.VmuOnlyInstallTests.setUp(self)
        for old, new in (('sci', 'baseline'), ('scif', 'full-submit')):
            (self.package/'variants'/old).rename(self.package/'variants'/new)
            entry = self.metadata['variants'].pop(old)
            entry.update(directory='variants/'+new, transport=1)
            self.metadata['variants'][new] = entry
        self.metadata['schema'] = installer.SCHEMA
        self.write_metadata()
        shutil.copyfile(ROOT/'tools/install_native_vmu_only.py', self.package/'tools/install_native_vmu_only.py')
        shutil.copyfile(ROOT/'tools/install_skies_vmu_compare.py', self.package/'install.py')
        (self.card/installer.DESTINATION).write_bytes(module_bytes(b'current accepted 4edbc SCI VMU-only', '4edbc5600000'))
        legacy = self.card/'KUI/native-vmu-only/backup';legacy.mkdir(parents=True)
        (legacy/'original.kui').write_bytes(b'exact pre-4edbc original')
        (legacy/'manifest.json').write_text('{"preserve":"old backup metadata"}')
        (legacy.parent/'state.json').write_text('{"preserve":"old test state"}')
        self.original = self.snapshot()

    def run_mode(self, mode=None, restore=False):
        return installer.install(self.card, self.package, mode,
                                 self.profile.relative_to(self.card), restore)

    def test_modes_share_profile_and_restore_exact_4edbc(self):
        previous = self.original[str(installer.DESTINATION)]
        first = self.run_mode('baseline')
        baseline = (self.card/installer.DESTINATION).read_bytes()
        second = self.run_mode('full-submit')
        full = (self.card/installer.DESTINATION).read_bytes()
        self.assertNotEqual(baseline, full)
        self.assertEqual((first['profile'], first['pair_path']), (second['profile'], second['pair_path']))
        for name, data in (('baseline', baseline), ('full-submit', full)):
            manifest = json.loads((self.package/'variants'/name/personalizer.MANIFEST).read_text())
            at = manifest['profile_module_offset']
            self.assertEqual(data[at:at+4096], self.profile.read_bytes())
        self.run_mode('baseline')
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), baseline)
        self.run_mode(restore=True)
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), previous)
        self.assert_protected()
        self.assertEqual(original_core.TRANSPORTS, {'sci': 1, 'scif': 0})
        self.assertEqual(original_core.PARENT, Path('KUI/native-vmu-only'))

    def test_repeats_are_no_write(self):
        for mode, restore in (('baseline', False), ('full-submit', False), (None, True)):
            self.run_mode(mode, restore)
            core = installer.load_core(self.package)
            with patch.object(installer, 'load_core', return_value=core), \
                 patch.object(core, 'publish', side_effect=AssertionError('unexpected write')):
                self.assertTrue(self.run_mode(mode, restore)['already_installed'])

    def test_changed_destination_refuses_and_retains_old_namespace(self):
        self.run_mode('baseline')
        (self.card/installer.DESTINATION).write_bytes(module_bytes(b'external loader update', '123456789abc'))
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'outside this test'):
            self.run_mode('full-submit')
        self.assertEqual(self.snapshot(), before)
        self.assert_protected()

    def test_profile_change_during_preflight_refuses_before_backup(self):
        core = installer.load_core(self.package)
        p = core.load_personalizer(self.package)
        select = p.select_profile
        def changed_profile(*args, **kwargs):
            selected = select(*args, **kwargs)
            self.profile.write_bytes(selected['wire'][:-1]+bytes([selected['wire'][-1]^1]))
            return selected
        with patch.object(installer, 'load_core', return_value=core), \
             patch.object(core, 'load_personalizer', return_value=p), \
             patch.object(p, 'select_profile', side_effect=changed_profile):
            with self.assertRaisesRegex(ValueError, 'changed during preflight'):
                self.run_mode('baseline')
        self.assertFalse((self.card/installer.PARENT).exists())
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), self.original[str(installer.DESTINATION)])
        self.assertEqual((self.card/'KUI/native-vmu-only/state.json').read_bytes(),
                         self.original['KUI/native-vmu-only/state.json'])

    def test_profile_path_swap_after_pending_receipt_refuses_target_publication(self):
        core = installer.load_core(self.package);publish = core.publish;injected = False
        outside = self.root/'other-profile.bin';outside.write_bytes(self.profile.read_bytes())
        def swap_once(path, data):
            nonlocal injected
            publish(path, data)
            if path == self.card/installer.STATE and not injected:
                injected = True
                self.profile.unlink();self.profile.symlink_to(outside)
        with patch.object(installer, 'load_core', return_value=core), patch.object(core, 'publish', side_effect=swap_once):
            with self.assertRaisesRegex(ValueError, 'ordinary'):
                self.run_mode('baseline')
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), self.original[str(installer.DESTINATION)])
        self.assertEqual((self.card/'KUI/native-vmu-only/state.json').read_bytes(),
                         self.original['KUI/native-vmu-only/state.json'])
        self.run_mode(restore=True)

    def test_failure_after_destination_rename_rolls_back(self):
        self.run_mode('baseline');before = self.snapshot()
        core = installer.load_core(self.package);publish = core.publish;injected = False
        def fail_once(path, data):
            nonlocal injected
            publish(path, data)
            if path == self.card/installer.DESTINATION and not injected:
                injected = True
                raise OSError('failure after rename')
        with patch.object(installer, 'load_core', return_value=core), patch.object(core, 'publish', side_effect=fail_once):
            with self.assertRaisesRegex(OSError, 'after rename'):
                self.run_mode('full-submit')
        self.assertEqual(self.snapshot(), before)
        self.run_mode(restore=True)

    def test_pending_state_recovers_either_exact_mode(self):
        self.run_mode('baseline');baseline = (self.card/installer.DESTINATION).read_bytes()
        self.run_mode('full-submit');full = (self.card/installer.DESTINATION).read_bytes()
        core = installer.load_core(self.package)
        state = json.loads((self.card/installer.STATE).read_text())
        state.update(pending=True, allowed=[core.sha(baseline), core.sha(full)])
        for current in (baseline, full):
            (self.card/installer.DESTINATION).write_bytes(current)
            (self.card/installer.STATE).write_text(json.dumps(state))
            self.run_mode('baseline')
            self.assertEqual((self.card/installer.DESTINATION).read_bytes(), baseline)
            self.assertFalse(json.loads((self.card/installer.STATE).read_text())['pending'])

    def test_symlink_destination_refused_before_new_namespace(self):
        path = self.card/installer.DESTINATION
        outside = self.root/'outside';outside.write_bytes(path.read_bytes())
        path.unlink();path.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'ordinary'):
            self.run_mode('baseline')
        self.assertFalse((self.card/installer.PARENT).exists())

    def test_real_cli_extract_switch_restore_and_old_state_preserved(self):
        for arguments in (('--mode', 'baseline'), ('--mode', 'full-submit'), ('--restore',)):
            run = subprocess.run([sys.executable, str(self.package/'install.py'), str(self.card),
                                  *arguments, '--profile', str(self.profile.relative_to(self.card))],
                                 cwd=self.root, text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn('Safely eject', run.stdout)
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), self.original[str(installer.DESTINATION)])
        self.assert_protected()


if __name__ == '__main__':
    unittest.main()
