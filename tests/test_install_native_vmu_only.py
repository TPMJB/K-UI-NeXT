#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Single-loader VMU-only installs preserve current GUI, modules and saves."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
import install_native_vmu_only as installer
import personalize_vmu_native_persistent as personalizer
from runtime_package import envelope
from test_personalize_vmu_native_persistent import generic, pair, COMMIT


def module_bytes(payload, build):
    payload = payload.ljust((len(payload)+3)&~3, b' ')
    return envelope(payload, len(payload), build)


class VmuOnlyInstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.package, self.card = self.root/'package', self.root/'card'
        self.card.mkdir()
        self.metadata = dict(schema=installer.SCHEMA, build=COMMIT[:12], source_commit=COMMIT,
                             lease_bytes=installer.LEASE_BYTES, variants={})
        for name, transport in installer.TRANSPORTS.items():
            variant = self.package/'variants'/name
            old, manifest = generic(variant)
            payload = old[64:]+name.encode().ljust(16, b' ')
            module = envelope(payload, len(payload), COMMIT[:12])
            manifest.update(module_bytes=len(module), module_sha256=personalizer.sha(module))
            (variant/personalizer.BASE_MODULE).write_bytes(module)
            (variant/personalizer.MANIFEST).write_text(json.dumps(manifest))
            self.metadata['variants'][name] = dict(directory='variants/'+name,
                transport=transport, module_sha256=personalizer.sha(module))
        tools = self.package/'tools'
        tools.mkdir()
        for name in ('runtime_package.py', 'vmu_persist_profile.py'):
            shutil.copyfile(ROOT/'tools'/name, tools/name)
        shutil.copyfile(ROOT/'tools/personalize_vmu_native_persistent.py', self.package/'personalize.py')
        shutil.copyfile(ROOT/'tools/install_native_vmu_only.py', self.package/'install.py')
        self.write_metadata()
        self.profile = pair(self.card, 'p0005', number='0005')
        for relative, payload in (
                (installer.DESTINATION, b'original ordinary loader'),
                (Path('KUI/apps/games/retail-native-dual.kui'), b'current background loader'),
                (Path('KUI/runtime.kui'), b'latest recovery dashboard GUI')):
            (self.card/relative).parent.mkdir(parents=True, exist_ok=True)
            (self.card/relative).write_bytes(module_bytes(payload, '123456789abc'))
        (self.card/'KUI/apps/games/launch-settings-a.bin').write_bytes(b'unchanged reader choices')
        (self.card/'KUI/vmu-assignments.json').write_bytes(b'unchanged assignments')
        self.original = self.snapshot()

    def write_metadata(self):
        (self.package/'vmu-only.json').write_text(json.dumps(self.metadata))

    def snapshot(self):
        return {str(p.relative_to(self.card)): p.read_bytes() for p in self.card.rglob('*') if p.is_file()}

    def run_variant(self, transport=None, restore=False, profile=None):
        return installer.install(self.card, self.package, transport,
                                 profile or self.profile.relative_to(self.card), restore)

    def assert_protected(self):
        after = self.snapshot()
        for name, data in self.original.items():
            if name != str(installer.DESTINATION):
                self.assertEqual(after[name], data, name)

    def test_sci_scif_switch_and_exact_restore(self):
        self.run_variant('sci')
        first = (self.card/installer.DESTINATION).read_bytes()
        self.assertNotEqual(first, self.original[str(installer.DESTINATION)])
        self.run_variant('scif')
        self.assertNotEqual((self.card/installer.DESTINATION).read_bytes(), first)
        self.run_variant('sci')
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), first)
        self.run_variant(restore=True)
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), self.original[str(installer.DESTINATION)])
        self.assert_protected()

    def test_repeated_install_and_restore_are_no_write(self):
        for transport, restore in (('sci', False), ('scif', False), (None, True)):
            self.run_variant(transport, restore)
            with patch.object(installer, 'publish', side_effect=AssertionError('Unexpected write')):
                self.assertTrue(self.run_variant(transport, restore)['already_installed'])

    def test_gui_updates_are_preserved_without_obsolete_hash_pin(self):
        self.run_variant('sci')
        gui = self.card/'KUI/runtime.kui'
        updated = module_bytes(b'newer recovery GUI', '987654321abc')
        gui.write_bytes(updated)
        self.run_variant('scif')
        self.run_variant(restore=True)
        self.assertEqual(gui.read_bytes(), updated)

    def test_no_profile_needed_for_restore(self):
        self.run_variant('sci')
        self.profile.unlink()
        installer.install(self.card, self.package, restore=True)
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), self.original[str(installer.DESTINATION)])

    def test_changed_package_refuses_before_backup(self):
        path = self.package/'variants/sci'/personalizer.BASE_MODULE
        data = bytearray(path.read_bytes());data[-1] ^= 1;path.write_bytes(data)
        with self.assertRaises(ValueError):
            self.run_variant('sci')
        self.assertEqual(self.snapshot(), self.original)
        self.assertFalse((self.card/installer.PARENT).exists())

    def test_wrong_profile_refuses_before_backup(self):
        (self.profile.parent/'identity.bin').write_bytes(b'X'*16)
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'identity'):
            self.run_variant('sci')
        self.assertEqual(self.snapshot(), before)
        self.assertFalse((self.card/installer.PARENT).exists())

    def test_multiple_prepared_cards_require_selection(self):
        pair(self.card, 'p0006', number='0006')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Several prepared'):
            installer.install(self.card, self.package, 'sci')
        self.assertEqual(self.snapshot(), before)

    def test_explicit_profile_can_switch_cards_without_touching_either(self):
        second = pair(self.card, 'p0006', number='0006')
        before = {str(p.relative_to(self.card)): p.read_bytes()
                  for p in (self.card/'KUI/vmu-test').rglob('*') if p.is_file()}
        self.run_variant('sci')
        self.run_variant('sci', profile=second.relative_to(self.card))
        self.assertEqual(before, {str(p.relative_to(self.card)): p.read_bytes()
                  for p in (self.card/'KUI/vmu-test').rglob('*') if p.is_file()})
        self.run_variant(restore=True)

    def test_corrupt_backup_refuses_without_mutation(self):
        self.run_variant('sci')
        (self.card/installer.BACKUP/'original.kui').write_bytes(b'bad backup')
        before = self.snapshot()
        with self.assertRaises(ValueError):
            self.run_variant(restore=True)
        self.assertEqual(self.snapshot(), before)

    def test_external_valid_loader_update_is_not_overwritten(self):
        self.run_variant('sci')
        (self.card/installer.DESTINATION).write_bytes(module_bytes(b'new external module', 'abcdefabcdef'))
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'outside this test'):
            self.run_variant(restore=True)
        self.assertEqual(self.snapshot(), before)

    def test_receipt_cannot_authorize_unrelated_code_by_hash(self):
        self.run_variant('sci')
        other = module_bytes(b'changed code', COMMIT[:12])
        (self.card/installer.DESTINATION).write_bytes(other)
        path = self.card/installer.STATE
        state = json.loads(path.read_text());state['allowed'] = [installer.sha(other)]
        path.write_text(json.dumps(state))
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'outside this test'):
            self.run_variant(restore=True)
        self.assertEqual(self.snapshot(), before)

    def test_symlink_destination_refuses(self):
        target = self.card/installer.DESTINATION
        other = self.root/'other';other.write_bytes(target.read_bytes())
        target.unlink();target.symlink_to(other)
        with self.assertRaisesRegex(ValueError, 'ordinary'):
            self.run_variant('sci')
        self.assertEqual(other.read_bytes(), self.original[str(installer.DESTINATION)])
        self.assertFalse((self.card/installer.PARENT).exists())

    def test_symlink_backup_parent_refuses_before_any_write(self):
        other = self.root/'other';other.mkdir()
        (self.card/installer.PARENT).symlink_to(other, target_is_directory=True)
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'ordinary'):
            self.run_variant('sci')
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(list(other.iterdir()), [])

    def test_receipt_without_backup_refuses(self):
        parent = self.card/installer.PARENT;parent.mkdir()
        (self.card/installer.STATE).write_text('{}')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'no exact loader backup'):
            self.run_variant('sci')
        self.assertEqual(self.snapshot(), before)

    def test_pending_receipt_recovers_before_or_after_publication(self):
        self.run_variant('sci')
        sci = (self.card/installer.DESTINATION).read_bytes()
        self.run_variant('scif')
        scif = (self.card/installer.DESTINATION).read_bytes()
        state = json.loads((self.card/installer.STATE).read_text())
        state.update(pending=True, allowed=[installer.sha(sci), installer.sha(scif)])
        for current in (sci, scif):
            (self.card/installer.DESTINATION).write_bytes(current)
            (self.card/installer.STATE).write_text(json.dumps(state))
            self.run_variant('sci')
            self.assertEqual((self.card/installer.DESTINATION).read_bytes(), sci)
            self.assertFalse(json.loads((self.card/installer.STATE).read_text())['pending'])
        self.assert_protected()

    def test_postrename_failure_rolls_back_invocation_bytes(self):
        self.run_variant('sci')
        before = self.snapshot()
        publish = installer.publish
        injected = False
        def fail_once(path, data):
            nonlocal injected
            publish(path, data)
            if path == self.card/installer.DESTINATION and not injected:
                injected = True
                raise OSError('injected after rename')
        with patch.object(installer, 'publish', side_effect=fail_once):
            with self.assertRaisesRegex(OSError, 'injected'):
                self.run_variant('scif')
        self.assertEqual(self.snapshot(), before)
        self.run_variant(restore=True)

    def test_interruption_after_backup_before_receipt_is_recoverable(self):
        publish = installer.publish
        def stop_before_receipt(path, data):
            if path == self.card/installer.STATE:
                raise OSError('interrupted before receipt')
            publish(path, data)
        with patch.object(installer, 'publish', side_effect=stop_before_receipt):
            with self.assertRaisesRegex(OSError, 'interrupted'):
                self.run_variant('sci')
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), self.original[str(installer.DESTINATION)])
        self.run_variant('sci')
        self.run_variant(restore=True)
        self.assert_protected()

    def test_invalid_variant_metadata_refuses_before_backup(self):
        self.metadata['variants']['scif']['transport'] = 1
        self.write_metadata()
        with self.assertRaisesRegex(ValueError, 'variant'):
            self.run_variant('sci')
        self.assertEqual(self.snapshot(), self.original)

    def test_real_cli_in_extracted_package_installs_switches_and_restores(self):
        for arguments in (('--transport', 'sci'), ('--transport', 'scif'), ('--restore',)):
            result = subprocess.run([sys.executable, str(self.package/'install.py'), str(self.card),
                                     *arguments, '--profile', str(self.profile.relative_to(self.card))],
                                    cwd=self.root, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Safely eject', result.stdout)
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), self.original[str(installer.DESTINATION)])
        self.assert_protected()


if __name__ == '__main__':
    unittest.main()
