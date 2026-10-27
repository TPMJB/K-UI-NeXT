#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Standalone publication has its own exact rollback and retains the 6be session."""
import hashlib
import importlib.util
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
import install_native_standalone_reader as installer
import install_vmu_native_isolation as core
import test_install_vmu_native_isolation as fixtures
from test_personalize_vmu_native_persistent import pair
from runtime_package import envelope


def snapshot(root):
    return {str(path.relative_to(root)): path.read_bytes()
            for path in root.rglob('*') if path.is_file()}


def extracted_install_check(files, gui_bytes):
    """Run the actual extracted CLI against a disposable, personalized 6be card."""
    with tempfile.TemporaryDirectory(prefix='standalone-cli-') as temporary:
        directory = Path(temporary)
        package, card = directory/'package', directory/'card'
        for relative, data in files.items():
            path = Path(relative)
            installer.require(not path.is_absolute() and '..' not in path.parts, 'Unsafe extracted package path')
            target = package/path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        (card/installer.GUI).parent.mkdir(parents=True)
        (card/installer.GUI).write_bytes(gui_bytes)
        profile = pair(card, 'p0005', number='0005')
        old = envelope(b'\x09\x00'*64, 128, '0123456789ab')
        for path in core.MODULES.values():
            (card/path).parent.mkdir(parents=True, exist_ok=True)
            (card/path).write_bytes(old)
        settings = card/'KUI/apps/games/launch-settings-standalone.bin'
        settings.write_bytes(b'preserved current launch settings')
        # Install the shipped canonical 6be module with a real synthetic owner
        # profile, so the new CLI exercises full manifest and receipt checks.
        baseline_core = installer.load_module(package/'tools/install_vmu_native_isolation.py', 'standalone_cli_baseline')
        baseline_core.install(card, package/'baseline', 'reader-only', profile.relative_to(card))
        before = snapshot(card)
        def run(variant, success=True):
            result = subprocess.run([sys.executable, str(package/'install.py'), str(card),
                                     '--variant', variant], cwd=directory, text=True, capture_output=True)
            installer.require(result.returncode == (0 if success else 2),
                              'Extracted standalone CLI failed: '+result.stdout+result.stderr)
            return result
        first = run('doa2')
        installer.require('Background Off (Standard)' in first.stdout and
                          'New standalone demand' in first.stdout, 'Extracted CLI selector instructions differ')
        run('doa2')
        installer.require((card/installer.DESTINATION).read_bytes() == files[installer.MODULE_PATH],
                          'Extracted candidate bytes differ')
        run('restore'); run('restore')
        installer.require({key: value for key, value in snapshot(card).items()
                           if not key.startswith(str(installer.PARENT)+'/')} == before,
                          'Extracted restore changed the original 6be session or prepared files')
        installer.require((card/installer.BACKUP/installer.DESTINATION.name).read_bytes() == old,
                          'Extracted ordinary loader backup differs')
        for relative, replacement in ((installer.DESTINATION, envelope(b'\x0b\x00'*64, 128, '0123456789ab')),
                                      (installer.GUI, old),
                                      (installer.BACKUP/installer.DESTINATION.name, b'corrupt backup')):
            path = card/relative
            saved = path.read_bytes(); path.write_bytes(replacement)
            refused_before = snapshot(card)
            run('doa2', success=False)
            installer.require(snapshot(card) == refused_before, 'Extracted refusal wrote card files')
            path.write_bytes(saved)
        # Execute the same shipped module in-process for a post-rename failure;
        # CLI subprocess checks above independently exercise package imports.
        shipped = installer.load_module(package/'install.py', 'standalone_cli_rollback')
        real_publish = shipped.publish
        injected = False
        def fail_once(path, data):
            nonlocal injected
            real_publish(path, data)
            if path == card/installer.DESTINATION and not injected:
                injected = True
                raise OSError('injected post-rename failure')
        try:
            with patch.object(shipped, 'publish', side_effect=fail_once):
                shipped.install(card, package, 'doa2')
        except OSError as error:
            installer.require(str(error) == 'injected post-rename failure', 'Unexpected rollback error')
        else:
            raise ValueError('Extracted rollback fault was not exercised')
        installer.require(injected and (card/installer.DESTINATION).read_bytes() == old,
                          'Extracted publication failure did not restore exact invocation bytes')
        run('restore')
        installer.require({key: value for key, value in snapshot(card).items()
                           if not key.startswith(str(installer.PARENT)+'/')} == before,
                          'Extracted rollback changed protected files')
        return dict(actual_cli_invocations=8, install_twice=True, restore_twice=True,
                    exact_previous_loader_restored=True, customized_6be_module_unchanged=True,
                    isolation_backup_and_receipt_unchanged=True, prepared_files_gui_settings_unchanged=True,
                    changed_module_refused_before_writes=True, changed_gui_refused_before_writes=True,
                    corrupt_standalone_backup_refused_before_writes=True,
                    post_rename_failure_exact_rollback=True)


class StandaloneInstallTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.IsolationInstallTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.card = self.fixture.card
        self.original_boot = envelope(b'\x09\x00'*64, 128, '0123456789ab')
        for path in core.MODULES.values():
            (self.card/path).write_bytes(self.original_boot)
        self.fixture.upgrade_package('6be76940ccc94166b2f14708702d6d08bcbde4d1')
        self.fixture.metadata.update(split_controller=True, cached_metadata=False, public_variant='combined')
        for name, entry in self.fixture.metadata['variants'].items():
            raw = json.dumps(dict(module_sha256=entry['module_sha256'])).encode()
            (self.fixture.package/entry['directory']/'variant-layout.json').write_bytes(raw)
            entry['linked_layout_sha256'] = hashlib.sha256(raw).hexdigest()
        self.fixture.write_metadata()
        self.fixture.run_variant('reader-only')
        self.before = snapshot(self.card)
        self.package = self.fixture.root/'standalone'
        shutil.copytree(self.fixture.package, self.package/'baseline')
        tools = self.package/'tools'; tools.mkdir()
        for name in ('runtime_package.py', 'vmu_persist_profile.py', 'vmu_native_mode.py',
                     'vmu_native_cache_seed.py', 'install_vmu_native_dual.py', 'install_vmu_native_isolation.py'):
            shutil.copyfile(ROOT/'tools'/name, tools/name)
        self.build = 'b'*12
        self.candidate = envelope(b'\x0b\x00'*80, 160, self.build)
        path = self.package/installer.MODULE_PATH; path.parent.mkdir(parents=True)
        path.write_bytes(self.candidate)
        baseline_sha = installer.sha((self.package/'baseline/isolation.json').read_bytes())
        gui = self.fixture.metadata['gui']
        self.metadata = dict(schema=installer.SCHEMA, source_commit='c'*40,
            runtime_source_commit='b'*40, build=self.build, variant='doa2', destination=str(installer.DESTINATION),
            module=dict(path=installer.MODULE_PATH, bytes=len(self.candidate), sha256=installer.sha(self.candidate)),
            gui=gui, baseline=dict(build=installer.BASELINE_BUILD, isolation_metadata_sha256=baseline_sha),
            standalone_reader=True, virtual_vmu=False, readahead=False, demand_only=True, reader='New (X)',
            ui_background_selection='Off (Standard)', engine='New standalone demand', heap_reservation_bytes=0)
        self.write_metadata()
        self.patchers = [patch.object(installer, 'BASELINE_METADATA_SHA', baseline_sha),
                         patch.object(installer, 'GUI_BUILD', gui['build']),
                         patch.object(installer, 'GUI_SHA', gui['sha256'])]
        for patcher in self.patchers:
            patcher.start(); self.addCleanup(patcher.stop)

    def write_metadata(self):
        (self.package/'standalone-reader.json').write_text(json.dumps(self.metadata))

    def run_variant(self, variant):
        return installer.install(self.card, self.package, variant)

    def assert_protected(self):
        after = snapshot(self.card)
        for path, data in self.before.items():
            if path != str(installer.DESTINATION):
                self.assertEqual(after[path], data, path)

    def test_exact_restore_and_repeated_operations_do_not_touch_6be_or_saves(self):
        self.run_variant('doa2')
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), self.candidate)
        self.assertEqual((self.card/installer.BACKUP/installer.DESTINATION.name).read_bytes(), self.original_boot)
        with patch.object(installer, 'publish', side_effect=AssertionError('unexpected publication')):
            self.assertTrue(self.run_variant('doa2')['already_installed'])
        self.run_variant('restore')
        with patch.object(installer, 'publish', side_effect=AssertionError('unexpected publication')):
            self.assertTrue(self.run_variant('restore')['already_installed'])
        self.assertEqual((self.card/installer.DESTINATION).read_bytes(), self.original_boot)
        self.assert_protected()

    def test_wrong_candidate_gui_receipt_profile_or_module_refuses_before_any_backup(self):
        paths = [self.package/installer.MODULE_PATH, self.card/installer.GUI,
                 self.card/core.STATE, self.card/installer.DUAL, self.fixture.profile,
                 self.card/installer.DESTINATION, self.card/core.BACKUP/'vmu-only.kui']
        for path in paths:
            with self.subTest(path=path):
                old = path.read_bytes(); path.write_bytes(old[:-1]+bytes([old[-1]^1]))
                before = snapshot(self.card)
                with self.assertRaises((ValueError, UnicodeError)):
                    self.run_variant('doa2')
                self.assertEqual(snapshot(self.card), before)
                self.assertFalse((self.card/installer.PARENT).exists())
                path.write_bytes(old)

    def test_untyped_capabilities_or_wrong_route_refuse_before_writes(self):
        for field, wrong in (('virtual_vmu', 0), ('readahead', True), ('demand_only', 1),
                             ('heap_reservation_bytes', False), ('destination', str(installer.DUAL))):
            with self.subTest(field=field):
                old = self.metadata[field]; self.metadata[field] = wrong; self.write_metadata()
                with self.assertRaises(ValueError):
                    self.run_variant('doa2')
                self.assertEqual(snapshot(self.card), self.before)
                self.metadata[field] = old
        self.write_metadata()

    def test_corrupt_backup_external_update_and_symlink_refuse_without_writes(self):
        self.run_variant('doa2')
        for relative in (installer.BACKUP/installer.DESTINATION.name, installer.DESTINATION,
                         core.STATE):
            with self.subTest(path=relative):
                path = self.card/relative; old = path.read_bytes(); path.write_bytes(b'external change')
                before = snapshot(self.card)
                with self.assertRaises(ValueError):
                    self.run_variant('restore')
                self.assertEqual(snapshot(self.card), before)
                path.write_bytes(old)
        path = self.card/installer.DESTINATION
        old = path.read_bytes(); path.unlink()
        outside = self.fixture.root/'outside.kui'; outside.write_bytes(old); path.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'ordinary'):
            self.run_variant('restore')
        self.assertEqual(outside.read_bytes(), old)

    def test_postrename_failure_rolls_back_and_pending_receipt_recovers(self):
        for variant, before in (('doa2', self.original_boot), ('restore', self.candidate)):
            with self.subTest(variant=variant):
                if variant == 'restore':
                    self.run_variant('doa2')
                publish = installer.publish
                injected = False
                def fail_once(path, data):
                    nonlocal injected
                    publish(path, data)
                    if path == self.card/installer.DESTINATION and not injected:
                        injected = True
                        raise OSError('injected failure after rename')
                with patch.object(installer, 'publish', side_effect=fail_once):
                    with self.assertRaisesRegex(OSError, 'injected'):
                        self.run_variant(variant)
                self.assertEqual((self.card/installer.DESTINATION).read_bytes(), before)
                self.run_variant('restore')
        self.assert_protected()

    def test_old_isolation_installer_refuses_until_standalone_restore(self):
        self.run_variant('doa2')
        before = snapshot(self.card)
        with self.assertRaisesRegex(ValueError, 'outside this test'):
            core.install(self.card, self.package/'baseline', 'reader-only',
                         self.fixture.profile.relative_to(self.card))
        self.assertEqual(snapshot(self.card), before)
        self.run_variant('restore')
        self.assertTrue(core.install(self.card, self.package/'baseline', 'reader-only',
                                   self.fixture.profile.relative_to(self.card))['already_installed'])

    def test_restore_keeps_current_profile_save_and_launch_settings_changes(self):
        self.run_variant('doa2')
        card = self.fixture.profile.parent/'card.vmd'; card.write_bytes(b'S'*131072)
        settings = self.card/'KUI/apps/games/launch-settings-a.bin'; settings.write_bytes(b'current user choices')
        self.run_variant('restore')
        self.assertEqual(card.read_bytes(), b'S'*131072)
        self.assertEqual(settings.read_bytes(), b'current user choices')


if __name__ == '__main__':
    unittest.main()
