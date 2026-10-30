#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Paired compact installs preserve existing saves, settings and all old backups."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
import install_skies_background72 as tool
import install_skies_background72_ui as ui
import personalize_vmu_native_persistent as p
import test_install_native_vmu_only as fixture
from test_personalize_vmu_native_persistent import generic, COMMIT
from runtime_package import envelope

OLD_NAMESPACES = ('native-vmu-only', 'native-vmu-only-skies', 'skies-launch-ui',
                  'skies-next-loader', 'skies-next-ui', 'skies-read-fix-loader')


class Background72Install(unittest.TestCase):
    snapshot = fixture.VmuOnlyInstallTests.snapshot
    write_metadata = fixture.VmuOnlyInstallTests.write_metadata

    def setUp(self):
        fixture.VmuOnlyInstallTests.setUp(self)
        shutil.rmtree(self.package/'variants')
        self.metadata = dict(schema=tool.SCHEMA, source_commit=COMMIT, build=COMMIT[:12], variants={})
        for name, geometry in tool.CONTRACTS.items():
            old, manifest = generic(self.package/'variants'/name)
            payload = old[64:]+name.encode().ljust(16, b' ')
            module = envelope(payload, len(payload), COMMIT[:12])
            manifest.update(module_bytes=len(module), module_sha256=p.sha(module))
            (self.package/'variants'/name/p.BASE_MODULE).write_bytes(module)
            (self.package/'variants'/name/p.MANIFEST).write_text(json.dumps(manifest))
            self.metadata['variants'][name] = dict(directory='variants/'+name, **geometry, module_sha256=p.sha(module))
        self.metadata_write()
        for name in ('install_native_vmu_only.py', 'install_skies_launch_ui.py'):
            shutil.copyfile(ROOT/'tools'/name, self.package/'tools'/name)
        shutil.copyfile(ROOT/'tools/install_skies_background72.py', self.package/'install.py')
        shutil.copyfile(ROOT/'tools/install_skies_background72_ui.py', self.package/'install-ui.py')
        (self.package/'gui').mkdir()
        self.gui = fixture.module_bytes(b'matching compact Background GUI', COMMIT[:12])
        (self.package/'gui/runtime.kui').write_bytes(self.gui)
        (self.package/'gui.json').write_text(json.dumps(dict(schema=ui.SCHEMA, source_commit=COMMIT,
            build=COMMIT[:12], module_sha256=p.sha(self.gui))))
        for namespace in OLD_NAMESPACES:
            directory = self.card/'KUI'/namespace
            (directory/'backup').mkdir(parents=True)
            (directory/'state.json').write_bytes(b'old receipt '+namespace.encode())
            (directory/'backup/original.kui').write_bytes(b'old original '+namespace.encode())
            (directory/'backup/manifest.json').write_bytes(b'old manifest '+namespace.encode())
        self.original = self.snapshot()

    def metadata_write(self):
        (self.package/'skies-background72.json').write_text(json.dumps(self.metadata))

    def run_install(self, restore=False):
        return tool.install(self.card, self.package, self.profile.relative_to(self.card), restore)

    def protected(self, gui=False):
        allowed = {str(path) for path in tool.DESTINATIONS.values()}
        if gui:allowed.add(str(ui.DESTINATION))
        for name, data in self.original.items():
            if name not in allowed:self.assertEqual(self.snapshot()[name], data, name)

    def assert_original_loaders(self):
        for name, destination in tool.DESTINATIONS.items():
            self.assertEqual((self.card/destination).read_bytes(), self.original[str(destination)], name)

    def test_both_distinct_modules_same_profile_geometry_and_exact_restore(self):
        result = self.run_install()
        self.assertEqual(result['profile'], str(self.profile.relative_to(self.card)))
        installed = []
        for name, geometry in tool.CONTRACTS.items():
            actual = (self.card/tool.DESTINATIONS[name]).read_bytes();installed.append(actual)
            manifest = json.loads((self.package/'variants'/name/p.MANIFEST).read_text())
            offset = manifest['profile_module_offset']
            self.assertEqual(actual[offset:offset+4096], self.profile.read_bytes())
            for key, value in geometry.items():self.assertEqual(result['components'][name][key], value)
        self.assertNotEqual(*installed)
        self.protected()
        restored = self.run_install(True)
        self.assert_original_loaders();self.protected()
        self.assertTrue(restored['restored'])
        self.assertNotIn('lease_bytes', restored['components']['background'])
        self.assertEqual((self.card/'KUI/runtime.kui').read_bytes(), self.original['KUI/runtime.kui'])

    def test_install_and_restore_repeat_without_publication(self):
        for restore in (False, True):
            self.run_install(restore)
            cores = {name: tool.load_core(self.package, name) for name in tool.CONTRACTS}
            with patch.object(tool, 'load_core', side_effect=lambda package, name: cores[name]):
                with patch.object(cores['standalone'], 'publish', side_effect=AssertionError('unexpected write')), \
                     patch.object(cores['background'], 'publish', side_effect=AssertionError('unexpected write')):
                    self.assertTrue(all(x['already_installed'] for x in self.run_install(restore)['components'].values()))

    def test_all_metadata_geometry_source_and_boolean_types_bound_before_writes(self):
        changes = [('lease_bytes', 331776), ('worker_bytes', 114688), ('cache_bytes', 0),
            ('controller_submit', 'full'), ('transport', 0), ('prefetch_enabled', 1),
            ('receive_engine', 'standard-paced-dma')]
        for key, value in changes:
            with self.subTest(key=key):
                saved = copy.deepcopy(self.metadata)
                self.metadata['variants']['background'][key] = value;self.metadata_write()
                with self.assertRaises(ValueError):self.run_install()
                self.assertEqual(self.snapshot(), self.original)
                self.metadata = saved;self.metadata_write()
        self.metadata['source_commit'] = 'd'*40;self.metadata['build'] = 'd'*12;self.metadata_write()
        with self.assertRaises(ValueError):self.run_install()
        self.assertEqual(self.snapshot(), self.original)

    def test_background_tamper_refuses_before_any_backup_or_standard_write(self):
        path = self.package/'variants/background'/p.BASE_MODULE
        raw = path.read_bytes();path.write_bytes(raw[:-1]+bytes([raw[-1]^1]))
        with self.assertRaises(ValueError):self.run_install()
        self.assertEqual(self.snapshot(), self.original)
        self.assertFalse((self.card/tool.PARENT).exists())

    def test_background_drift_preflight_refuses_before_touching_standard(self):
        self.run_install()
        (self.card/tool.DESTINATIONS['background']).write_bytes(fixture.module_bytes(b'new independent background', 'abcdefabcdef'))
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'outside this package'):self.run_install(True)
        self.assertEqual(self.snapshot(), before)

    def test_profile_late_change_rolls_back_both_original_loaders(self):
        cores = {name: tool.load_core(self.package, name) for name in tool.CONTRACTS}
        core = cores['background'];publish = core.publish;injected = False
        def change_once(path, data):
            nonlocal injected
            publish(path, data)
            if path == self.card/core.STATE and not injected:
                injected = True
                (self.profile.parent/'identity.bin').write_bytes(b'X'*16)
        with patch.object(tool, 'load_core', side_effect=lambda package, name: cores[name]), \
             patch.object(core, 'publish', side_effect=change_once):
            with self.assertRaisesRegex(ValueError, 'profile/identity changed'):self.run_install()
        self.assert_original_loaders()
        self.run_install(True)

    def test_failure_after_second_target_rename_rolls_back_both_and_receipts(self):
        self.run_install();self.run_install(True);before = self.snapshot()
        cores = {name: tool.load_core(self.package, name) for name in tool.CONTRACTS}
        core = cores['background'];publish = core.publish;injected = False
        def fail_once(path, data):
            nonlocal injected
            publish(path, data)
            if path == self.card/core.DESTINATION and not injected:
                injected = True;raise OSError('second loader renamed')
        with patch.object(tool, 'load_core', side_effect=lambda package, name: cores[name]), \
             patch.object(core, 'publish', side_effect=fail_once):
            with self.assertRaisesRegex(OSError, 'second loader'):self.run_install()
        self.assertEqual(self.snapshot(), before)

    def test_interrupted_paired_publication_accepts_mixed_exact_states_for_restore(self):
        self.run_install()
        for name, destination in tool.DESTINATIONS.items():
            core = tool.load_core(self.package, name)
            current = (self.card/destination).read_bytes()
            original = self.original[str(destination)]
            state = json.loads((self.card/core.STATE).read_text())
            state.update(pending=True, allowed=[core.sha(original), core.sha(current)])
            (self.card/core.STATE).write_text(json.dumps(state))
            if name == 'background':(self.card/destination).write_bytes(original)
        # Simulate loss of power between the two target publications. Restore
        # recovers both originals even after the prepared profile disappeared.
        self.profile.unlink()
        tool.install(self.card, self.package, restore=True)
        self.assert_original_loaders()

    def test_whole_metadata_digest_stable_and_old_core_constants_untouched(self):
        core = tool.load_core(self.package, 'standalone')
        _, _, _, digest = tool.load_package(core, self.package)
        self.assertEqual(digest, core.sha(core.wire(self.metadata)))
        self.assertEqual(fixture.installer.PARENT, Path('KUI/native-vmu-only'))
        self.assertNotIn(tool.PARENT.name, OLD_NAMESPACES)

    def test_missing_backup_or_symlink_refuses_before_mutation(self):
        core = tool.load_core(self.package, 'background')
        (self.card/core.PARENT).mkdir(parents=True)
        (self.card/core.STATE).write_text('{}')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'no exact backup'):self.run_install()
        self.assertEqual(self.snapshot(), before)
        shutil.rmtree(self.card/tool.PARENT)
        outside = self.root/'outside';outside.mkdir()
        (self.card/tool.PARENT).symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'ordinary'):self.run_install()
        self.assertEqual(list(outside.iterdir()), [])

    def test_restore_needs_no_profile_and_preserves_saves_updated_after_install(self):
        self.run_install()
        image = self.profile.parent/'card.vmd';image.write_bytes(b'S'+image.read_bytes()[1:])
        journal = self.profile.parent/'card.journal';journal.write_bytes(b'J'+journal.read_bytes()[1:])
        self.profile.unlink()
        tool.install(self.card, self.package, restore=True)
        self.assert_original_loaders()
        self.assertEqual(image.read_bytes()[0:1], b'S');self.assertEqual(journal.read_bytes()[0:1], b'J')

    def test_ui_install_and_independent_restores_preserve_all_other_targets(self):
        self.run_install();loaders = {name: (self.card/path).read_bytes() for name,path in tool.DESTINATIONS.items()}
        ui.install(self.card, self.package)
        self.assertEqual((self.card/ui.DESTINATION).read_bytes(), self.gui)
        ui.install(self.card, self.package, True)
        self.assertEqual((self.card/ui.DESTINATION).read_bytes(), self.original[str(ui.DESTINATION)])
        for name,path in tool.DESTINATIONS.items():self.assertEqual((self.card/path).read_bytes(), loaders[name])
        self.run_install(True);self.assert_original_loaders();self.protected()

    def test_background_only_needs_no_profile_or_card_and_retains_standard(self):
        shutil.rmtree(self.card/'KUI/vmu-test')
        before = self.snapshot()
        result = tool.install(self.card, self.package, background_only=True)
        self.assertEqual(set(result['components']), {'background'})
        self.assertNotIn('profile', result)
        module = (self.package/'variants/background'/p.BASE_MODULE).read_bytes()
        self.assertEqual((self.card/tool.DESTINATIONS['background']).read_bytes(), module)
        self.assertFalse((self.card/tool.PARENT/'standalone').exists())
        self.assertEqual((self.card/tool.DESTINATIONS['standalone']).read_bytes(), before[str(tool.DESTINATIONS['standalone'])])
        self.assertEqual((self.card/ui.DESTINATION).read_bytes(), before[str(ui.DESTINATION)])
        tool.install(self.card, self.package, background_only=True)
        tool.install(self.card, self.package, restore=True, background_only=True)
        for name, data in before.items():self.assertEqual(self.snapshot()[name], data, name)

    def test_background_only_then_paired_shares_backup_identity_and_subset_restore(self):
        tool.install(self.card, self.package, background_only=True)
        background_core = tool.load_core(self.package, 'background')
        backup = (self.card/background_core.BACKUP/'manifest.json').read_bytes()
        self.run_install()
        self.assertEqual((self.card/background_core.BACKUP/'manifest.json').read_bytes(), backup)
        standalone_new = (self.card/tool.DESTINATIONS['standalone']).read_bytes()
        tool.install(self.card, self.package, restore=True, background_only=True)
        self.assertEqual((self.card/tool.DESTINATIONS['standalone']).read_bytes(), standalone_new)
        self.assertEqual((self.card/tool.DESTINATIONS['background']).read_bytes(),
                         self.original[str(tool.DESTINATIONS['background'])])
        self.run_install(True);self.assert_original_loaders();self.protected()

    def test_paired_then_background_only_generic_switch_preserves_standard_receipt(self):
        self.run_install()
        core = tool.load_core(self.package, 'standalone')
        standalone = (self.card/core.DESTINATION).read_bytes()
        state = (self.card/core.STATE).read_bytes()
        tool.install(self.card, self.package, background_only=True)
        self.assertEqual((self.card/tool.DESTINATIONS['background']).read_bytes(),
                         (self.package/'variants/background'/p.BASE_MODULE).read_bytes())
        self.assertEqual((self.card/core.DESTINATION).read_bytes(), standalone)
        self.assertEqual((self.card/core.STATE).read_bytes(), state)
        tool.install(self.card, self.package, restore=True, background_only=True)
        self.assertEqual((self.card/core.STATE).read_bytes(), state)
        self.run_install(True);self.assert_original_loaders()

    def test_paired_restore_after_only_background_install_refuses_without_writes(self):
        tool.install(self.card, self.package, background_only=True)
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'No paired loader backup.*standalone'):
            tool.install(self.card, self.package, restore=True)
        self.assertEqual(self.snapshot(), before)
        tool.install(self.card, self.package, restore=True, background_only=True)
        self.assert_original_loaders()

    def test_background_only_extracted_cli_no_profile_then_exact_restore(self):
        shutil.rmtree(self.card/'KUI/vmu-test')
        for args in (('--background-only',), ('--background-only',), ('--background-only','--restore')):
            run = subprocess.run([sys.executable,str(self.package/'install.py'),str(self.card),*args],
                                 text=True,capture_output=True,cwd=self.root)
            self.assertEqual(run.returncode, 0, run.stderr)
            report = json.JSONDecoder().raw_decode(run.stdout)[0]
            self.assertTrue(report['background_only'])
            self.assertEqual(set(report['components']), {'background'})
        self.assert_original_loaders()

    def test_extracted_cli_installs_both_gui_then_independently_restores(self):
        for script, args in (('install.py', ('--profile',str(self.profile.relative_to(self.card)))),
                             ('install-ui.py', ()), ('install.py', ('--restore',)), ('install-ui.py', ('--restore',))):
            run = subprocess.run([sys.executable,str(self.package/script),str(self.card),*args],
                                 text=True,capture_output=True,cwd=self.root)
            self.assertEqual(run.returncode, 0, run.stderr)
        self.assert_original_loaders();self.protected()
        self.assertEqual((self.card/ui.DESTINATION).read_bytes(), self.original[str(ui.DESTINATION)])


if __name__ == '__main__':unittest.main()
