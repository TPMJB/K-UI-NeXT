#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Variant routing, exact persistent recovery, and unchanged prepared saves."""
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
import install_vmu_native_isolation as installer
import personalize_vmu_native_persistent as personalizer
import vmu_native_mode as mode
from runtime_package import envelope
from test_personalize_vmu_native_persistent import generic, pair, COMMIT


class IsolationInstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.package, self.card = self.root/'package', self.root/'card'
        self.card.mkdir()
        self.metadata = dict(schema=installer.SCHEMA, build=COMMIT[:12], source_commit=COMMIT, variants={})
        for name, destination in installer.MODULES.items():
            variant = self.package/'variants'/name
            old, manifest = generic(variant)
            payload = old[64:]+mode.template_mode()+name.encode().ljust(16, b' ')
            module = envelope(payload, len(payload), COMMIT[:12])
            manifest.update(module_bytes=len(module), module_sha256=personalizer.sha(module),
                            runtime_mode_module_offset=len(old), runtime_mode_bytes=32,
                            runtime_mode_sha256=personalizer.sha(mode.template_mode()),
                            runtime_mode_choices=personalizer.MODE_CHOICES_BACKGROUND)
            (variant/personalizer.BASE_MODULE).write_bytes(module)
            (variant/personalizer.MANIFEST).write_text(json.dumps(manifest))
            self.metadata['variants'][name] = dict(directory='variants/'+name,
                destination=str(destination), module_sha256=personalizer.sha(module))
        tools = self.package/'tools'
        tools.mkdir()
        for name in ('runtime_package.py', 'vmu_persist_profile.py', 'vmu_native_mode.py',
                     'install_vmu_native_dual.py'):
            shutil.copyfile(ROOT/'tools'/name, tools/name)
        shutil.copyfile(ROOT/'tools/personalize_vmu_native_persistent.py', self.package/'personalize.py')
        shutil.copyfile(ROOT/'tools/install_vmu_native_isolation.py', self.package/'install.py')
        gui = envelope(b'\x09\x00'*64, 128, COMMIT[:12])
        (self.card/installer.GUI).parent.mkdir()
        (self.card/installer.GUI).write_bytes(gui)
        self.metadata['gui'] = dict(sha256=personalizer.sha(gui), build=COMMIT[:12])
        self.write_metadata()
        self.profile = pair(self.card, 'p0005', number='0005')
        for name, path in installer.MODULES.items():
            (self.card/path).parent.mkdir(parents=True, exist_ok=True)
            (self.card/path).write_bytes(('original '+name).encode())
        (self.card/'KUI/apps/games/launch-settings-a.bin').write_bytes(b'unchanged saved reader choices')
        self.original = self.snapshot()

    def write_metadata(self):
        (self.package/'isolation.json').write_text(json.dumps(self.metadata))

    def snapshot(self):
        return {str(p.relative_to(self.card)): p.read_bytes() for p in self.card.rglob('*') if p.is_file()}

    def run_variant(self, variant):
        return installer.install(self.card, self.package, variant, self.profile.relative_to(self.card))

    def assert_protected(self):
        after = self.snapshot()
        for name, data in self.original.items():
            if name not in map(str, installer.MODULES.values()):
                self.assertEqual(after[name], data, name)

    def upgrade_package(self, new_commit='b'*40):
        """A later pair explicitly pins only the known earlier pair's code."""
        old_build = self.metadata['build']
        previous = dict(gui_sha256=self.metadata['gui']['sha256'], variants={})
        for name in installer.MODULES:
            directory = self.package/'variants'/name
            module = (directory/personalizer.BASE_MODULE).read_bytes()
            manifest = json.loads((directory/personalizer.MANIFEST).read_text())
            previous['variants'][name] = {key: manifest[key] for key in
                ('module_bytes', 'module_sha256', 'profile_module_offset')}
            updated = envelope(module[64:], personalizer.verify(module)['memory_bytes'], new_commit[:12])
            manifest.update(source_commit=new_commit, build=new_commit[:12], module_sha256=personalizer.sha(updated))
            (directory/personalizer.BASE_MODULE).write_bytes(updated)
            (directory/personalizer.MANIFEST).write_text(json.dumps(manifest))
            self.metadata['variants'][name]['module_sha256'] = personalizer.sha(updated)
        self.metadata.update(build=new_commit[:12], source_commit=new_commit,
                             compatible_packages={**self.metadata.get('compatible_packages', {}), old_build: previous})
        self.write_metadata()

    def migrate_second_session(self, variant):
        self.run_variant('reader-only')
        baseline = {p.name: p.read_bytes() for p in (self.card/installer.BACKUP).iterdir()}
        self.upgrade_package()
        self.run_variant('reader-only' if variant in ('restore', 'pending') else variant)
        if variant == 'restore':
            self.run_variant('restore')
        elif variant == 'pending':
            path = self.card/installer.STATE;state = json.loads(path.read_text());state['pending'] = True
            state['allowed']['reader-only'].append(personalizer.sha(self.original[str(installer.MODULES['reader-only'])]))
            path.write_text(json.dumps(state))
        self.upgrade_package('d'*40)
        self.run_variant('reader-only');self.run_variant('vmu-only');self.run_variant('restore')
        self.assertEqual({p.name: p.read_bytes() for p in (self.card/installer.BACKUP).iterdir()}, baseline)
        for path in installer.MODULES.values():
            self.assertEqual((self.card/path).read_bytes(), self.original[str(path)])
        self.assert_protected()

    def test_switches_restore_inactive_module_and_exact_originals(self):
        self.run_variant('reader-only')
        reader = (self.card/installer.MODULES['reader-only']).read_bytes()
        self.assertNotEqual(reader, self.original[str(installer.MODULES['reader-only'])])
        self.run_variant('vmu-only')
        self.assertEqual((self.card/installer.MODULES['reader-only']).read_bytes(),
                         self.original[str(installer.MODULES['reader-only'])])
        self.run_variant('reader-only')
        self.assertEqual((self.card/installer.MODULES['vmu-only']).read_bytes(),
                         self.original[str(installer.MODULES['vmu-only'])])
        self.assertEqual((self.card/installer.MODULES['reader-only']).read_bytes(), reader)
        self.run_variant('restore')
        for path in installer.MODULES.values():
            self.assertEqual((self.card/path).read_bytes(), self.original[str(path)])
        self.assert_protected()

    def test_repeated_install_and_restore_are_no_write(self):
        for variant in ('reader-only', 'restore'):
            self.run_variant(variant)
            with patch.object(installer, 'publish', side_effect=AssertionError('Unexpected write')):
                self.assertTrue(self.run_variant(variant)['already_installed'])

    def test_wrong_gui_refuses_before_backup_or_module_write(self):
        (self.card/installer.GUI).write_bytes(b'other GUI')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'existing 8adf'):
            self.run_variant('reader-only')
        self.assertEqual(self.snapshot(), before)

    def test_changed_payload_refuses_before_backup(self):
        path = self.package/'variants/reader-only'/personalizer.BASE_MODULE
        data = bytearray(path.read_bytes());data[-1] ^= 1;path.write_bytes(data)
        with self.assertRaises(ValueError):
            self.run_variant('reader-only')
        self.assertEqual(self.snapshot(), self.original)

    def test_corrupt_backup_preserves_current_files(self):
        self.run_variant('reader-only')
        (self.card/installer.BACKUP/'vmu-only.kui').write_bytes(b'bad backup')
        before = self.snapshot()
        with self.assertRaises(ValueError):
            self.run_variant('restore')
        self.assertEqual(self.snapshot(), before)

    def test_external_module_update_is_not_overwritten(self):
        self.run_variant('reader-only')
        (self.card/installer.MODULES['vmu-only']).write_bytes(b'external loader update')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'outside this test'):
            self.run_variant('restore')
        self.assertEqual(self.snapshot(), before)

    def test_symlink_module_is_refused(self):
        path = self.card/installer.MODULES['vmu-only']
        path.unlink();other = self.root/'other';other.write_bytes(b'outside card')
        path.symlink_to(other)
        with self.assertRaisesRegex(ValueError, 'ordinary'):
            self.run_variant('reader-only')
        self.assertEqual(other.read_bytes(), b'outside card')

    def test_postrename_failure_restores_exact_invocation_bytes(self):
        self.run_variant('reader-only')
        before = {name: (self.card/path).read_bytes() for name, path in installer.MODULES.items()}
        publish = installer.publish
        injected = False
        def fail_once(path, data):
            nonlocal injected
            publish(path, data)
            if path == self.card/installer.MODULES['vmu-only'] and not injected:
                injected = True
                raise OSError('injected failure after rename')
        with patch.object(installer, 'publish', side_effect=fail_once):
            with self.assertRaisesRegex(OSError, 'injected'):
                self.run_variant('vmu-only')
        for name, path in installer.MODULES.items():
            self.assertEqual((self.card/path).read_bytes(), before[name])
        self.run_variant('restore')
        self.assert_protected()

    def test_pending_receipt_allows_exact_mixed_state_recovery(self):
        self.run_variant('reader-only')
        state_path = self.card/installer.STATE
        state = json.loads(state_path.read_text())
        state['pending'] = True
        for name, path in installer.MODULES.items():
            state['allowed'][name] = list(dict.fromkeys(state['allowed'][name]+[
                personalizer.sha(self.original[str(path)])]))
        state_path.write_text(json.dumps(state))
        reader_path = self.card/installer.MODULES['reader-only']
        reader_path.write_bytes(self.original[str(installer.MODULES['reader-only'])])
        self.run_variant('restore')
        self.assert_protected()

    def test_absent_original_loader_is_absent_after_restore(self):
        path = self.card/installer.MODULES['vmu-only'];path.unlink()
        self.run_variant('vmu-only');self.assertTrue(path.exists())
        self.run_variant('restore');self.assertFalse(path.exists())

    def test_restore_does_not_require_valid_prepared_pair(self):
        self.run_variant('vmu-only')
        self.profile.write_bytes(b'profile unavailable during recovery')
        self.run_variant('restore')
        self.assertEqual(self.profile.read_bytes(), b'profile unavailable during recovery')

    def test_save_changes_between_tests_are_retained(self):
        self.run_variant('reader-only')
        card = self.profile.parent/'card.vmd';journal = self.profile.parent/'card.journal'
        card.write_bytes(b'S'*131072);journal.write_bytes(b'N'*1024)
        self.run_variant('vmu-only');self.run_variant('restore')
        self.assertEqual(card.read_bytes(), b'S'*131072)
        self.assertEqual(journal.read_bytes(), b'N'*1024)

    def test_cli_runs_from_outside_extracted_package(self):
        result = subprocess.run([sys.executable, str(self.package/'install.py'), str(self.card),
            '--profile', str(self.profile.relative_to(self.card)), '--variant', 'reader-only'],
            cwd=self.root, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Virtual A2 is OFF', result.stdout)
        self.assert_protected()

    def test_known_previous_active_variants_reuse_immutable_original_backup(self):
        for previous_variant in installer.MODULES:
            with self.subTest(previous_variant=previous_variant):
                # Return to the original pair for each independent migration.
                if (self.card/installer.BACKUP).exists():
                    self.run_variant('restore')
                    shutil.rmtree(self.card/installer.BACKUP_PARENT)
                if previous_variant == 'vmu-only':
                    # Fresh fixture with the same original files and profile.
                    self.package = self.root/'previous-again'
                    shutil.copytree(self.root/'initial-package', self.package)
                    self.metadata = json.loads((self.package/'isolation.json').read_text())
                else:
                    shutil.copytree(self.package, self.root/'initial-package')
                self.run_variant(previous_variant)
                backup_before = {p.name: p.read_bytes() for p in (self.card/installer.BACKUP).iterdir()}
                self.upgrade_package()
                self.run_variant('reader-only');self.run_variant('vmu-only');self.run_variant('restore')
                self.assertEqual({p.name: p.read_bytes() for p in (self.card/installer.BACKUP).iterdir()}, backup_before)
                for path in installer.MODULES.values():
                    self.assertEqual((self.card/path).read_bytes(), self.original[str(path)])
                self.assert_protected()

    def test_known_previous_restored_session_can_install_new_pair(self):
        self.run_variant('vmu-only');self.run_variant('restore')
        backup = (self.card/installer.BACKUP/'manifest.json').read_bytes()
        self.upgrade_package();self.run_variant('reader-only');self.run_variant('restore')
        self.assertEqual((self.card/installer.BACKUP/'manifest.json').read_bytes(), backup)

    def test_previous_pending_receipt_can_restore_without_profile_file(self):
        self.run_variant('reader-only')
        state = json.loads((self.card/installer.STATE).read_text());state['pending'] = True
        (self.card/installer.STATE).write_text(json.dumps(state))
        self.upgrade_package();self.profile.write_bytes(b'unavailable')
        self.run_variant('restore')
        self.assertEqual(self.profile.read_bytes(), b'unavailable')
        for path in installer.MODULES.values():
            self.assertEqual((self.card/path).read_bytes(), self.original[str(path)])

    def test_unknown_previous_backup_build_refuses_without_writes(self):
        self.run_variant('reader-only');self.upgrade_package()
        path = self.card/installer.BACKUP/'manifest.json'
        record = json.loads(path.read_text());record['package_build'] = 'c'*12
        path.write_text(json.dumps(record));before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Backup provenance'):
            self.run_variant('reader-only')
        self.assertEqual(self.snapshot(), before)

    def test_previous_receipt_cannot_authorize_changed_code(self):
        self.run_variant('reader-only');self.upgrade_package()
        path = self.card/installer.MODULES['reader-only']
        data = bytearray(path.read_bytes()[64:]);data[-1] ^= 1
        data = envelope(bytes(data), len(data), COMMIT[:12]);path.write_bytes(data)
        state_path = self.card/installer.STATE
        state = json.loads(state_path.read_text());state['allowed']['reader-only'] = [personalizer.sha(data)]
        state_path.write_text(json.dumps(state));before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'pinned isolation code'):
            self.run_variant('restore')
        self.assertEqual(self.snapshot(), before)

    def test_previous_receipt_cannot_authorize_another_profile_identity(self):
        self.run_variant('reader-only');self.upgrade_package()
        other_profile = pair(self.card, 'p0006', number='0006')
        path = self.card/installer.MODULES['reader-only']
        data = bytearray(path.read_bytes());offset = self.metadata['compatible_packages'][COMMIT[:12]][
            'variants']['reader-only']['profile_module_offset']
        data[offset:offset+4096] = other_profile.read_bytes()
        data = envelope(bytes(data[64:]), len(data)-64, COMMIT[:12]);path.write_bytes(data)
        state_path = self.card/installer.STATE
        state = json.loads(state_path.read_text());state['allowed']['reader-only'] = [personalizer.sha(data)]
        state_path.write_text(json.dumps(state));before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'different prepared card'):
            self.run_variant('restore')
        self.assertEqual(self.snapshot(), before)

    def test_second_known_reader_session_preserves_original_backup(self):
        self.migrate_second_session('reader-only')

    def test_second_known_standard_session_preserves_original_backup(self):
        self.migrate_second_session('vmu-only')

    def test_second_known_restored_session_preserves_original_backup(self):
        self.migrate_second_session('restore')

    def test_second_known_pending_session_preserves_original_backup(self):
        self.migrate_second_session('pending')

    def test_cli_capture_instructions_keep_normal_mode_and_two_pages(self):
        self.metadata['variants']['reader-only'].update(e1_wait_capture=True, prefetch_enabled=False)
        self.write_metadata()
        result = subprocess.run([sys.executable, str(self.package/'install.py'), str(self.card),
            '--profile', str(self.profile.relative_to(self.card)), '--variant', 'reader-only'],
            cwd=self.root, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('30 controller polls', result.stdout)
        self.assertIn('both short CPU/reader-state pages', result.stdout)
        self.assertIn('NORMAL mode remains selected', result.stdout)
        manifest = json.loads((self.package/'variants/reader-only'/personalizer.MANIFEST).read_text())
        offset = manifest['runtime_mode_module_offset']
        installed = (self.card/installer.MODULES['reader-only']).read_bytes()
        self.assertEqual(mode.decode_mode(installed[offset:offset+32]), 0)


if __name__ == '__main__':
    unittest.main()
