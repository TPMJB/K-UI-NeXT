#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the extracted two-file installer, refusals, rollback and save preservation."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
import install_vmu_native_dual as installer
import personalize_vmu_native_persistent as personalizer
import vmu_native_mode as mode
from runtime_package import envelope, verify
from test_personalize_vmu_native_persistent import generic, pair, COMMIT

CAP = bytes.fromhex('4b4452314455414c010000002000000001000000020000000000060000000000')


class DualInstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.package = self.root/'package'
        self.card = self.root/'card'
        self.card.mkdir()
        old, self.manifest = generic(self.package)
        payload = old[64:] + mode.template_mode() + CAP
        self.module = envelope(payload, len(payload), COMMIT[:12])
        self.manifest.update(module_sha256=personalizer.sha(self.module), module_bytes=len(self.module),
                             runtime_mode_module_offset=len(old), runtime_mode_bytes=32,
                             runtime_mode_sha256=personalizer.sha(mode.template_mode()),
                             runtime_mode_choices=personalizer.MODE_CHOICES_BACKGROUND)
        (self.package/personalizer.BASE_MODULE).write_bytes(self.module)
        (self.package/personalizer.MANIFEST).write_text(json.dumps(self.manifest))
        tools = self.package/'tools'
        tools.mkdir()
        for name in ('runtime_package.py', 'vmu_persist_profile.py', 'vmu_native_mode.py'):
            shutil.copyfile(ROOT/'tools'/name, tools/name)
        shutil.copyfile(ROOT/'tools/personalize_vmu_native_persistent.py', self.package/'personalize.py')
        shutil.copyfile(ROOT/'tools/install_vmu_native_dual.py', self.package/'install.py')
        self.gui = envelope(b'\x09\x00'*96, 192, COMMIT[:12])
        target = self.package/'base/KUI/runtime.kui'
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(self.gui)
        self.metadata = dict(schema='kui.native.dual-runtime.v1', build=COMMIT[:12],
                             runtime_sha256=personalizer.sha(self.gui), runtime_bytes=len(self.gui),
                             module_sha256=personalizer.sha(self.module))
        self.metadata_write()
        self.profile = pair(self.card, 'p0005', number='0005')
        self.dual = self.card/installer.DESTINATION
        self.dual.parent.mkdir(parents=True, exist_ok=True)
        self.dual.write_bytes(b'old native dual module')
        self.runtime = self.card/installer.GUI
        self.runtime.write_bytes(b'old GUI')
        for name in ('retail-boot.kui', 'ce-probe.kui', 'cdda-toy-raw.kui'):
            (self.dual.parent/name).write_bytes(('existing '+name).encode())
        (self.dual.parent/'launch-settings-a.bin').write_bytes(b'existing launch choices')

    def metadata_write(self):
        (self.package/'base/native-dual-runtime.json').write_text(json.dumps(self.metadata))

    def snapshot(self):
        return {p.relative_to(self.card).as_posix(): p.read_bytes()
                for p in self.card.rglob('*') if p.is_file()}

    def install(self):
        return installer.install(self.card, self.package,
                                 self.profile.relative_to(self.card), True)

    def test_matching_pair_updates_only_two_files(self):
        before = self.snapshot()
        result = self.install()
        after = self.snapshot()
        changed = {name for name in before if before[name] != after[name]}
        self.assertEqual(changed, {str(installer.GUI), str(installer.DESTINATION)})
        self.assertEqual(set(before), set(after))
        self.assertEqual(self.runtime.read_bytes(), self.gui)
        self.assertEqual(verify(self.dual.read_bytes())['build'], COMMIT[:12])
        offset = self.manifest['runtime_mode_module_offset']
        self.assertEqual(mode.decode_mode(self.dual.read_bytes()[offset:offset+32]), 2)
        self.assertEqual(result['installed_files'], [str(installer.GUI), str(installer.DESTINATION)])
        self.assertTrue(result['fresh_discovery'])

    def test_repeated_install_performs_no_replace(self):
        self.install()
        with patch.object(installer.os, 'replace', side_effect=AssertionError('Unexpected write')):
            self.install()

    def test_extracted_cli_works_outside_package(self):
        result = subprocess.run([sys.executable, str(self.package/'install.py'), str(self.card),
                                 '--profile', str(self.profile.relative_to(self.card)), '--profile-runtime'],
                                cwd=self.root, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('New (X) / Legacy (Y)', result.stdout)

    def test_evolution_trace_targets_dual_module_and_preserves_card(self):
        before = self.snapshot()
        result = subprocess.run([sys.executable, str(self.package/'install.py'), str(self.card),
                                 '--profile', str(self.profile.relative_to(self.card)), '--evolution-trace'],
                                cwd=self.root, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Evolution 1 trace enabled', result.stdout)
        offset = self.manifest['runtime_mode_module_offset']
        self.assertEqual(mode.decode_mode(self.dual.read_bytes()[offset:offset+32]), 6)
        after = self.snapshot()
        self.assertEqual(set(before), set(after))
        self.assertTrue(all(after[name] == value for name, value in before.items()
                            if name not in (str(installer.GUI), str(installer.DESTINATION))))

    def test_conflicting_diagnostics_refuse_without_writes(self):
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Select either'):
            installer.install(self.card, self.package, self.profile, True, True)
        self.assertEqual(before, self.snapshot())
        result = subprocess.run([sys.executable, str(self.package/'install.py'), str(self.card),
                                 '--profile-runtime', '--evolution-trace'], capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(before, self.snapshot())

    def test_metadata_and_gui_mutations_refuse_before_installed_writes(self):
        before = self.snapshot()
        for key, value in (('schema', 'unknown'), ('build', 'b'*12),
                           ('runtime_sha256', '0'*64), ('module_sha256', '0'*64),
                           ('runtime_bytes', len(self.gui)+1)):
            with self.subTest(key=key):
                old = self.metadata[key]
                self.metadata[key] = value
                self.metadata_write()
                with self.assertRaises(ValueError):
                    self.install()
                self.assertEqual(before, self.snapshot())
                self.metadata[key] = old
        self.metadata_write()
        gui = self.package/'base/KUI/runtime.kui'
        gui.write_bytes(self.gui[:-1]+bytes([self.gui[-1]^1]))
        with self.assertRaises(ValueError):
            self.install()
        self.assertEqual(before, self.snapshot())

    def test_missing_profile_does_not_leave_temporary_or_changed_files(self):
        before = self.snapshot()
        with self.assertRaises((OSError, ValueError)):
            installer.install(self.card, self.package, Path('KUI/missing.bin'), True)
        self.assertEqual(before, self.snapshot())

    def test_gui_replace_failure_rolls_back_module(self):
        before = self.snapshot()
        replace = os.replace
        def failure(source, destination):
            if Path(destination) == self.runtime:
                raise OSError('Injected GUI rename failure')
            return replace(source, destination)
        with patch.object(installer.os, 'replace', side_effect=failure):
            with self.assertRaisesRegex(OSError, 'Injected'):
                self.install()
        self.assertEqual(before, self.snapshot())

    def test_gui_sync_failure_rolls_back_both_files(self):
        before = self.snapshot()
        sync = installer.sync_directory
        calls = 0
        def failure(path):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError('Injected GUI fsync failure')
            return sync(path)
        with patch.object(installer, 'sync_directory', side_effect=failure):
            with self.assertRaisesRegex(OSError, 'Injected'):
                self.install()
        self.assertEqual(before, self.snapshot())

    def test_module_directory_sync_failure_rolls_back_module(self):
        before = self.snapshot()
        sync = installer.sync_directory
        calls = 0
        def failure(path):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise OSError('Injected module directory fsync failure')
            return sync(path)
        with patch.object(installer, 'sync_directory', side_effect=failure):
            with self.assertRaisesRegex(OSError, 'Injected module'):
                self.install()
        self.assertEqual(before, self.snapshot())

    def test_final_readback_failure_rolls_back_both_files(self):
        before = self.snapshot()
        read = Path.read_bytes
        failed = False
        def failure(path):
            nonlocal failed
            data = read(path)
            if path == self.runtime and data == self.gui and not failed:
                failed = True
                return data[:-1]+bytes([data[-1]^1])
            return data
        with patch.object(Path, 'read_bytes', failure):
            with self.assertRaisesRegex(ValueError, 'Installed candidate readback differs'):
                self.install()
        self.assertTrue(failed)
        self.assertEqual(before, self.snapshot())

    def test_profile_and_gui_symlinks_refused(self):
        before = self.snapshot()
        self.runtime.unlink()
        elsewhere = self.root/'outside.kui'
        elsewhere.write_bytes(b'outside file')
        self.runtime.symlink_to(elsewhere)
        with self.assertRaises(ValueError):
            self.install()
        self.assertEqual(elsewhere.read_bytes(), b'outside file')
        self.assertEqual(self.dual.read_bytes(), before[str(installer.DESTINATION)])

    def test_generic_api_destination_allowlist_preserves_ordinary_loader(self):
        before = self.snapshot()
        for path in ('KUI/runtime.kui', '../escape.kui', '/tmp/escape.kui'):
            with self.subTest(path=path):
                with self.assertRaises(ValueError):
                    personalizer.install(self.card, package=self.package, profile=self.profile,
                                         destination_path=path)
                self.assertEqual(before, self.snapshot())


if __name__ == '__main__':
    unittest.main()
