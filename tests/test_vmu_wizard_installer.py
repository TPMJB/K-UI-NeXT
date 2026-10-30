#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real CLI install/restore and interrupted-publication recovery on a temp SD."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from runtime_package import envelope

spec = importlib.util.spec_from_file_location('wizard_installer', ROOT/'tools/install_vmu_wizard.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class WizardInstaller(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='kui-wizard-installer-')
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.card = self.work/'card'
        self.package = self.work/'package'
        self.runtime = envelope(b'old-ui!!', 4096, '1562c62b5330')
        self.new = envelope(b'new-ui!!', 8192, '0123456789ab')
        self.foreign = envelope(b'other-ui', 4096, 'abcdefabcdef')
        self.dual = envelope(b'dual-vmu', 4096, installer.REQUIRED_DUAL_BUILD)
        (self.card/'KUI/apps/games').mkdir(parents=True)
        (self.package/'gui').mkdir(parents=True)
        (self.card/installer.RUNTIME).write_bytes(self.runtime)
        (self.card/installer.DUAL).write_bytes(self.dual)
        (self.package/'gui/runtime.kui').write_bytes(self.new)
        (self.package/'wizard.json').write_bytes(installer.wire(dict(
            schema=installer.SCHEMA, build='0123456789ab', runtime_sha256=installer.sha(self.new),
            required_dual_build=installer.REQUIRED_DUAL_BUILD)))
        shutil.copyfile(ROOT/'tools/install_vmu_wizard.py', self.package/'install.py')
        for name in ('KUI/native-general/backup/gui.kui', 'KUI/native-general/state.json',
                     'KUI/vmu-test/p0005/card.vmd', 'KUI/vmu-test/p0005/card.journal',
                     'KUI/vmu-test/p0005/identity.bin',
                     'KUI/vmu-test/p0005/retail-persistent-profile-0005.bin',
                     'KUI/apps/games/launch-settings-save.bin', 'KUI/apps/games/retail-boot.kui',
                     'KUI/apps/games/retail-native-standalone.kui'):
            path = self.card/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('protected '+name).encode())
        self.protected = self.snapshot()

    def snapshot(self):
        return {str(path.relative_to(self.card)): path.read_bytes()
                for path in self.card.rglob('*') if path.is_file() and
                path.relative_to(self.card) != installer.RUNTIME and
                not str(path.relative_to(self.card)).startswith('KUI/vmu-wizard/')}

    def cli(self, *args, passed=True):
        result = subprocess.run([sys.executable, str(self.package/'install.py'), str(self.card), *args],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, passed, result.stdout+result.stderr)
        self.assertEqual(self.snapshot(), self.protected)
        return result

    def checkpoint(self):
        return self.card/installer.PARENT/'checkpoints/0123456789ab'

    def test_install_reinstall_restore_exact_previous_ui(self):
        installed = self.cli()
        self.assertIn('Home > VMU tools > Import wizard', installed.stdout)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.new)
        self.assertEqual((self.checkpoint()/'runtime-before.kui').read_bytes(), self.runtime)
        receipt = (self.checkpoint()/'state.json').read_bytes()
        self.assertIn('"changed": []', self.cli().stdout)
        self.assertEqual((self.checkpoint()/'state.json').read_bytes(), receipt)
        self.cli('--restore')
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.runtime)
        self.assertIn('"changed": []', self.cli('--restore').stdout)
        self.cli()
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.new)

    def test_foreign_valid_ui_is_backed_up_on_first_install(self):
        (self.card/installer.RUNTIME).write_bytes(self.foreign)
        self.cli()
        self.cli('--restore')
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.foreign)

    def test_foreign_later_update_is_never_overwritten(self):
        self.cli()
        (self.card/installer.RUNTIME).write_bytes(self.foreign)
        for args in ((), ('--restore',)):
            result = self.cli(*args, passed=False)
            self.assertIn('changed outside this wizard', result.stderr)
            self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.foreign)

    def test_corrupt_checkpoint_is_refused(self):
        self.cli()
        (self.checkpoint()/'runtime-before.kui').write_bytes(self.foreign)
        self.cli('--restore', passed=False)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.new)

    def test_missing_or_wrong_native_module_is_refused_before_publication(self):
        (self.card/installer.DUAL).write_bytes(self.foreign)
        self.protected = self.snapshot()
        self.cli(passed=False)
        self.assertFalse((self.card/installer.PARENT).exists())
        (self.card/installer.DUAL).unlink()
        self.protected = self.snapshot()
        self.cli(passed=False)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.runtime)

    def test_corrupt_package_is_refused(self):
        (self.package/'gui/runtime.kui').write_bytes(self.foreign)
        self.cli(passed=False)
        self.assertFalse((self.card/installer.PARENT).exists())

    def test_restore_without_checkpoint_is_refused(self):
        self.cli('--restore', passed=False)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.runtime)

    def test_symlink_runtime_is_refused_without_touching_external_file(self):
        outside = self.work/'outside.kui'
        outside.write_bytes(self.runtime)
        (self.card/installer.RUNTIME).unlink()
        (self.card/installer.RUNTIME).symlink_to(outside)
        self.cli(passed=False)
        self.assertEqual(outside.read_bytes(), self.runtime)
        self.assertFalse((self.card/installer.PARENT).exists())

    def test_symlink_parent_or_checkpoint_is_refused(self):
        self.cli()
        checkpoint = self.checkpoint()
        moved = self.work/'checkpoint'
        checkpoint.rename(moved)
        checkpoint.symlink_to(moved, target_is_directory=True)
        self.cli('--restore', passed=False)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.new)

    def test_symlink_card_root_is_refused(self):
        alias = self.work/'alias'
        alias.symlink_to(self.card, target_is_directory=True)
        with self.assertRaises(ValueError):
            installer.run(alias, self.package)
        self.assertFalse((self.card/installer.PARENT).exists())

    def test_interrupt_after_pending_receipt_recovers_from_old_runtime(self):
        real = installer.publish
        def interrupted(path, data, root):
            real(path, data, root)
            if path.name == 'state.json' and json.loads(data)['pending']:
                raise OSError('injected power loss after pending receipt')
        with mock.patch.object(installer, 'publish', interrupted):
            with self.assertRaises(OSError):
                installer.run(self.card, self.package)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.runtime)
        self.cli()
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.new)

    def test_interrupt_after_runtime_replace_restores_original(self):
        real = installer.publish
        def interrupted(path, data, root):
            real(path, data, root)
            if path == self.card/installer.RUNTIME:
                raise OSError('injected power loss after runtime replace')
        with mock.patch.object(installer, 'publish', interrupted):
            with self.assertRaises(OSError):
                installer.run(self.card, self.package)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.new)
        self.cli('--restore')
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.runtime)

    def test_interrupt_before_final_receipt_can_finish_install(self):
        real = installer.publish
        def interrupted(path, data, root):
            if path.name == 'state.json' and not json.loads(data)['pending']:
                raise OSError('injected final receipt failure')
            real(path, data, root)
        with mock.patch.object(installer, 'publish', interrupted):
            with self.assertRaises(OSError):
                installer.run(self.card, self.package)
        self.cli()
        self.assertFalse(json.loads((self.checkpoint()/'state.json').read_bytes())['pending'])

    def test_external_change_after_pending_receipt_is_preserved(self):
        real = installer.publish
        def changed(path, data, root):
            real(path, data, root)
            if path.name == 'state.json' and json.loads(data)['pending']:
                (self.card/installer.RUNTIME).write_bytes(self.foreign)
        with mock.patch.object(installer, 'publish', changed):
            with self.assertRaisesRegex(ValueError, 'changed before runtime replacement'):
                installer.run(self.card, self.package)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.foreign)
        self.cli('--restore', passed=False)

    def test_incomplete_checkpoint_does_not_replace_runtime(self):
        real = installer.publish
        def interrupted(path, data, root):
            real(path, data, root)
            if path.name == 'runtime-before.kui':
                raise OSError('injected checkpoint write failure')
        with mock.patch.object(installer, 'publish', interrupted):
            with self.assertRaises(OSError):
                installer.run(self.card, self.package)
        self.assertEqual((self.card/installer.RUNTIME).read_bytes(), self.runtime)
        self.assertFalse(self.checkpoint().exists())
        self.cli()


if __name__ == '__main__':
    unittest.main()
