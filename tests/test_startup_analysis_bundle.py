#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""IP-profile dispatch tests using synthetic native and CE raw-GDI inputs."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch
import zipfile

import test_native_startup_bundle as fixture_tests

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/kui-startup-analysis-bundle.py'
SPEC = importlib.util.spec_from_file_location('startup_analysis_bundle', SCRIPT)
bundle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bundle)


class StartupAnalysisBundleTests(unittest.TestCase):
    setUp = fixture_tests.NativeStartupBundleTests.setUp
    fixture = fixture_tests.NativeStartupBundleTests.fixture
    payload = fixture_tests.NativeStartupBundleTests.payload
    directory = fixture_tests.NativeStartupBundleTests.directory
    hashes = fixture_tests.NativeStartupBundleTests.hashes

    def test_native_and_ce_profiles_dispatch_to_their_strict_helpers(self):
        for ce in (False, True):
            with self.subTest(windows_ce=ce):
                self.fixture()
                self.payload(0, 56, b'0000011 ' if ce else b'0000010 ')
                before = self.hashes()
                helper, entries, report = bundle.extract(self.gdi)
                self.assertIs(helper, bundle.wince if ce else bundle.native)
                self.assertEqual(report['windows_ce'], ce)
                self.assertEqual(report['boot_path'], '1ST_READ.BIN')
                self.assertEqual(entries['1ST_READ.BIN'], self.boot)
                if ce:
                    self.assertIn('UNRELATED.EXE', entries)
                    self.assertIn('SYSTEM/GRAPHICS.DLL', entries)
                else:
                    self.assertEqual(set(entries), {'IP.BIN', 'disc.gdi', '1ST_READ.BIN'})
                self.assertEqual(self.hashes(), before)

    def test_only_dedicated_ce_profile_exception_can_retry(self):
        for error in (ValueError('Windows CE is not supported'), OSError('read failure'),
                      UnicodeError('bad metadata')):
            with self.subTest(error=type(error).__name__):
                with patch.object(bundle.native, 'extract', side_effect=error):
                    with patch.object(bundle.wince, 'extract') as fallback:
                        with self.assertRaises(type(error)):
                            bundle.extract(self.gdi)
                fallback.assert_not_called()
        profile = bundle.native.WindowsCEProfileError('validated CE IP flag')
        with patch.object(bundle.native, 'extract', side_effect=profile):
            with patch.object(bundle.wince, 'extract', return_value=({}, {'windows_ce': True})) as fallback:
                helper, entries, report = bundle.extract(self.gdi)
        fallback.assert_called_once_with(self.gdi)
        self.assertIs(helper, bundle.wince)
        self.assertEqual((entries, report), ({}, {'windows_ce': True}))

    def test_actual_malformed_native_metadata_and_invalid_flags_never_retry(self):
        for ce_flags in (b'0000000 ', b'00000G1 '):
            with self.subTest(flags=ce_flags):
                self.fixture()
                self.payload(0, 56, ce_flags)
                self.payload(16, 130, b'\xfe')
                before = self.hashes()
                with patch.object(bundle.wince, 'extract') as fallback:
                    with self.assertRaises(ValueError):
                        bundle.extract(self.gdi)
                fallback.assert_not_called()
                self.assertEqual(self.hashes(), before)

    def test_ce_profile_is_revalidated_and_malformed_ce_fails(self):
        self.payload(0, 56, b'0000001 ')
        self.payload(16, 130, b'\xfe')
        before = self.hashes()
        with patch.object(bundle.wince, 'extract', wraps=bundle.wince.extract) as fallback:
            with self.assertRaisesRegex(ValueError, 'byte orders'):
                bundle.extract(self.gdi)
        fallback.assert_called_once_with(self.gdi)
        self.assertEqual(self.hashes(), before)
        self.assertFalse(self.output.exists())

    def test_cli_profiles_default_custom_no_overwrite_and_version(self):
        for ce, args, name in ((False, [], 'Startup-analysis.zip'),
                               (True, ['--output', 'Silver-startup.zip'], 'Silver-startup.zip')):
            with self.subTest(windows_ce=ce):
                self.fixture()
                self.payload(0, 56, b'0000001 ' if ce else b'0000000 ')
                before = self.hashes()
                result = subprocess.run([sys.executable, str(SCRIPT), str(self.gdi), *args],
                                        cwd=self.folder, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                destination = self.folder / name
                with zipfile.ZipFile(destination) as archive:
                    self.assertIsNone(archive.testzip())
                    report = json.loads(archive.read('startup-info.json'))
                    self.assertEqual(report['windows_ce'], ce)
                    self.assertEqual(archive.read('1ST_READ.BIN'), self.boot)
                created = destination.read_bytes()
                result = subprocess.run([sys.executable, str(SCRIPT), str(self.gdi), *args],
                                        cwd=self.folder, capture_output=True, text=True)
                self.assertEqual(result.returncode, 1)
                self.assertIn('already exists', result.stderr)
                self.assertEqual(destination.read_bytes(), created)
                self.assertEqual(self.hashes(), before)
                destination.unlink()
        result = subprocess.run([sys.executable, str(SCRIPT), '--version'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'K-UI startup analysis dispatcher v1')


if __name__ == '__main__':
    unittest.main()
