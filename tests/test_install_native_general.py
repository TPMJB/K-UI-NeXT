#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install/restore the general candidate on a temporary card; no console bytes."""
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
sys.path.insert(0, str(ROOT/'tests'))
import install_native_general as installer
import personalize_vmu_native_persistent as personalizer
import package_native_general as packager
from runtime_package import envelope
from test_personalize_vmu_native_persistent import generic, pair, COMMIT


def sha(data):
    return hashlib.sha256(data).hexdigest()


class GeneralInstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        self.package, self.card = base/'package', base/'card'
        self.package.mkdir()
        self.card.mkdir()
        self.module, _ = generic(self.package/'dual')
        self.gui = envelope(b'\x09\x00'*96, 192, COMMIT[:12])
        self.standalone = envelope(b'\x0b\x00'*80, 160, COMMIT[:12])
        for relative, data in (('gui/runtime.kui', self.gui),
                               ('standalone/retail-native-standalone.kui', self.standalone)):
            (self.package/relative).parent.mkdir(parents=True, exist_ok=True)
            (self.package/relative).write_bytes(data)
        (self.package/'tools').mkdir()
        for name in ('runtime_package.py', 'vmu_persist_profile.py', 'vmu_native_mode.py', 'vmu_native_cache_seed.py'):
            shutil.copyfile(ROOT/'tools'/name, self.package/'tools'/name)
        shutil.copyfile(ROOT/'tools/personalize_vmu_native_persistent.py', self.package/'personalize.py')
        shutil.copyfile(ROOT/'tools/install_native_general.py', self.package/'install.py')
        self.pilot = b'DOA2 pilot module bytes'
        self.metadata = dict(schema=installer.SCHEMA, build=COMMIT[:12], source_commit=COMMIT,
                             gui=dict(build=COMMIT[:12], sha256=sha(self.gui)),
                             dual=dict(build=COMMIT[:12], sha256=sha(self.module)),
                             standalone=dict(build=COMMIT[:12], sha256=sha(self.standalone)),
                             refuse_ordinary_sha256=[sha(self.pilot)])
        self.write_metadata()
        self.profile = pair(self.card, 'p0005', number='0005')
        games = self.card/'KUI/apps/games'
        games.mkdir(parents=True)
        (self.card/installer.FILES['gui']).write_bytes(b'8adf Games UI')
        (self.card/installer.FILES['dual']).write_bytes(b'personalized 6be dual module')
        (self.card/installer.ORDINARY).write_bytes(b'ordinary retail-boot module')
        (games/'launch-settings-a.bin').write_bytes(b'existing launch choices')
        self.before = self.snapshot()

    def write_metadata(self):
        (self.package/'general.json').write_text(json.dumps(self.metadata))

    def snapshot(self):
        return {p.relative_to(self.card).as_posix(): p.read_bytes()
                for p in self.card.rglob('*') if p.is_file()}

    def run_install(self, restore=False, checkpoint=None):
        return installer.run(self.card, self.package, restore,
                             None if restore else self.profile.relative_to(self.card), checkpoint)

    def prepare_predecessor(self):
        self.run_install()
        predecessor = envelope(b'\x0d\x00'*112, 224, '26a9b2193556')
        (self.card/installer.FILES['gui']).write_bytes(predecessor)
        self.metadata['accepted_runtime_predecessors'] = [dict(build='26a9b2193556', sha256=sha(predecessor))]
        self.write_metadata()
        return predecessor

    def checkpoint_members(self):
        return [(str(path), (self.card/path).read_bytes(), tarfile.REGTYPE)
                for path in (*installer.FILES.values(), installer.STATE)]

    def checkpoint(self, members=None, mode='w'):
        path = Path(self.temp.name)/'before.tar'
        with tarfile.open(path, mode) as archive:
            for name, data, kind in members if members is not None else self.checkpoint_members():
                info = tarfile.TarInfo(name)
                info.type = kind
                info.size = len(data) if kind in (tarfile.REGTYPE, tarfile.AREGTYPE) else 0
                if kind in (tarfile.SYMTYPE, tarfile.LNKTYPE):
                    info.linkname = 'KUI/runtime.kui'
                archive.addfile(info, io.BytesIO(data) if info.size else None)
        return path

    def assert_protected(self):
        now = self.snapshot()
        for name in ('KUI/apps/games/retail-boot.kui', 'KUI/apps/games/launch-settings-a.bin',
                     'KUI/vmu-test/p0005/retail-persistent-profile-0005.bin', 'KUI/vmu-test/p0005/card.vmd',
                     'KUI/vmu-test/p0005/card.journal', 'KUI/vmu-test/p0005/identity.bin'):
            self.assertEqual(now[name], self.before[name], name)

    def assert_original(self):
        now = self.snapshot()
        for name, data in self.before.items():
            self.assertEqual(now[name], data, name)
        self.assertNotIn('KUI/apps/games/retail-native-standalone.kui', now)

    def test_install_restore_reinstall_exact(self):
        result = self.run_install()
        self.assertEqual(sorted(result['changed']), sorted(str(p) for p in installer.FILES.values()))
        now = self.snapshot()
        self.assertEqual(now['KUI/runtime.kui'], self.gui)
        self.assertEqual(now['KUI/apps/games/retail-native-standalone.kui'], self.standalone)
        dual = now['KUI/apps/games/retail-native-dual.kui']
        offset = json.loads((self.package/'dual'/personalizer.MANIFEST).read_text())['profile_module_offset']
        self.assertEqual(dual[offset:offset+4096], self.profile.read_bytes())
        self.assertEqual(len(dual), len(self.module))
        self.assert_protected()
        state = json.loads((self.card/installer.STATE).read_text())
        self.assertTrue(state['installed'] and not state['pending'])
        self.assertEqual(self.run_install()['changed'], [])
        self.run_install(restore=True)
        self.assert_original()
        self.assertEqual(self.run_install(restore=True)['changed'], [])
        self.run_install()
        self.assertEqual(self.snapshot()['KUI/apps/games/retail-native-dual.kui'], dual)
        self.run_install(restore=True)
        self.assert_original()

    def test_later_package_replaces_installed_candidate(self):
        """A later candidate installs over an earlier one and back again;
        restore still returns the files from before the first install."""
        self.run_install()
        first = {name: (self.card/path).read_bytes() for name, path in installer.FILES.items()}
        later = Path(self.temp.name)/'later'
        shutil.copytree(self.package, later)
        gui = envelope(b'\x0a\x00'*96, 192, COMMIT[:12])
        standalone = envelope(b'\x0c\x00'*80, 160, COMMIT[:12])
        (later/'gui/runtime.kui').write_bytes(gui)
        (later/'standalone/retail-native-standalone.kui').write_bytes(standalone)
        (later/'general.json').write_text(json.dumps(dict(
            self.metadata, gui=dict(build=COMMIT[:12], sha256=sha(gui)),
            standalone=dict(build=COMMIT[:12], sha256=sha(standalone)))))
        profile = self.profile.relative_to(self.card)
        result = installer.run(self.card, later, False, profile)
        self.assertEqual(sorted(result['changed']),
                         sorted(str(installer.FILES[name]) for name in ('gui', 'standalone')))
        self.assertEqual((self.card/installer.FILES['gui']).read_bytes(), gui)
        self.assertEqual((self.card/installer.FILES['standalone']).read_bytes(), standalone)
        self.assertEqual((self.card/installer.FILES['dual']).read_bytes(), first['dual'])
        self.assert_protected()
        self.run_install()
        self.assertEqual({name: (self.card/path).read_bytes() for name, path in installer.FILES.items()}, first)
        installer.run(self.card, later, False, profile)
        installer.run(self.card, later, True)
        self.assert_original()

    def test_doa2_pilot_refuses_before_writes(self):
        (self.card/installer.ORDINARY).write_bytes(self.pilot)
        self.before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'DOA2 pilot'):
            self.run_install()
        self.assertEqual(self.snapshot(), self.before)

    def test_unrecorded_standalone_and_missing_backup_refuse(self):
        (self.card/installer.FILES['standalone']).write_bytes(b'someone else')
        self.before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'unrecorded'):
            self.run_install()
        with self.assertRaisesRegex(ValueError, 'No general candidate backup'):
            self.run_install(restore=True)
        self.assertEqual(self.snapshot(), self.before)

    def test_outside_change_is_preserved(self):
        self.run_install()
        (self.card/installer.FILES['gui']).write_bytes(b'another Games UI')
        changed = self.snapshot()
        for restore in (False, True):
            with self.assertRaisesRegex(ValueError, 'changed outside'):
                self.run_install(restore)
            self.assertEqual(self.snapshot(), changed)

    def test_known_runtime_requires_exact_checkpoint(self):
        self.prepare_predecessor()
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'exact four-file --runtime-checkpoint'):
            self.run_install()
        self.assertEqual(self.snapshot(), before)

    def test_checkpoint_invalid_runtime_still_refused(self):
        self.prepare_predecessor()
        (self.card/installer.FILES['gui']).write_bytes(b'unreviewed runtime')
        checkpoint = self.checkpoint()
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'runtime header'):
            self.run_install(checkpoint=checkpoint)
        self.assertEqual(self.snapshot(), before)

    def test_newer_runtime_not_in_package_requires_checkpoint(self):
        self.run_install()
        newer = envelope(b'new ripper GUI!!'*8, 128, '79c8e25a9383')
        (self.card/installer.FILES['gui']).write_bytes(newer)
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'exact four-file --runtime-checkpoint'):
            self.run_install()
        self.assertEqual(self.snapshot(), before)
        backup_before = {name: data for name, data in before.items()
                         if name.startswith(str(installer.BACKUP)+'/')}
        checkpoint = self.checkpoint()
        checkpoint_before = checkpoint.read_bytes()
        result = self.run_install(checkpoint=checkpoint)
        self.assertEqual(result['runtime_checkpoint']['sha256'], sha(checkpoint_before))
        self.assertEqual(checkpoint.read_bytes(), checkpoint_before)
        self.assertEqual({name: data for name, data in self.snapshot().items()
                          if name.startswith(str(installer.BACKUP)+'/')}, backup_before)
        self.assertEqual((self.card/installer.FILES['gui']).read_bytes(), self.gui)
        self.assert_protected()
        subprocess.run(['tar', '-xf', str(checkpoint), '-C', str(self.card)], check=True)
        self.assertEqual(self.snapshot(), before)

    def test_checkpoint_rejects_damaged_runtime_envelopes(self):
        self.run_install()
        valid = envelope(b'new ripper GUI!!'*8, 128, '79c8e25a9383')
        damaged = bytearray(valid)
        damaged[-1] ^= 1
        for value in (valid[:-1], bytes(damaged)):
            with self.subTest(value=sha(value)):
                (self.card/installer.FILES['gui']).write_bytes(value)
                checkpoint = self.checkpoint()
                before = self.snapshot()
                with self.assertRaises(ValueError):
                    self.run_install(checkpoint=checkpoint)
                self.assertEqual(self.snapshot(), before)

    def test_checkpoint_cannot_accept_changed_loader(self):
        self.prepare_predecessor()
        for role in ('dual', 'standalone'):
            with self.subTest(role=role):
                path = self.card/installer.FILES[role]
                old = path.read_bytes()
                path.write_bytes(b'unreviewed loader')
                checkpoint = self.checkpoint()
                before = self.snapshot()
                with self.assertRaisesRegex(ValueError, 'changed outside'):
                    self.run_install(checkpoint=checkpoint)
                self.assertEqual(self.snapshot(), before)
                path.write_bytes(old)

    def test_checkpoint_install_and_tar_rollback_preserve_new_saves(self):
        self.prepare_predecessor()
        checkpoint = self.checkpoint()
        checkpoint_bytes = checkpoint.read_bytes()
        before = self.snapshot()
        result = self.run_install(checkpoint=checkpoint)
        self.assertEqual((self.card/installer.FILES['gui']).read_bytes(), self.gui)
        self.assertEqual(result['runtime_checkpoint']['sha256'], sha(checkpoint_bytes))
        self.assertEqual(checkpoint.read_bytes(), checkpoint_bytes)
        self.assert_protected()
        updates = {'KUI/vmu-test/p0005/card.vmd': b'newly saved contents',
                   'KUI/vmu-test/p0005/card.journal': b'newly committed journal',
                   'KUI/game-cards/new-game/card.vmd': b'new game card',
                   'KUI/native-cache/v6/new.nvr': b'new cache'}
        for name, data in updates.items():
            path = self.card/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        subprocess.run(['tar', '-xf', str(checkpoint), '-C', str(self.card)], check=True)
        self.assertEqual(self.snapshot(), dict(before, **updates))
        self.assertEqual(checkpoint.read_bytes(), checkpoint_bytes)

    def test_invalid_checkpoints_refuse_without_writes(self):
        self.prepare_predecessor()
        members = self.checkpoint_members()
        stale = [(name, bytes([data[0]^1])+data[1:] if i == 0 else data, kind)
                 for i, (name, data, kind) in enumerate(members)]
        receipt_stale = [(name, data+b'\n' if name == str(installer.STATE) else data, kind)
                         for name, data, kind in members]
        variants = {'stale runtime': stale, 'stale receipt': receipt_stale,
                    'missing': members[:-1], 'duplicate': members+[members[0]],
                    'extra': members+[('unrelated.txt', b'other', tarfile.REGTYPE)],
                    'directory': [(members[0][0], b'', tarfile.DIRTYPE)]+members[1:],
                    'symbolic link': [(members[0][0], b'', tarfile.SYMTYPE)]+members[1:],
                    'hard link': [(members[0][0], b'', tarfile.LNKTYPE)]+members[1:]}
        for label, variant in variants.items():
            with self.subTest(label=label):
                checkpoint = self.checkpoint(variant)
                before = self.snapshot()
                with self.assertRaisesRegex(ValueError, 'Runtime checkpoint'):
                    self.run_install(checkpoint=checkpoint)
                self.assertEqual(self.snapshot(), before)
        checkpoint = self.checkpoint(mode='w:gz')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Invalid uncompressed runtime checkpoint'):
            self.run_install(checkpoint=checkpoint)
        self.assertEqual(self.snapshot(), before)

    def test_checkpoint_validation_applies_without_override_and_refuses_symlink(self):
        self.run_install()
        checkpoint = self.checkpoint(self.checkpoint_members()[:-1])
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'exactly all four'):
            self.run_install(checkpoint=checkpoint)
        self.assertEqual(self.snapshot(), before)
        checkpoint = self.checkpoint()
        link = checkpoint.with_name('link.tar')
        link.symlink_to(checkpoint.name)
        with self.assertRaises(ValueError):
            self.run_install(checkpoint=link)
        self.assertEqual(self.snapshot(), before)

    def test_concurrent_checkpoint_bound_file_change_refused_before_publication(self):
        self.prepare_predecessor()
        checkpoint = self.checkpoint()
        real = installer.verify_runtime_checkpoint
        for relative in (installer.STATE, installer.FILES['gui']):
            original = (self.card/relative).read_bytes()
            def racing(*args):
                result = real(*args)
                (self.card/relative).write_bytes(original+b'\n')
                return result
            with self.subTest(path=str(relative)), patch.object(installer, 'verify_runtime_checkpoint', racing):
                before = self.snapshot()
                with self.assertRaisesRegex(ValueError, 'changed before publication'):
                    self.run_install(checkpoint=checkpoint)
                expected = dict(before)
                expected[str(relative)] = original+b'\n'
                self.assertEqual(self.snapshot(), expected)
                (self.card/relative).write_bytes(original)

    def test_runtime_predecessor_records_and_packager_validation(self):
        predecessor = self.prepare_predecessor()
        path = Path(self.temp.name)/'runtime.kui'
        path.write_bytes(predecessor)
        records = packager.runtime_predecessors([path, path])
        self.assertEqual(records, self.metadata['accepted_runtime_predecessors'])
        self.assertEqual(installer.runtime_predecessors({'accepted_runtime_predecessors': records}),
                         {sha(predecessor): '26a9b2193556'})
        path.write_bytes(predecessor[:-1]+bytes([predecessor[-1]^1]))
        with self.assertRaises(ValueError):
            packager.runtime_predecessors([path])
        invalid = [None, {}, [{'build': '26a9b2193556'}],
                   [dict(records[0], build='unreviewed')],
                   [dict(records[0], sha256='F'*64)], [records[0], records[0]],
                   [dict(records[0], extra=True)]]
        before = self.snapshot()
        for records in invalid:
            with self.subTest(records=records):
                self.metadata['accepted_runtime_predecessors'] = records
                self.write_metadata()
                with self.assertRaisesRegex(ValueError, 'Invalid runtime predecessor'):
                    self.run_install()
                self.assertEqual(self.snapshot(), before)

    def test_checkpoint_cli_and_restore_conflict(self):
        self.prepare_predecessor()
        checkpoint = self.checkpoint()
        command = [sys.executable, str(self.package/'install.py'), str(self.card),
                   '--profile', str(self.profile.relative_to(self.card)),
                   '--runtime-checkpoint', str(checkpoint)]
        out = subprocess.run(command, capture_output=True, text=True, check=True).stdout
        self.assertIn('runtime_checkpoint', out)
        self.assertEqual((self.card/installer.FILES['gui']).read_bytes(), self.gui)
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'cannot be used with --restore'):
            self.run_install(restore=True, checkpoint=checkpoint)
        result = subprocess.run(command+['--restore'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('cannot be used with --restore', result.stderr)
        self.assertEqual(self.snapshot(), before)

    def test_package_mismatch_refuses(self):
        self.metadata['gui']['sha256'] = '0'*64
        self.write_metadata()
        with self.assertRaisesRegex(ValueError, 'Games UI differs'):
            self.run_install()
        self.assertEqual(self.snapshot(), self.before)

    def test_interrupted_publication_rolls_back_and_recovers(self):
        real = installer.publish
        calls = {'files': 0}

        def failing(path, data):
            if path.name.endswith('.kui') and 'backup' not in path.parts and '.backup-' not in str(path):
                calls['files'] += 1
                if calls['files'] == 2:
                    raise OSError('simulated card removal')
            return real(path, data)
        with patch.object(installer, 'publish', failing):
            with self.assertRaisesRegex(OSError, 'card removal'):
                self.run_install()
        now = self.snapshot()
        for name in ('KUI/runtime.kui', 'KUI/apps/games/retail-native-dual.kui'):
            self.assertEqual(now[name], self.before[name], name)
        self.assertNotIn('KUI/apps/games/retail-native-standalone.kui', now)
        self.assertTrue(json.loads((self.card/installer.STATE).read_text())['pending'])
        self.run_install()
        self.run_install(restore=True)
        self.assert_original()

    def test_cli(self):
        command = [sys.executable, str(self.package/'install.py'), str(self.card),
                   '--profile', str(self.profile.relative_to(self.card))]
        out = subprocess.run(command, capture_output=True, text=True, check=True).stdout
        self.assertIn('Background New (X)', out)
        out = subprocess.run(command[:3]+['--restore'], capture_output=True, text=True, check=True).stdout
        self.assertIn('Exact previous Games UI', out)
        self.assert_original()


if __name__ == '__main__':
    unittest.main()
