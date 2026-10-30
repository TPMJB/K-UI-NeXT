#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent GUI-only update preserves loaders, cards and launch settings."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
import install_skies_launch_ui as installer
import test_install_native_vmu_only as base_tests
from test_install_native_vmu_only import module_bytes
from test_personalize_vmu_native_persistent import COMMIT


class SkiesLaunchUiInstall(unittest.TestCase):
    snapshot = base_tests.VmuOnlyInstallTests.snapshot
    write_metadata = base_tests.VmuOnlyInstallTests.write_metadata

    def setUp(self):
        base_tests.VmuOnlyInstallTests.setUp(self)
        shutil.copyfile(ROOT/'tools/install_native_vmu_only.py', self.package/'tools/install_native_vmu_only.py')
        shutil.copyfile(ROOT/'tools/install_skies_launch_ui.py', self.package/'install-ui.py')
        (self.package/'gui').mkdir()
        self.candidate = module_bytes(b'new launch UI fused boot CRC validation', COMMIT[:12])
        (self.package/'gui/runtime.kui').write_bytes(self.candidate)
        core = installer.load_core(self.package)
        self.gui = dict(schema=installer.SCHEMA, source_commit=COMMIT, build=COMMIT[:12],
                        module_sha256=core.sha(self.candidate))
        (self.package/'gui.json').write_text(json.dumps(self.gui))
        old = self.card/'KUI/native-vmu-only';old.mkdir()
        (old/'state.json').write_bytes(b'old native receipt unchanged')
        self.original = self.snapshot()

    def run_install(self, restore=False):
        return installer.install(self.card, self.package, restore)

    def protected(self):
        after = self.snapshot()
        for name, data in self.original.items():
            if name != str(installer.DESTINATION):
                self.assertEqual(after[name], data, name)

    def test_only_gui_changes_and_exact_original_restores(self):
        self.run_install()
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), self.candidate)
        self.protected()
        self.run_install(True)
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), self.original[str(installer.DESTINATION)])
        self.protected()

    def test_repeat_install_and_restore_no_write(self):
        for restore in (False, True):
            self.run_install(restore)
            core = installer.load_core(self.package)
            with patch.object(installer, 'load_core', return_value=core), \
                 patch.object(core, 'publish', side_effect=AssertionError('unexpected write')):
                self.assertTrue(self.run_install(restore)['already_installed'])

    def test_external_runtime_update_refuses_without_touching_files(self):
        self.run_install()
        (self.card/installer.DESTINATION).write_bytes(module_bytes(b'new recovery GUI from another thread', 'abcdefabcdef'))
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'outside this UI test'):
            self.run_install(True)
        self.assertEqual(self.snapshot(), before)
        self.protected()

    def test_changed_candidate_refuses_before_backup(self):
        (self.package/'gui/runtime.kui').write_bytes(module_bytes(b'altered valid GUI', COMMIT[:12]))
        with self.assertRaisesRegex(ValueError, 'checksum'):
            self.run_install()
        self.assertEqual(self.snapshot(), self.original)
        self.assertFalse((self.card/installer.PARENT).exists())

    def test_wrong_source_build_refuses_before_backup(self):
        self.gui['build'] = '123456789abc'
        (self.package/'gui.json').write_text(json.dumps(self.gui))
        with self.assertRaisesRegex(ValueError, 'manifest'):
            self.run_install()
        self.assertEqual(self.snapshot(), self.original)

    def test_failed_publication_after_rename_rolls_back_exact_previous_gui(self):
        core = installer.load_core(self.package);publish = core.publish;injected = False
        def fail_once(path, data):
            nonlocal injected
            publish(path, data)
            if path == self.card/installer.DESTINATION and not injected:
                injected = True
                raise OSError('failure after GUI rename')
        with patch.object(installer, 'load_core', return_value=core), patch.object(core, 'publish', side_effect=fail_once):
            with self.assertRaisesRegex(OSError, 'GUI rename'):
                self.run_install()
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), self.original[str(installer.DESTINATION)])
        self.protected()
        self.run_install();self.run_install(True)

    def test_runtime_drift_during_prepublication_is_preserved(self):
        core = installer.load_core(self.package);publish = core.publish;injected = False
        other = module_bytes(b'external GUI update during installation', 'abcdefabcdef')
        def drift_once(path, data):
            nonlocal injected
            publish(path, data)
            if path == self.card/installer.STATE and not injected:
                injected = True
                (self.card/installer.DESTINATION).write_bytes(other)
        with patch.object(installer, 'load_core', return_value=core), patch.object(core, 'publish', side_effect=drift_once):
            with self.assertRaisesRegex(ValueError, 'before publication'):
                self.run_install()
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), other)
        self.protected()

    def test_pending_state_recovers_original_or_candidate(self):
        self.run_install()
        core = installer.load_core(self.package)
        original = self.original[str(installer.DESTINATION)]
        state = json.loads((self.card/installer.STATE).read_text())
        state.update(pending=True, allowed=[core.sha(original), core.sha(self.candidate)])
        for current in (original, self.candidate):
            (self.card/installer.DESTINATION).write_bytes(current)
            (self.card/installer.STATE).write_text(json.dumps(state))
            self.run_install(True)
            self.assertEqual((self.card/installer.DESTINATION).read_bytes(), original)

    def test_symlink_runtime_or_backup_parent_refuses_before_writes(self):
        path = self.card/installer.DESTINATION
        outside = self.root/'outside';outside.write_bytes(path.read_bytes())
        path.unlink();path.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'ordinary'):
            self.run_install()
        path.unlink();path.write_bytes(outside.read_bytes())
        parent = self.root/'external-dir';parent.mkdir()
        (self.card/installer.PARENT).symlink_to(parent, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'ordinary'):
            self.run_install()
        self.assertEqual(list(parent.iterdir()), [])

    def test_real_extracted_cli_installs_and_restores_gui_only(self):
        for arguments in ((), ('--restore',)):
            run = subprocess.run([sys.executable, str(self.package/'install-ui.py'), str(self.card), *arguments],
                                 cwd=self.root, text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn('Safely eject', run.stdout)
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), self.original[str(installer.DESTINATION)])
        self.protected()


if __name__ == '__main__':
    unittest.main()
