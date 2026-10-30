#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extracted-style CLI, exact profile preservation and two-file recovery."""
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
import vmu_persist_profile as codec

spec = importlib.util.spec_from_file_location('toggle_installer', ROOT/'tools/install_vmu_toggle.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def module(profile, build, capable=False, mode=None):
    payload = (b'A'*32+(installer.MODE if mode is None else mode)+b'B'*32+profile+b'C'*32+
               (installer.CAPABILITY if capable else bytes(32))+bytes(32))
    return envelope(payload, len(payload)+4096, build)


class ToggleInstaller(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='kui-toggle-install-')
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.card, self.package = self.work/'card', self.work/'package'
        self.old_gui = envelope(b'old-ui!!', 4096, '1488aa9a5609')
        self.new_gui = envelope(b'new-ui!!', 8192, '0123456789ab')
        self.foreign = envelope(b'other-ui', 4096, 'abcdefabcdef')
        p = codec.decode_profile(codec.template_profile())
        p['cid'] = bytes(range(1, 17)).hex()
        p['pair_path'] = '/KUI/vmu-test/p0005'
        self.profile = codec.encode_profile(p)
        self.old_dual = module(self.profile, installer.REQUIRED_DUAL_BUILD)
        self.generic = module(codec.template_profile(), '0123456789ab', True)
        self.new_dual = installer.personalize(self.generic, self.old_dual, codec)
        (self.card/'KUI/apps/games').mkdir(parents=True)
        for directory in ('gui', 'dual', 'tools'):
            (self.package/directory).mkdir(parents=True)
        (self.card/installer.FILES['gui']).write_bytes(self.old_gui)
        (self.card/installer.FILES['dual']).write_bytes(self.old_dual)
        (self.package/'gui/runtime.kui').write_bytes(self.new_gui)
        (self.package/'dual/retail-native-dual.kui').write_bytes(self.generic)
        shutil.copyfile(ROOT/'tools/vmu_persist_profile.py', self.package/installer.CODEC)
        shutil.copyfile(ROOT/'tools/install_vmu_toggle.py', self.package/'install.py')
        self.metadata = dict(schema=installer.SCHEMA, build='0123456789ab',
            runtime_sha256=installer.sha(self.new_gui), dual_sha256=installer.sha(self.generic),
            profile_codec_sha256=installer.sha((self.package/installer.CODEC).read_bytes()),
            required_dual_build=installer.REQUIRED_DUAL_BUILD)
        self.save_metadata()
        for name in ('KUI/native-general/backup/gui.kui', 'KUI/native-general/state.json',
                     'KUI/vmu-test/p0005/card.vmd', 'KUI/vmu-test/p0005/card.journal',
                     'KUI/vmu-test/p0005/identity.bin',
                     'KUI/vmu-test/p0005/retail-persistent-profile-0005.bin',
                     'KUI/vmu-test/assign-a.bin', 'KUI/vmu-test/assign-b.bin',
                     'KUI/apps/games/launch-settings-a.bin', 'KUI/apps/games/retail-boot.kui',
                     'KUI/apps/games/retail-native-standalone.kui',
                     'KUI/vmu-wizard/checkpoints/1488aa9a5609/state.json',
                     'KUI/vmu/emulation-a.bin'):
            path = self.card/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('protected '+name).encode())
        self.protected = self.snapshot()

    def save_metadata(self):
        (self.package/'toggle.json').write_bytes(installer.wire(self.metadata))

    def snapshot(self):
        return {str(path.relative_to(self.card)): path.read_bytes() for path in self.card.rglob('*')
                if path.is_file() and path.relative_to(self.card) not in installer.FILES.values() and
                not str(path.relative_to(self.card)).startswith('KUI/vmu-toggle/')}

    def pair(self):
        return {name: (self.card/path).read_bytes() for name, path in installer.FILES.items()}

    def checkpoint(self):
        return self.card/installer.PARENT/'checkpoints/0123456789ab'

    def cli(self, *args, passed=True):
        result = subprocess.run([sys.executable, str(self.package/'install.py'), str(self.card), *args],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, passed, result.stdout+result.stderr)
        self.assertEqual(self.snapshot(), self.protected)
        return result

    def test_install_reinstall_exact_restore_and_profile_preservation(self):
        self.cli()
        self.assertEqual(self.pair(), {'gui': self.new_gui, 'dual': self.new_dual})
        at, p = installer.profile_slot(self.new_dual, codec)
        self.assertEqual(self.new_dual[at:at+4096], self.profile)
        self.assertEqual(p['pair_path'], '/KUI/vmu-test/p0005')
        receipt = (self.checkpoint()/'state.json').read_bytes()
        self.assertIn('"changed": []', self.cli().stdout)
        self.assertEqual((self.checkpoint()/'state.json').read_bytes(), receipt)
        self.cli('--restore')
        self.assertEqual(self.pair(), {'gui': self.old_gui, 'dual': self.old_dual})
        self.assertIn('"changed": []', self.cli('--restore').stdout)
        self.cli()
        self.assertEqual(self.pair(), {'gui': self.new_gui, 'dual': self.new_dual})

    def test_newer_external_ui_is_exact_predecessor(self):
        (self.card/installer.FILES['gui']).write_bytes(self.foreign)
        self.cli()
        self.cli('--restore')
        self.assertEqual(self.pair(), {'gui': self.foreign, 'dual': self.old_dual})

    def test_later_external_changes_block_install_and_restore_before_either_write(self):
        for name in installer.FILES:
            with self.subTest(file=name):
                self.cli()
                (self.card/installer.FILES[name]).write_bytes(self.foreign)
                before = self.pair()
                self.cli(passed=False)
                self.cli('--restore', passed=False)
                self.assertEqual(self.pair(), before)
                (self.card/installer.FILES[name]).write_bytes({'gui': self.new_gui, 'dual': self.new_dual}[name])
                self.cli('--restore')

    def test_bad_predecessor_profile_and_mode_are_refused(self):
        bad_mode = bytearray(installer.MODE)
        bad_mode[16] = 2
        for previous in (module(codec.template_profile(), installer.REQUIRED_DUAL_BUILD),
                         module(self.profile, 'abcdefabcdef'),
                         module(self.profile, installer.REQUIRED_DUAL_BUILD, mode=bad_mode),
                         envelope(self.old_dual[64:]+self.profile, len(self.old_dual)+4096, installer.REQUIRED_DUAL_BUILD)):
            with self.subTest(previous=installer.sha(previous)):
                (self.card/installer.FILES['dual']).write_bytes(previous)
                self.cli(passed=False)
                self.assertEqual(self.pair(), {'gui': self.old_gui, 'dual': previous})
        self.assertFalse(self.checkpoint().exists())

    def test_invalid_package_payload_or_codec_is_refused(self):
        for relative in ('gui/runtime.kui', 'dual/retail-native-dual.kui', installer.CODEC):
            with self.subTest(file=relative):
                path = self.package/relative
                original = path.read_bytes()
                path.write_bytes(original+b'corrupt')
                self.cli(passed=False)
                self.assertEqual(self.pair(), {'gui': self.old_gui, 'dual': self.old_dual})
                path.write_bytes(original)
        self.assertFalse(self.checkpoint().exists())

    def test_new_module_requires_capability_and_template(self):
        for value in (module(codec.template_profile(), '0123456789ab'),
                      module(self.profile, '0123456789ab', True)):
            (self.package/'dual/retail-native-dual.kui').write_bytes(value)
            self.metadata['dual_sha256'] = installer.sha(value)
            self.save_metadata()
            self.cli(passed=False)
            self.assertEqual(self.pair(), {'gui': self.old_gui, 'dual': self.old_dual})

    def test_raw_marker_constants_do_not_hide_unique_valid_records(self):
        extra = b'KUINVM01'+bytes(24)+b'KUIVMUP2'+bytes(24)
        previous = envelope(self.old_dual[64:]+extra, len(self.old_dual)+4096, installer.REQUIRED_DUAL_BUILD)
        result = installer.personalize(self.generic, previous, codec)
        self.assertEqual(result, self.new_dual)

    def test_ambiguous_capability_is_refused(self):
        value = envelope(self.generic[64:]+installer.CAPABILITY, len(self.generic)+4096, '0123456789ab')
        (self.package/'dual/retail-native-dual.kui').write_bytes(value)
        self.metadata['dual_sha256'] = installer.sha(value)
        self.save_metadata()
        self.cli(passed=False)

    def test_symlink_targets_and_root_are_refused(self):
        for name, relative in installer.FILES.items():
            path = self.card/relative
            original = path.read_bytes()
            outside = self.work/(name+'.kui')
            outside.write_bytes(original)
            path.unlink()
            path.symlink_to(outside)
            self.cli(passed=False)
            self.assertEqual(outside.read_bytes(), original)
            path.unlink()
            path.write_bytes(original)
        alias = self.work/'alias'
        alias.symlink_to(self.card, target_is_directory=True)
        with self.assertRaises(ValueError):
            installer.run(alias, self.package)

    def test_symlink_checkpoint_and_broken_checkpoint_are_refused(self):
        self.cli()
        checkpoint = self.checkpoint()
        moved = self.work/'checkpoint'
        checkpoint.rename(moved)
        checkpoint.symlink_to(moved, target_is_directory=True)
        self.cli('--restore', passed=False)
        checkpoint.unlink()
        moved.rename(checkpoint)
        (checkpoint/'dual-before.kui').write_bytes(self.foreign)
        self.cli('--restore', passed=False)
        self.assertEqual(self.pair(), {'gui': self.new_gui, 'dual': self.new_dual})

    def test_restore_without_checkpoint(self):
        self.cli('--restore', passed=False)
        self.assertEqual(self.pair(), {'gui': self.old_gui, 'dual': self.old_dual})

    def interrupt(self, wanted, after=True, restore=False):
        real = installer.publish
        def interrupted(path, data, root):
            selected = wanted(path, data)
            if selected and not after:
                raise OSError('injected interruption before publication')
            real(path, data, root)
            if selected and after:
                raise OSError('injected interruption after publication')
        with mock.patch.object(installer, 'publish', interrupted):
            with self.assertRaises(OSError):
                installer.run(self.card, self.package, restore)
        self.assertEqual(self.snapshot(), self.protected)

    def test_interrupted_checkpoint_leaves_both_originals(self):
        self.interrupt(lambda path, data: path.name == 'gui-before.kui')
        self.assertEqual(self.pair(), {'gui': self.old_gui, 'dual': self.old_dual})
        self.assertFalse(self.checkpoint().exists())
        self.cli()

    def test_interrupted_staging_leaves_both_originals(self):
        self.interrupt(lambda path, data: path.name == 'dual.kui')
        self.assertEqual(self.pair(), {'gui': self.old_gui, 'dual': self.old_dual})
        self.assertFalse((self.checkpoint()/'state.json').exists())
        self.cli()

    def test_interruption_after_pending_can_finish(self):
        self.interrupt(lambda path, data: path.name == 'state.json' and json.loads(data)['pending'])
        self.assertEqual(self.pair(), {'gui': self.old_gui, 'dual': self.old_dual})
        self.cli()

    def test_interruption_after_first_target_can_finish_or_restore(self):
        self.interrupt(lambda path, data: path == self.card/installer.FILES['gui'])
        self.assertEqual(self.pair(), {'gui': self.new_gui, 'dual': self.old_dual})
        self.cli('--restore')
        self.assertEqual(self.pair(), {'gui': self.old_gui, 'dual': self.old_dual})
        self.interrupt(lambda path, data: path == self.card/installer.FILES['gui'])
        self.cli()
        self.assertEqual(self.pair(), {'gui': self.new_gui, 'dual': self.new_dual})

    def test_interruption_after_second_target_can_restore(self):
        self.interrupt(lambda path, data: path == self.card/installer.FILES['dual'])
        self.assertEqual(self.pair(), {'gui': self.new_gui, 'dual': self.new_dual})
        self.cli('--restore')
        self.assertEqual(self.pair(), {'gui': self.old_gui, 'dual': self.old_dual})

    def test_final_receipt_failure_can_finish(self):
        self.interrupt(lambda path, data: path.name == 'state.json' and not json.loads(data)['pending'], False)
        self.cli()
        self.assertFalse(json.loads((self.checkpoint()/'state.json').read_bytes())['pending'])

    def test_interrupted_restore_can_resume_or_reinstall(self):
        self.cli()
        self.interrupt(lambda path, data: path == self.card/installer.FILES['gui'], restore=True)
        self.assertEqual(self.pair(), {'gui': self.old_gui, 'dual': self.new_dual})
        self.cli()
        self.assertEqual(self.pair(), {'gui': self.new_gui, 'dual': self.new_dual})
        self.interrupt(lambda path, data: path == self.card/installer.FILES['gui'], restore=True)
        self.cli('--restore')
        self.assertEqual(self.pair(), {'gui': self.old_gui, 'dual': self.old_dual})

    def test_external_mutation_after_pending_is_preserved(self):
        real = installer.publish
        def changed(path, data, root):
            real(path, data, root)
            if path.name == 'state.json' and json.loads(data)['pending']:
                (self.card/installer.FILES['dual']).write_bytes(self.foreign)
        with mock.patch.object(installer, 'publish', changed):
            with self.assertRaises(ValueError):
                installer.run(self.card, self.package)
        self.assertEqual(self.pair(), {'gui': self.old_gui, 'dual': self.foreign})
        self.cli('--restore', passed=False)

    def test_external_mutation_between_target_publications_is_preserved(self):
        real = installer.publish
        def changed(path, data, root):
            real(path, data, root)
            if path == self.card/installer.FILES['gui']:
                (self.card/installer.FILES['dual']).write_bytes(self.foreign)
        with mock.patch.object(installer, 'publish', changed):
            with self.assertRaises(ValueError):
                installer.run(self.card, self.package)
        self.assertEqual(self.pair(), {'gui': self.new_gui, 'dual': self.foreign})
        self.cli('--restore', passed=False)


if __name__ == '__main__':
    unittest.main()
