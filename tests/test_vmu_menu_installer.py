#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""GUI-only CLI, capability admission and exact interrupted-upgrade recovery."""
import fcntl
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
import package_vmu_menu as packager

spec = importlib.util.spec_from_file_location('menu_installer', ROOT/'tools/install_vmu_menu.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def native(profile, build='6096398d3373', *, capable=True, mode=None):
    payload = (b'A'*32+(installer.MODE if mode is None else mode)+b'B'*32+profile+b'C'*32+
               (installer.CAPABILITY if capable else bytes(32))+bytes(32))
    return envelope(payload, len(payload)+4096, build)


class MenuInstaller(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='kui-menu-install-')
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.card, self.package = self.work/'card', self.work/'package'
        self.old = envelope(b'old-ui!!', 4096, 'e506b571d3a8')
        self.new = envelope(b'new-ui!!', 8192, '0123456789ab')
        self.foreign = envelope(b'other-ui', 4096, 'abcdefabcdef')
        p = codec.decode_profile(codec.template_profile())
        p['cid'] = bytes(range(1, 17)).hex()
        p['pair_path'] = '/KUI/vmu-test/p0005'
        self.profile = codec.encode_profile(p)
        self.dual = native(self.profile)
        (self.card/'KUI/apps/games').mkdir(parents=True)
        (self.package/'gui').mkdir(parents=True)
        (self.package/'tools').mkdir()
        (self.card/installer.RUNTIME).write_bytes(self.old)
        (self.card/installer.DUAL).write_bytes(self.dual)
        (self.package/'gui/runtime.kui').write_bytes(self.new)
        shutil.copyfile(ROOT/'tools/vmu_persist_profile.py', self.package/installer.CODEC)
        shutil.copyfile(ROOT/'tools/install_vmu_menu.py', self.package/'install.py')
        self.metadata = dict(schema=installer.SCHEMA, build='0123456789ab',
                             runtime_sha256=installer.sha(self.new),
                             profile_codec_sha256=installer.sha((self.package/installer.CODEC).read_bytes()),
                             native_runtime_unchanged=True)
        self.save_metadata()
        for name in ('KUI/native-general/backup/gui.kui', 'KUI/native-general/state.json',
                     'KUI/vmu-toggle/checkpoints/6096398d3373/state.json',
                     'KUI/vmu-wizard/checkpoints/1488aa9a5609/state.json',
                     'KUI/vmu-test/p0005/card.vmd', 'KUI/vmu-test/p0005/card.journal',
                     'KUI/vmu-test/p0005/identity.bin',
                     'KUI/vmu-test/p0005/retail-persistent-profile-0005.bin',
                     'KUI/vmu-test/p0005/name-a.bin', 'KUI/vmu/cards/v0001/name-a.bin',
                     'KUI/vmu-test/assign-a.bin', 'KUI/vmu-test/assign-b.bin',
                     'KUI/apps/games/launch-settings-a.bin', 'KUI/apps/games/retail-boot.kui',
                     'KUI/apps/games/retail-native-standalone.kui',
                     'KUI/vmu/emulation-a.bin', 'KUI/vmu/emulation-b.bin'):
            path = self.card/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('protected '+name).encode())
        self.protected = self.snapshot()

    def save_metadata(self):
        (self.package/'menu.json').write_bytes(installer.wire(self.metadata))

    def snapshot(self):
        return {str(path.relative_to(self.card)): path.read_bytes() for path in self.card.rglob('*')
                if path.is_file() and path.relative_to(self.card) != installer.RUNTIME and
                not str(path.relative_to(self.card)).startswith('KUI/vmu-menu/')}

    def gui(self):
        return (self.card/installer.RUNTIME).read_bytes()

    def checkpoint(self):
        return self.card/installer.PARENT/'checkpoints/0123456789ab'

    def cli(self, *args, passed=True):
        result = subprocess.run([sys.executable, str(self.package/'install.py'), str(self.card), *args],
                                text=True, capture_output=True)
        self.assertEqual(result.returncode == 0, passed, result.stdout+result.stderr)
        self.assertEqual(self.snapshot(), self.protected)
        return result

    def test_install_reinstall_and_restore_exact_gui(self):
        result = self.cli()
        self.assertIn('GUI update installed.', result.stdout)
        self.assertEqual(self.gui(), self.new)
        backup = json.loads((self.checkpoint()/'backup.json').read_bytes())
        self.assertEqual(backup['native_sha256'], installer.native_hashes(installer.native_files(self.card)))
        self.assertEqual((self.checkpoint()/'runtime-before.kui').read_bytes(), self.old)
        receipt = (self.checkpoint()/'state.json').read_bytes()
        with mock.patch.object(installer, 'publish', side_effect=AssertionError('idempotent run wrote')):
            self.assertEqual(installer.run(self.card, self.package)['changed'], [])
        self.assertEqual((self.checkpoint()/'state.json').read_bytes(), receipt)
        self.cli('--restore')
        self.assertEqual(self.gui(), self.old)
        with mock.patch.object(installer, 'publish', side_effect=AssertionError('idempotent restore wrote')):
            self.assertEqual(installer.run(self.card, self.package, True)['changed'], [])
        self.cli()
        self.assertEqual(self.gui(), self.new)

    def test_any_initial_valid_ripper_gui_is_checkpointed(self):
        (self.card/installer.RUNTIME).write_bytes(self.foreign)
        self.cli()
        self.cli('--restore')
        self.assertEqual(self.gui(), self.foreign)

    def test_later_gui_changes_are_preserved(self):
        self.cli()
        (self.card/installer.RUNTIME).write_bytes(self.foreign)
        for args in ((), ('--restore',)):
            self.assertIn('changed outside this menu update', self.cli(*args, passed=False).stderr)
            self.assertEqual(self.gui(), self.foreign)

    def test_later_native_changes_are_preserved(self):
        self.cli()
        for role, relative in installer.NATIVE_FILES.items():
            with self.subTest(role=role):
                old = (self.card/relative).read_bytes()
                foreign = native(self.profile, 'abcdefabcdef') if role == 'dual' else self.foreign
                (self.card/relative).write_bytes(foreign)
                self.protected = self.snapshot()
                for args in ((), ('--restore',)):
                    self.cli(*args, passed=False)
                    self.assertEqual(self.gui(), self.new)
                    self.assertEqual((self.card/relative).read_bytes(), foreign)
                (self.card/relative).write_bytes(old)
                self.protected = self.snapshot()

    def test_optional_native_absence_is_pinned(self):
        (self.card/installer.STANDALONE).unlink()
        self.protected = self.snapshot()
        self.cli()
        (self.card/installer.STANDALONE).write_bytes(self.foreign)
        self.protected = self.snapshot()
        self.cli('--restore', passed=False)
        self.assertEqual(self.gui(), self.new)

    def test_newer_capability_module_does_not_require_build_allowlist(self):
        dual = native(self.profile, '9876543210ab')
        (self.card/installer.DUAL).write_bytes(dual)
        self.protected = self.snapshot()
        self.assertIn('9876543210ab', self.cli().stdout)
        self.cli('--restore')
        self.assertEqual((self.card/installer.DUAL).read_bytes(), dual)

    def test_native_capability_mode_profile_and_crc_refuse_before_checkpoint(self):
        bad_mode = bytearray(installer.MODE)
        bad_mode[16] = 7
        broken = bytearray(self.dual)
        broken[-1] ^= 1
        variants = (native(self.profile, capable=False), native(codec.template_profile()),
                    native(self.profile, mode=bad_mode), bytes(broken), self.foreign)
        for dual in variants:
            with self.subTest(sha=installer.sha(dual)):
                (self.card/installer.DUAL).write_bytes(dual)
                self.protected = self.snapshot()
                self.cli(passed=False)
                self.assertEqual(self.gui(), self.old)
                self.assertFalse((self.card/installer.PARENT).exists())

    def test_missing_native_module_refuses(self):
        (self.card/installer.DUAL).unlink()
        self.protected = self.snapshot()
        self.cli(passed=False)
        self.assertEqual(self.gui(), self.old)
        self.assertFalse((self.card/installer.PARENT).exists())

    def test_ambiguous_capability_and_profile_refuse(self):
        for extra in (installer.CAPABILITY, self.profile):
            previous = envelope(self.dual[64:]+extra, len(self.dual)+8192, '6096398d3373')
            (self.card/installer.DUAL).write_bytes(previous)
            self.protected = self.snapshot()
            self.cli(passed=False)
            self.assertEqual(self.gui(), self.old)
            self.assertFalse((self.card/installer.PARENT).exists())

    def test_invalid_raw_profile_marker_does_not_hide_valid_profile(self):
        previous = envelope(self.dual[64:]+b'KUIVMUP2'+bytes(24), len(self.dual)+8192, '6096398d3373')
        (self.card/installer.DUAL).write_bytes(previous)
        self.protected = self.snapshot()
        self.cli()
        self.cli('--restore')

    def test_corrupt_payload_codec_or_record_refuse(self):
        for relative in ('gui/runtime.kui', installer.CODEC):
            path = self.package/relative
            before = path.read_bytes()
            path.write_bytes(before+b'corrupt')
            self.cli(passed=False)
            self.assertEqual(self.gui(), self.old)
            path.write_bytes(before)
        self.metadata['native_runtime_unchanged'] = False
        self.save_metadata()
        self.cli(passed=False)
        self.assertFalse((self.card/installer.PARENT).exists())

    def test_corrupt_original_or_native_checkpoint_refuse(self):
        self.cli()
        path = self.checkpoint()/'runtime-before.kui'
        old = path.read_bytes()
        path.write_bytes(self.foreign)
        self.cli('--restore', passed=False)
        self.assertEqual(self.gui(), self.new)
        path.write_bytes(old)
        path = self.checkpoint()/'backup.json'
        backup = json.loads(path.read_bytes())
        backup['native_sha256']['dual'] = '0'*64
        path.write_bytes(installer.wire(backup))
        self.cli('--restore', passed=False)
        self.assertEqual(self.gui(), self.new)

    def test_restore_without_checkpoint_refuses(self):
        self.cli('--restore', passed=False)
        self.assertEqual(self.gui(), self.old)

    def test_symlinks_and_root_alias_refuse(self):
        for relative in (installer.RUNTIME, installer.DUAL, installer.STANDALONE):
            path = self.card/relative
            old = path.read_bytes()
            outside = self.work/('outside-'+path.name)
            outside.write_bytes(old)
            path.unlink()
            path.symlink_to(outside)
            self.cli(passed=False)
            self.assertEqual(outside.read_bytes(), old)
            path.unlink()
            path.write_bytes(old)
        alias = self.work/'alias'
        alias.symlink_to(self.card, target_is_directory=True)
        with self.assertRaises(ValueError):
            installer.run(alias, self.package)

    def test_symlink_checkpoint_is_refused(self):
        self.cli()
        moved = self.work/'checkpoint'
        self.checkpoint().rename(moved)
        self.checkpoint().symlink_to(moved, target_is_directory=True)
        self.cli('--restore', passed=False)
        self.assertEqual(self.gui(), self.new)

    def test_concurrent_installer_lock_refuses(self):
        parent = self.card/installer.PARENT
        parent.mkdir(parents=True)
        with (parent/'installer.lock').open('w') as lock:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.assertIn('Another VMU menu installer', self.cli(passed=False).stderr)
        self.assertEqual(self.gui(), self.old)

    def interrupt(self, wanted, *, after=True, restore=False):
        real = installer.publish
        def changed(path, data, root):
            selected = wanted(path, data)
            if selected and not after:
                raise OSError('injected interruption before publication')
            real(path, data, root)
            if selected and after:
                raise OSError('injected interruption after publication')
        with mock.patch.object(installer, 'publish', changed):
            with self.assertRaises(OSError):
                installer.run(self.card, self.package, restore)
        self.assertEqual(self.snapshot(), self.protected)

    def test_incomplete_checkpoint_leaves_old_runtime(self):
        self.interrupt(lambda path, data: path.name == 'runtime-before.kui')
        self.assertFalse(self.checkpoint().exists())
        self.assertEqual(self.gui(), self.old)
        self.cli()

    def test_interruption_after_pending_can_finish(self):
        self.interrupt(lambda path, data: path.name == 'state.json' and json.loads(data)['pending'])
        self.assertEqual(self.gui(), self.old)
        self.cli()
        self.assertEqual(self.gui(), self.new)

    def test_interruption_after_runtime_can_restore(self):
        self.interrupt(lambda path, data: path == self.card/installer.RUNTIME)
        self.assertEqual(self.gui(), self.new)
        self.cli('--restore')
        self.assertEqual(self.gui(), self.old)

    def test_interruption_before_final_receipt_can_finish(self):
        self.interrupt(lambda path, data: path.name == 'state.json' and not json.loads(data)['pending'], after=False)
        self.cli()
        self.assertFalse(json.loads((self.checkpoint()/'state.json').read_bytes())['pending'])

    def test_interrupted_restore_can_resume_or_reinstall(self):
        self.cli()
        self.interrupt(lambda path, data: path == self.card/installer.RUNTIME, restore=True)
        self.assertEqual(self.gui(), self.old)
        self.cli()
        self.assertEqual(self.gui(), self.new)
        self.interrupt(lambda path, data: path == self.card/installer.RUNTIME, restore=True)
        self.cli('--restore')
        self.assertEqual(self.gui(), self.old)

    def test_external_gui_change_after_pending_is_preserved(self):
        real = installer.publish
        def changed(path, data, root):
            real(path, data, root)
            if path.name == 'state.json' and json.loads(data)['pending']:
                (self.card/installer.RUNTIME).write_bytes(self.foreign)
        with mock.patch.object(installer, 'publish', changed):
            with self.assertRaisesRegex(ValueError, 'changed before runtime replacement'):
                installer.run(self.card, self.package)
        self.assertEqual(self.gui(), self.foreign)
        self.cli('--restore', passed=False)

    def test_external_native_change_after_pending_is_preserved(self):
        real = installer.publish
        foreign = native(self.profile, 'abcdefabcdef')
        def changed(path, data, root):
            real(path, data, root)
            if path.name == 'state.json' and json.loads(data)['pending']:
                (self.card/installer.DUAL).write_bytes(foreign)
        with mock.patch.object(installer, 'publish', changed):
            with self.assertRaisesRegex(ValueError, 'changed before runtime replacement'):
                installer.run(self.card, self.package)
        self.assertEqual(self.gui(), self.old)
        self.assertEqual((self.card/installer.DUAL).read_bytes(), foreign)
        self.protected = self.snapshot()
        self.cli('--restore', passed=False)

    def test_external_native_change_after_gui_replace_is_preserved(self):
        real = installer.publish
        foreign = native(self.profile, 'abcdefabcdef')
        def changed(path, data, root):
            real(path, data, root)
            if path == self.card/installer.RUNTIME:
                (self.card/installer.DUAL).write_bytes(foreign)
        with mock.patch.object(installer, 'publish', changed):
            with self.assertRaisesRegex(ValueError, 'protected native modules changed'):
                installer.run(self.card, self.package)
        self.assertEqual(self.gui(), self.new)
        self.assertEqual((self.card/installer.DUAL).read_bytes(), foreign)
        self.protected = self.snapshot()
        self.cli('--restore', passed=False)

    def test_idempotent_run_still_checks_concurrent_runtime_change(self):
        self.cli()
        real = installer.state_read
        def changed(*args):
            result = real(*args)
            (self.card/installer.RUNTIME).write_bytes(self.foreign)
            return result
        with mock.patch.object(installer, 'state_read', changed):
            with self.assertRaisesRegex(ValueError, 'changed before completion'):
                installer.run(self.card, self.package)
        self.assertEqual(self.gui(), self.foreign)
        self.cli('--restore', passed=False)


class MenuPackager(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='kui-menu-package-')
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.root = self.work/'source'
        (self.root/'tools').mkdir(parents=True)
        (self.root/'docs').mkdir()
        for name in ('install_vmu_menu.py', 'vmu_persist_profile.py'):
            shutil.copyfile(ROOT/'tools'/name, self.root/'tools'/name)
        (self.root/'docs/vmu-menu-test.md').write_text('Build @BUILD@; GUI only.\n')
        (self.root/'LICENSE').write_text('test license\n')
        (self.root/'THIRD_PARTY.md').write_text('test notices\n')
        self.gui = self.work/'runtime.kui'
        self.gui.write_bytes(envelope(b'new-ui!!', 8192, '0123456789ab'))
        self.source = '0123456789ab'+'0'*28
        self.output = self.work/'delivery'

    def invoke(self, status='', name=None):
        # Bundle creation is mocked here; final delivery separately verifies
        # the real complete source bundle and actual compiled GUI.
        def git(*args):
            return self.source if args == ('rev-parse', 'HEAD') else status
        def bundle(arguments, **kwargs):
            Path(arguments[5]).write_bytes(b'mocked source bundle for packaging tests')
            return subprocess.CompletedProcess(arguments, 0)
        args = ['package_vmu_menu.py', '--gui', str(self.gui), '--output', str(self.output)]
        if name is not None:
            args.extend(['--name', name])
        with mock.patch.object(packager, 'ROOT', self.root), mock.patch.object(packager, 'git', git), \
             mock.patch.object(packager.subprocess, 'run', bundle), mock.patch.object(sys, 'argv', args):
            packager.main()

    def test_gui_only_package_manifest_covers_every_member(self):
        import zipfile
        for name in (None, 'K-UI-Drive-Speed-Probe'):
            with self.subTest(name=name):
                prefix = name or 'K-UI-GUI-Update'
                self.output = self.work/prefix
                self.invoke(name=name)
                archive = next(self.output.glob('*.zip'))
                self.assertEqual(archive.name, prefix+'-0123456789ab.zip')
                package = self.work/'extracted'/prefix
                with zipfile.ZipFile(archive) as z:
                    members = set(z.namelist())
                    self.assertIn('install.py', members)
                    self.assertIn('README-FIRST.md', members)
                    expected = set()
                    for line in z.read('SHA256SUMS.txt').decode().splitlines():
                        digest, member = line.split('  ', 1)
                        self.assertEqual(installer.sha(z.read(member)), digest)
                        expected.add(member)
                    self.assertEqual(members, expected|{'SHA256SUMS.txt'})
                    self.assertFalse(any('retail-native' in member for member in members))
                    metadata = json.loads(z.read('menu.json'))
                    self.assertEqual(metadata['replaces'], ['KUI/runtime.kui'])
                    self.assertTrue(metadata['native_runtime_unchanged'])
                    self.assertFalse(metadata['console_compatibility_verified'])
                    self.assertEqual(metadata['source_commit'], self.source)
                    z.extractall(package)
                _, record, gui, _ = installer.package_data(package)
                self.assertEqual(gui, self.gui.read_bytes())
                self.assertEqual(record['build'], '0123456789ab')

    def test_dirty_source_refuses_before_creating_package(self):
        for name in ('../update', '/tmp/update', 'two words', ''):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'Package name'):
                self.invoke(name=name)
            self.assertFalse(self.output.exists())
        with self.assertRaisesRegex(ValueError, 'Commit source changes'):
            self.invoke(' M src/example.c')
        self.assertFalse(self.output.exists())

    def test_mismatched_gui_source_label_refuses(self):
        self.gui.write_bytes(envelope(b'other-ui', 8192, 'abcdefabcdef'))
        with self.assertRaisesRegex(ValueError, 'differs from committed source'):
            self.invoke()
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
