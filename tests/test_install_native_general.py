#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Install/restore the general candidate on a temporary card; no console bytes."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
sys.path.insert(0, str(ROOT/'tests'))
import install_native_general as installer
import personalize_vmu_native_persistent as personalizer
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

    def run_install(self, restore=False):
        return installer.run(self.card, self.package, restore,
                             None if restore else self.profile.relative_to(self.card))

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
