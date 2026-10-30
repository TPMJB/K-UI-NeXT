#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-policy installs preserve the GUI, prepared card and all earlier tests."""
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
import install_skies_read_fix as tool
import install_native_vmu_only as original_core
import personalize_vmu_native_persistent as p
import test_install_native_vmu_only as base_fixture
from test_personalize_vmu_native_persistent import generic
from test_install_native_vmu_only import module_bytes
from runtime_package import envelope

OLD_NAMESPACES = ('native-vmu-only', 'native-vmu-only-skies', 'skies-launch-ui',
                  'skies-next-loader', 'skies-next-ui')


class ReadFixInstall(unittest.TestCase):
    snapshot = base_fixture.VmuOnlyInstallTests.snapshot
    write_metadata = base_fixture.VmuOnlyInstallTests.write_metadata
    assert_protected = base_fixture.VmuOnlyInstallTests.assert_protected

    def setUp(self):
        base_fixture.VmuOnlyInstallTests.setUp(self)
        # Replace the old SCI/SCIF package fixture with three distinct SCI
        # policy modules. Runtime bytes are synthetic, never retail/save data.
        shutil.rmtree(self.package/'variants')
        self.metadata.pop('lease_bytes')
        self.metadata.update(schema=tool.SCHEMA, variants={})
        for mode, geometry in tool.CONTRACTS.items():
            old, manifest = generic(self.package/'variants'/mode)
            payload = old[64:]+mode.encode().ljust(16, b' ')
            module = envelope(payload, len(payload), manifest['build'])
            manifest.update(module_bytes=len(module), module_sha256=p.sha(module))
            (self.package/'variants'/mode/p.BASE_MODULE).write_bytes(module)
            (self.package/'variants'/mode/p.MANIFEST).write_text(json.dumps(manifest))
            self.metadata['variants'][mode] = dict(directory='variants/'+mode,
                **geometry, module_sha256=p.sha(module))
        self.metadata_write()
        shutil.copyfile(ROOT/'tools/install_native_vmu_only.py', self.package/'tools/install_native_vmu_only.py')
        shutil.copyfile(ROOT/'tools/install_skies_read_fix.py', self.package/'install.py')
        (self.card/tool.DESTINATION).write_bytes(module_bytes(b'prior accepted72KiB loader', '76b53dcc9464'))
        for namespace in OLD_NAMESPACES:
            directory = self.card/'KUI'/namespace;directory.mkdir(parents=True)
            (directory/'state.json').write_bytes(b'preserve old state '+namespace.encode())
            (directory/'backup').mkdir()
            (directory/'backup/original.kui').write_bytes(b'preserve original '+namespace.encode())
            (directory/'backup/manifest.json').write_bytes(b'preserve backup manifest '+namespace.encode())
        self.original = self.snapshot()

    def metadata_write(self):
        (self.package/'skies-read-fix.json').write_text(json.dumps(self.metadata))

    def run_mode(self, mode=None, restore=False):
        return tool.install(self.card, self.package, mode, self.profile.relative_to(self.card), restore)

    def test_default_is_paced_72k_split(self):
        result = self.run_mode()
        self.assertEqual((result['mode'], result['sci_rx_policy'], result['lease_bytes'], result['controller_submit']),
                         ('paced', 'paced', 73728, 'split'))
        self.assert_protected()
        self.run_mode(restore=True)

    def test_three_real_policy_switches_retain_same_profile_and_exact_original(self):
        installed = {}
        for mode in ('paced', 'pio', 'unpaced', 'paced'):
            result = self.run_mode(mode)
            actual = (self.card/tool.DESTINATION).read_bytes()
            self.assertEqual((result['sci_rx_policy'], result['lease_bytes'], result['controller_submit']),
                             (mode, 73728, 'split'))
            self.assertEqual(result['profile'], str(self.profile.relative_to(self.card)))
            manifest = json.loads((self.package/'variants'/mode/p.MANIFEST).read_text())
            offset = manifest['profile_module_offset']
            self.assertEqual(actual[offset:offset+4096], self.profile.read_bytes())
            if mode in installed:self.assertEqual(actual, installed[mode])
            installed[mode] = actual
            self.assert_protected()
        self.assertEqual(len(set(installed.values())), 3)
        restored = self.run_mode(restore=True)
        self.assertNotIn('lease_bytes', restored)
        self.assertNotIn('sci_rx_policy', restored)
        self.assertEqual(restored['build'], '76b53dcc9464')
        self.assertEqual((self.card/tool.DESTINATION).read_bytes(), self.original[str(tool.DESTINATION)])
        self.assert_protected()
        self.assertEqual(original_core.PARENT, Path('KUI/native-vmu-only'))
        self.assertNotIn(tool.PARENT.name, OLD_NAMESPACES)

    def test_all_three_policies_share_stable_whole_metadata_hash(self):
        hashes = []
        for mode in tool.CONTRACTS:
            core = tool.load_core(self.package)
            _, selected, _, digest = tool.load_package(core, self.package, mode)
            self.assertEqual(selected['lease_bytes'], 73728)
            hashes.append(digest)
        self.assertEqual(len(set(hashes)), 1)
        self.assertEqual(hashes[0], core.sha(core.wire(self.metadata)))

    def test_nonselected_policy_contract_mutation_refuses_before_writes(self):
        for key, value in (('sci_rx_policy', 'paced'), ('lease_bytes', 114688),
                           ('controller_submit', 'full'), ('transport', 0)):
            with self.subTest(key=key):
                original = copy.deepcopy(self.metadata)
                self.metadata['variants']['unpaced'][key] = value
                self.metadata_write()
                with self.assertRaises(ValueError):self.run_mode('paced')
                self.assertEqual(self.snapshot(), self.original)
                self.assertFalse((self.card/tool.PARENT).exists())
                self.metadata = original;self.metadata_write()

    def test_nonselected_module_tamper_refuses_before_backup(self):
        path = self.package/'variants/unpaced'/p.BASE_MODULE
        raw = path.read_bytes();path.write_bytes(raw[:-1]+bytes([raw[-1]^1]))
        with self.assertRaises(ValueError):self.run_mode()
        self.assertEqual(self.snapshot(), self.original)
        self.assertFalse((self.card/tool.PARENT).exists())

    def test_wrong_build_or_source_refuses_before_backup(self):
        original = copy.deepcopy(self.metadata)
        for key, value in (('source_commit', 'd'*40), ('build', 'd'*12)):
            self.metadata[key] = value;self.metadata_write()
            with self.assertRaises(ValueError):self.run_mode()
            self.assertEqual(self.snapshot(), self.original)
            self.metadata = copy.deepcopy(original);self.metadata_write()

    def test_repeated_modes_and_restore_do_not_publish(self):
        for mode, restore in (('paced', False), ('pio', False), ('unpaced', False), (None, True)):
            self.run_mode(mode, restore)
            core = tool.load_core(self.package)
            with patch.object(tool, 'load_core', return_value=core), \
                 patch.object(core, 'publish', side_effect=AssertionError('Unexpected write')):
                self.assertTrue(self.run_mode(mode, restore)['already_installed'])

    def test_late_profile_path_swap_refuses_target_publication(self):
        core = tool.load_core(self.package);publish = core.publish;injected = False
        outside = self.root/'profile-outside';outside.write_bytes(self.profile.read_bytes())
        def swap_once(path, data):
            nonlocal injected
            publish(path, data)
            if path == self.card/tool.STATE and not injected:
                injected = True;self.profile.unlink();self.profile.symlink_to(outside)
        with patch.object(tool, 'load_core', return_value=core), patch.object(core, 'publish', side_effect=swap_once):
            with self.assertRaisesRegex(ValueError, 'ordinary'):self.run_mode('pio')
        self.assertEqual((self.card/tool.DESTINATION).read_bytes(), self.original[str(tool.DESTINATION)])
        for namespace in OLD_NAMESPACES:
            self.assertEqual((self.card/'KUI'/namespace/'state.json').read_bytes(),
                             self.original['KUI/'+namespace+'/state.json'])
        self.run_mode(restore=True)

    def test_failure_after_target_rename_rolls_back_current_policy(self):
        self.run_mode('paced');before = self.snapshot()
        core = tool.load_core(self.package);publish = core.publish;injected = False
        def fail_once(path, data):
            nonlocal injected
            publish(path, data)
            if path == self.card/tool.DESTINATION and not injected:
                injected = True;raise OSError('failure after rename')
        with patch.object(tool, 'load_core', return_value=core), patch.object(core, 'publish', side_effect=fail_once):
            with self.assertRaisesRegex(OSError, 'after rename'):self.run_mode('pio')
        self.assertEqual(self.snapshot(), before)

    def test_external_loader_update_is_preserved(self):
        self.run_mode()
        (self.card/tool.DESTINATION).write_bytes(module_bytes(b'external loader update', 'abcdefabcdef'))
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'outside this test'):self.run_mode(restore=True)
        self.assertEqual(self.snapshot(), before)

    def test_receipt_without_backup_refuses_before_mutation(self):
        directory = self.card/tool.PARENT;directory.mkdir()
        (directory/'state.json').write_text('{}')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'no exact loader backup'):self.run_mode()
        self.assertEqual(self.snapshot(), before)

    def test_extract_cli_default_pio_unpaced_and_restore(self):
        for arguments in ((), ('--mode','pio'), ('--mode','unpaced'), ('--mode','paced'), ('--restore',)):
            run = subprocess.run([sys.executable,str(self.package/'install.py'),str(self.card),*arguments,
                '--profile',str(self.profile.relative_to(self.card))],text=True,capture_output=True,cwd=self.root)
            self.assertEqual(run.returncode,0,run.stderr)
            report = json.JSONDecoder().raw_decode(run.stdout)[0]
            if not arguments:self.assertEqual((report['mode'],report['lease_bytes']),('paced',73728))
        self.assertEqual((self.card/tool.DESTINATION).read_bytes(), self.original[str(tool.DESTINATION)])
        self.assert_protected()
        self.assertEqual((self.card/'KUI/runtime.kui').read_bytes(), self.original['KUI/runtime.kui'])


if __name__ == '__main__':unittest.main()
