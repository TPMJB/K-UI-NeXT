#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The compatibility test installer changes exactly its three files, reversibly."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools')]
import install_native_compat_test as installer
from runtime_package import envelope

COMMIT = '0123456789abcdef0123456789abcdef01234567'


def module(text, build):
    payload = text.encode().ljust((len(text)+3)&~3, b' ')
    return envelope(payload, len(payload), build)


class CompatTestInstall(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        self.package, self.card = base/'package', base/'card'
        self.files = {}
        metadata = dict(schema=installer.SCHEMA, source_commit=COMMIT, build=COMMIT[:12], files={})
        for name in installer.TARGETS:
            data = module('new '+name, COMMIT[:12])
            path = self.package/'files'/(name+'.kui')
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            self.files[name] = data
            metadata['files'][name] = dict(path='files/'+name+'.kui', sha256=installer.sha(data))
        self.metadata = metadata
        self.write_metadata()
        for relative, text in ((installer.TARGETS['gui'], 'old gui'),
                               (installer.TARGETS['background'], 'old background'),
                               (Path('KUI/apps/games/retail-boot.kui'), 'standard loader'),
                               (Path('KUI/vmu-test/p0005/card.bin'), 'card bytes')):
            (self.card/relative).parent.mkdir(parents=True, exist_ok=True)
            (self.card/relative).write_bytes(module(text, '123456789abc'))
        self.original = self.snapshot()

    def write_metadata(self):
        (self.package/'compat-test.json').write_text(json.dumps(self.metadata))

    def snapshot(self):
        return {str(p.relative_to(self.card)): p.read_bytes() for p in self.card.rglob('*')
                if p.is_file() and installer.PARENT not in p.relative_to(self.card).parents}

    def run_install(self, restore=False):
        return installer.install(self.card, self.package, restore)

    def test_install_reinstall_and_exact_restore(self):
        self.run_install()
        after = self.snapshot()
        for name, target in installer.TARGETS.items():
            self.assertEqual(after[str(target)], self.files[name])
        for name, data in self.original.items():
            if Path(name) not in installer.TARGETS.values():
                self.assertEqual(after[name], data, name)
        with patch.object(installer, 'publish', side_effect=AssertionError('unexpected write')):
            self.assertTrue(all(item['already'] for item in self.run_install()['files'].values()))
        self.run_install(restore=True)
        self.assertEqual(self.snapshot(), self.original)
        self.assertFalse((self.card/installer.TARGETS['standalone']).exists())
        self.run_install()
        self.assertEqual(self.snapshot()[str(installer.TARGETS['standalone'])], self.files['standalone'])

    def test_existing_standalone_is_backed_up_and_restored(self):
        target = self.card/installer.TARGETS['standalone']
        target.write_bytes(module('old standalone', '123456789abc'))
        before = self.snapshot()
        self.run_install()
        self.run_install(restore=True)
        self.assertEqual(self.snapshot(), before)

    def test_tampered_package_refuses_before_any_write(self):
        path = self.package/'files/background.kui'
        data = bytearray(path.read_bytes())
        data[-1] ^= 1
        path.write_bytes(data)
        with self.assertRaises(ValueError):
            self.run_install()
        self.assertEqual(self.snapshot(), self.original)
        self.assertFalse((self.card/installer.PARENT).exists())

    def test_wrong_build_refuses(self):
        other = module('new background', 'fedcba987654')
        (self.package/'files/background.kui').write_bytes(other)
        self.metadata['files']['background']['sha256'] = installer.sha(other)
        self.write_metadata()
        with self.assertRaisesRegex(ValueError, 'pinned build'):
            self.run_install()
        self.assertFalse((self.card/installer.PARENT).exists())

    def test_external_change_is_preserved(self):
        self.run_install()
        changed = module('someone else', 'abcdefabcdef')
        (self.card/installer.TARGETS['gui']).write_bytes(changed)
        before = self.snapshot()
        for restore in (True, False):
            with self.assertRaisesRegex(ValueError, 'Changed outside'):
                self.run_install(restore)
            self.assertEqual(self.snapshot(), before)

    def test_another_package_backup_refuses(self):
        self.run_install()
        self.metadata['source_commit'] = 'f'*40
        self.metadata['build'] = 'f'*12
        for name in installer.TARGETS:
            data = module('new '+name, 'f'*12)
            (self.package/'files'/(name+'.kui')).write_bytes(data)
            self.metadata['files'][name]['sha256'] = installer.sha(data)
        self.write_metadata()
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Another compatibility test package'):
            self.run_install()
        self.assertEqual(self.snapshot(), before)

    def test_missing_gui_or_symlink_refuses(self):
        gui = self.card/installer.TARGETS['gui']
        saved = gui.read_bytes()
        gui.unlink()
        with self.assertRaisesRegex(ValueError, 'Missing current file'):
            self.run_install()
        other = Path(self.temp.name)/'elsewhere.kui'
        other.write_bytes(saved)
        gui.symlink_to(other)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            self.run_install()
        self.assertEqual(other.read_bytes(), saved)
        self.assertFalse((self.card/installer.PARENT).exists())

    def test_interrupted_install_completes_or_restores(self):
        publish = installer.publish
        calls = []

        def fail_second_target(path, data):
            if path.name.endswith('.kui') and installer.PARENT not in path.relative_to(self.card).parents:
                calls.append(path)
                if len(calls) == 2:
                    raise OSError('injected power loss')
            publish(path, data)
        with patch.object(installer, 'publish', side_effect=fail_second_target):
            with self.assertRaisesRegex(OSError, 'injected'):
                self.run_install()
        self.assertTrue(json.loads((self.card/installer.STATE).read_text())['pending'])
        self.run_install(restore=True)
        self.assertEqual(self.snapshot(), self.original)
        self.run_install()
        self.assertFalse(json.loads((self.card/installer.STATE).read_text())['pending'])

    def test_real_cli_installs_and_restores(self):
        tool = self.package/'install.py'
        tool.write_bytes((ROOT/'tools/install_native_compat_test.py').read_bytes())
        for arguments in ((), ('--restore',)):
            result = subprocess.run([sys.executable, str(tool), str(self.card), *arguments],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Safely eject', result.stdout)
        self.assertEqual(self.snapshot(), self.original)


if __name__ == '__main__':
    unittest.main()
