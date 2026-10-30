#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Three geometries share one exact backup and preserve every earlier test."""
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
import install_skies_next as tool
import install_skies_next_ui as gui_tool
import install_native_vmu_only as original_core
import personalize_vmu_native_persistent as p
import test_install_skies_vmu_compare as old_fixture
import test_install_skies_launch_ui as gui_fixture
from test_personalize_vmu_native_persistent import generic
from test_install_native_vmu_only import module_bytes
from runtime_package import envelope


class NextInstall(unittest.TestCase):
    snapshot = old_fixture.SkiesCompareInstall.snapshot
    write_metadata = old_fixture.SkiesCompareInstall.write_metadata
    assert_protected = old_fixture.SkiesCompareInstall.assert_protected
    def setUp(self):
        old_fixture.SkiesCompareInstall.setUp(self)
        old, manifest = generic(self.package/'variants/reference-112')
        payload = old[64:]+b'reference-112'.ljust(16, b' ')
        module = envelope(payload, len(payload), manifest['build'])
        manifest.update(module_bytes=len(module), module_sha256=p.sha(module))
        (self.package/'variants/reference-112'/p.BASE_MODULE).write_bytes(module)
        (self.package/'variants/reference-112'/p.MANIFEST).write_text(json.dumps(manifest))
        self.metadata.pop('lease_bytes')
        self.metadata['schema'] = tool.SCHEMA
        self.metadata['variants']['reference-112'] = dict(directory='variants/reference-112', module_sha256=p.sha(module))
        for name, geometry in tool.CONTRACTS.items():
            self.metadata['variants'][name].update(geometry)
        self.metadata_write()
        shutil.copyfile(ROOT/'tools/install_skies_next.py', self.package/'install.py')
        for namespace in ('native-vmu-only-skies', 'skies-launch-ui', 'skies-next-ui'):
            parent = self.card/'KUI'/namespace;parent.mkdir()
            (parent/'state.json').write_text('{"old":"'+namespace+'"}')
            (parent/'backup').mkdir();(parent/'backup/original.kui').write_bytes(b'preserve '+namespace.encode())
        self.original = self.snapshot()
    def metadata_write(self):
        (self.package/'skies-next.json').write_text(json.dumps(self.metadata))
    def run_mode(self, mode=None, restore=False):
        return tool.install(self.card, self.package, mode, self.profile.relative_to(self.card), restore)
    def test_three_modes_have_distinct_code_same_profile_exact_geometry_and_restore(self):
        installed = {}
        for mode in ('baseline', 'full-submit', 'reference-112', 'baseline'):
            report = self.run_mode(mode)
            data = (self.card/tool.DESTINATION).read_bytes()
            geometry = tool.CONTRACTS[mode]
            self.assertEqual((report['lease_bytes'], report['controller_submit']),
                             (geometry['lease_bytes'], geometry['controller_submit']))
            manifest = json.loads((self.package/'variants'/mode/p.MANIFEST).read_text())
            at = manifest['profile_module_offset']
            self.assertEqual(data[at:at+4096], self.profile.read_bytes())
            if mode in installed:self.assertEqual(data, installed[mode])
            installed[mode] = data
        self.assertEqual(len(set(installed.values())), 3)
        report = self.run_mode(restore=True)
        self.assertNotIn('lease_bytes', report)
        self.assertEqual(report['build'], '4edbc5600000')
        self.assertEqual((self.card/tool.DESTINATION).read_bytes(), self.original[str(tool.DESTINATION)])
        self.assert_protected()
        self.assertEqual(original_core.PARENT, Path('KUI/native-vmu-only'))
    def test_omitted_mode_selects_72k_baseline(self):
        report = self.run_mode()
        self.assertEqual((report['mode'], report['lease_bytes'], report['controller_submit']),
                         ('baseline', 0x12000, 'split'))
        self.run_mode(restore=True)

    def test_entire_package_hash_stable_across_mode_geometry(self):
        hashes = []
        for mode in tool.CONTRACTS:
            core = tool.load_core(self.package)
            _, metadata, _, digest = tool.load_package(core, self.package, mode)
            self.assertEqual(metadata['lease_bytes'], tool.CONTRACTS[mode]['lease_bytes'])
            hashes.append(digest)
        self.assertEqual(len(set(hashes)), 1)
        self.assertEqual(hashes[0], core.sha(core.wire(self.metadata)))
    def test_wrong_nonselected_geometry_or_controller_or_transport_refuses_before_writes(self):
        for field, value in (('lease_bytes', 0x12000), ('controller_submit', 'full'), ('transport', 0)):
            with self.subTest(field=field):
                original = copy.deepcopy(self.metadata)
                self.metadata['variants']['reference-112'][field] = value
                self.metadata_write()
                with self.assertRaisesRegex(ValueError, 'contract'):
                    self.run_mode('baseline')
                self.assertEqual(self.snapshot(), self.original)
                self.metadata = original;self.metadata_write()
    def test_different_source_build_or_module_hash_refuses(self):
        path = self.package/'variants/reference-112'/p.BASE_MODULE
        data = path.read_bytes();path.write_bytes(data[:-1]+bytes([data[-1]^1]))
        with self.assertRaises(ValueError):self.run_mode('baseline')
        self.assertEqual(self.snapshot(), self.original)
    def test_repeated_modes_and_restore_are_no_write(self):
        for mode, restore in (('baseline', False), ('full-submit', False), ('reference-112', False), (None, True)):
            self.run_mode(mode, restore)
            core = tool.load_core(self.package)
            with patch.object(tool, 'load_core', return_value=core), patch.object(core, 'publish', side_effect=AssertionError('write')):
                self.assertTrue(self.run_mode(mode, restore)['already_installed'])
    def test_late_profile_path_change_refuses_target(self):
        core = tool.load_core(self.package);publish = core.publish;injected = False
        outside = self.root/'outside-profile';outside.write_bytes(self.profile.read_bytes())
        def swap(path, data):
            nonlocal injected
            publish(path, data)
            if path == self.card/tool.STATE and not injected:
                injected = True;self.profile.unlink();self.profile.symlink_to(outside)
        with patch.object(tool, 'load_core', return_value=core), patch.object(core, 'publish', side_effect=swap):
            with self.assertRaisesRegex(ValueError, 'ordinary'):self.run_mode('reference-112')
        self.assertEqual((self.card/tool.DESTINATION).read_bytes(), self.original[str(tool.DESTINATION)])
        self.run_mode(restore=True)
    def test_failure_after_rename_restores_current_mode(self):
        self.run_mode('baseline');before = self.snapshot()
        core = tool.load_core(self.package);publish = core.publish;injected = False
        def fail(path, data):
            nonlocal injected
            publish(path, data)
            if path == self.card/tool.DESTINATION and not injected:
                injected = True;raise OSError('after rename')
        with patch.object(tool, 'load_core', return_value=core), patch.object(core, 'publish', side_effect=fail):
            with self.assertRaisesRegex(OSError, 'after rename'):self.run_mode('reference-112')
        self.assertEqual(self.snapshot(), before)
    def test_external_target_update_refuses_restore(self):
        self.run_mode('reference-112')
        (self.card/tool.DESTINATION).write_bytes(module_bytes(b'external update', 'abcdefabcdef'))
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'outside this test'):self.run_mode(restore=True)
        self.assertEqual(self.snapshot(), before)
    def test_real_extracted_cli_three_modes_and_restore(self):
        for args in (('--mode','baseline'),('--mode','full-submit'),('--mode','reference-112'),('--restore',)):
            run = subprocess.run([sys.executable,str(self.package/'install.py'),str(self.card),*args,
                '--profile',str(self.profile.relative_to(self.card))],text=True,capture_output=True,cwd=self.root)
            self.assertEqual(run.returncode,0,run.stderr)
        self.assertEqual((self.card/tool.DESTINATION).read_bytes(), self.original[str(tool.DESTINATION)])
        self.assert_protected()


class NextUiInstall(unittest.TestCase):
    snapshot = gui_fixture.SkiesLaunchUiInstall.snapshot
    write_metadata = gui_fixture.SkiesLaunchUiInstall.write_metadata
    protected = gui_fixture.SkiesLaunchUiInstall.protected
    def setUp(self):
        gui_fixture.SkiesLaunchUiInstall.setUp(self)
        shutil.copyfile(ROOT/'tools/install_skies_launch_ui.py',self.package/'tools/install_skies_launch_ui.py')
        shutil.copyfile(ROOT/'tools/install_skies_next_ui.py',self.package/'install-ui.py')
        self.gui['schema']=gui_tool.SCHEMA
        (self.package/'gui.json').write_text(json.dumps(self.gui))
        for namespace in ('native-vmu-only-skies','skies-launch-ui','skies-next-loader'):
            path=self.card/'KUI'/namespace;path.mkdir();(path/'state.json').write_bytes(b'old receipt '+namespace.encode())
        self.original=self.snapshot()
    def run_install(self, restore=False):return gui_tool.install(self.card,self.package,restore)
    def test_new_ui_namespace_preserves_all_old_namespaces_and_exact_restore(self):
        self.run_install();self.assertEqual((self.card/gui_tool.DESTINATION).read_bytes(),self.candidate)
        self.assertTrue((self.card/gui_tool.STATE).is_file());self.protected()
        self.run_install(True)
        self.assertEqual((self.card/gui_tool.DESTINATION).read_bytes(),self.original[str(gui_tool.DESTINATION)])
        self.protected()
    def test_unrelated_gui_update_is_preserved(self):
        self.run_install();other=module_bytes(b'other GUI update','abcdefabcdef')
        (self.card/gui_tool.DESTINATION).write_bytes(other)
        before=self.snapshot()
        with self.assertRaisesRegex(ValueError,'outside this UI test'):self.run_install(True)
        self.assertEqual(self.snapshot(),before)
    def test_real_extracted_cli_ui_restore(self):
        for args in ((),('--restore',)):
            run=subprocess.run([sys.executable,str(self.package/'install-ui.py'),str(self.card),*args],
                text=True,capture_output=True,cwd=self.root)
            self.assertEqual(run.returncode,0,run.stderr)
        self.protected()


if __name__=='__main__':unittest.main()
