#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generic WinCE extraction against independently authored raw-GDI fixtures."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch
import zipfile

import test_armada_startup_bundle as fixture_tests
from games_fixture import DATA, RAW, SESSION, record

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/kui-wince-startup-bundle.py'
SPEC = importlib.util.spec_from_file_location('wince_startup_bundle', SCRIPT)
bundle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bundle)


class WinCEStartupBundleTests(unittest.TestCase):
    # Reuse only synthetic disc construction and established adversarial
    # checks. The generic extractor below is the implementation under test.
    setUp = fixture_tests.ArmadaStartupBundleTests.setUp
    payload = fixture_tests.ArmadaStartupBundleTests.payload
    directory = fixture_tests.ArmadaStartupBundleTests.directory
    large_root = fixture_tests.ArmadaStartupBundleTests.large_root
    hashes = fixture_tests.ArmadaStartupBundleTests.hashes
    mandatory_bytes = fixture_tests.ArmadaStartupBundleTests.mandatory_bytes
    no_staging_files = fixture_tests.ArmadaStartupBundleTests.no_staging_files

    def fixture(self):
        fixture_tests.ArmadaStartupBundleTests.fixture(self)
        self.payload(0, 128, b'Independent WinCE Puzzle Fixture'.ljust(128))
        self.payload(0, 64, b'KUI-CE001 ')

    def reject_read_only(self):
        before = self.hashes()
        with self.assertRaises((ValueError, OSError)):
            bundle.extract(self.gdi)
        self.assertEqual(self.hashes(), before)
        self.assertFalse(self.output.exists())

    def established_checks(self, *names):
        with patch.object(fixture_tests, 'bundle', bundle):
            for name in names:
                with self.subTest(check=name):
                    self.output.unlink(missing_ok=True)
                    self.fixture()
                    getattr(fixture_tests.ArmadaStartupBundleTests, name)(self)

    def test_foreign_title_full_inventory_and_hashes(self):
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        self.assertEqual(report['title'], 'Independent WinCE Puzzle Fixture')
        self.assertEqual(report['product'], 'KUI-CE001')
        self.assertEqual(report['version'], 'V1.000')
        self.assertTrue(report['windows_ce'])
        self.assertEqual(set(entries), {'IP.BIN', 'disc.gdi'} | set(self.contents))
        hashes = {item['path']: item['sha256'] for item in report['files']}
        for name, data in self.contents.items():
            self.assertEqual(entries[name], data)
            self.assertEqual(hashes[name], hashlib.sha256(data).hexdigest())
        inventory = {item['path']: item for item in report['inventory']}
        self.assertIn('SYSTEM/TEXTURE.BIN', inventory)
        self.assertNotIn('SYSTEM/TEXTURE.BIN', entries)
        self.assertEqual(report['tracks'][2], {
            'number': 3, 'start_lba': SESSION, 'end_lba': SESSION+64,
            'control': 4, 'sector_bytes': RAW, 'file': 'track03.bin', 'file_bytes': 64*RAW})
        self.assertEqual(report['audio_candidates']['low_density_tracks'], [2])
        self.assertEqual(report['audio_candidates']['high_density_tracks'], [])
        self.assertFalse(report['audio_candidates']['usage_verified'])
        self.assertFalse(report['audio_payload_copied'])
        self.assertTrue(any('warning tracks' in item for item in report['warnings']))
        self.assertEqual(report['limits']['scanned_entries'], 32768)
        self.assertEqual(report['limits']['directory_count'], 512)
        self.assertEqual(report['limits']['total_directory_bytes'], 16*1024*1024)
        self.assertEqual(report['limits']['selected_file_count'], 2048)
        self.assertEqual(self.hashes(), before)

    def test_seventeen_and_ninety_nine_tracks_without_audio_reads(self):
        for count in (17, 99):
            with self.subTest(tracks=count):
                self.fixture()
                rows = self.gdi.read_text().splitlines()[1:]
                for number in range(4, count+1):
                    name = f'audio{number:02d}.raw'
                    (self.source/name).write_bytes(bytes([number])*RAW)
                    rows.append(f'{number} {SESSION+64+number-4} 0 2352 {name} 0')
                self.gdi.write_text(str(count)+'\n'+'\n'.join(rows)+'\n')
                before = self.hashes()
                original_open = Path.open

                def no_audio_open(path, *args, **kwargs):
                    if path.suffix.lower() == '.raw':
                        raise AssertionError('PCM tracks must not be opened')
                    return original_open(path, *args, **kwargs)

                with patch.object(Path, 'open', no_audio_open):
                    entries, report = bundle.extract(self.gdi)
                self.assertEqual(len(report['tracks']), count)
                self.assertEqual(report['tracks'][-1]['number'], count)
                self.assertEqual(report['audio_candidates']['high_density_tracks'], list(range(4, count+1)))
                self.assertFalse(any(name.endswith('.raw') for name in entries))
                self.assertEqual(self.hashes(), before)
        self.gdi.write_text(self.gdi.read_text().replace('99\n', '100\n', 1))
        self.reject_read_only()

    def test_one_data_track_and_no_audio_are_accepted(self):
        self.gdi.write_text('1\n1 45000 4 2352 track03.bin 0\n')
        entries, report = bundle.extract(self.gdi)
        self.assertIn('0WINCEOS.BIN', entries)
        self.assertEqual(len(report['tracks']), 1)
        self.assertEqual(report['audio_candidates']['high_density_tracks'], [])
        self.assertEqual(report['audio_candidates']['low_density_tracks'], [])
        self.assertTrue(any('No standalone audio' in item for item in report['warnings']))

    def test_arbitrary_nested_boot_and_executable(self):
        self.payload(0, 96, b'CESTART.BIN'.ljust(16))
        self.directory(20, self.root_records[:2]+self.root_records[4:])
        self.directory(30, self.system_records+[
            record(b'CESTART.BIN;1', SESSION+33, len(self.contents['0WINCEOS.BIN'])),
            record(b'PUZZLE.EXE;1', SESSION+22, len(self.contents['ARMADA.EXE']))])
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        self.assertEqual(report['boot_path'], 'SYSTEM/CESTART.BIN')
        self.assertEqual(report['application_candidates'], ['SYSTEM/PUZZLE.EXE'])
        self.assertEqual(entries['SYSTEM/CESTART.BIN'], self.contents['0WINCEOS.BIN'])
        self.assertEqual(entries['SYSTEM/PUZZLE.EXE'], self.contents['ARMADA.EXE'])
        self.assertEqual(self.hashes(), before)

    def test_missing_non_ce_and_metadata_collisions_refused(self):
        self.payload(0, 56, b'0000000 ')
        self.reject_read_only()
        self.fixture()
        self.payload(0, 96, b'ABSENT.BIN'.ljust(16))
        self.reject_read_only()
        for name in (b'IP.BIN', b'DISC.GDI'):
            with self.subTest(boot=name):
                self.fixture()
                self.payload(0, 96, name.ljust(16))
                self.directory(20, self.root_records[:2]+[
                    record(name+b';1', SESSION+33, 4097)]+self.root_records[3:])
                with self.assertRaisesRegex(ValueError, 'metadata'):
                    bundle.extract(self.gdi)
        for name in (b'IP.BIN', b'DISC.GDI', b'STARTUP-INFO.JSON'):
            with self.subTest(directory=name):
                self.fixture()
                self.directory(20, self.root_records[:5]+[
                    record(name, SESSION+30, DATA, True)])
                with self.assertRaisesRegex(ValueError, 'metadata'):
                    bundle.extract(self.gdi)

    def test_generic_boot_has_no_unverified_prefix_size_requirement(self):
        self.directory(20, self.root_records[:2]+[
            record(b'0winceos.bin;1', SESSION+33, 512)]+self.root_records[3:])
        entries, _ = bundle.extract(self.gdi)
        self.assertEqual(len(entries['0WINCEOS.BIN']), 512)

    def test_audio_assets_inventory_only(self):
        self.directory(20, self.root_records+[
            record(name, SESSION+24, 32) for name in (b'MUSIC.WAV;1', b'SONG.PCM;1', b'TRACK.RAW;1')])
        entries, report = bundle.extract(self.gdi)
        inventory = {item['path'] for item in report['inventory']}
        for name in ('MUSIC.WAV', 'SONG.PCM', 'TRACK.RAW'):
            self.assertIn(name, inventory)
            self.assertNotIn(name, entries)

    def test_directory_kernel_and_extent_guards(self):
        self.established_checks('test_quoted_track_name_and_cross_track_file',
            'test_unique_nested_kernel_discovered_from_ip_basename',
            'test_multiple_nonroot_kernels_refused_with_candidate_paths',
            'test_root_kernel_preferred_over_nested_basename',
            'test_duplicate_versions_case_and_unsafe_names',
            'test_directory_cycle_or_repeated_extent_refused',
            'test_iso_dual_endianness_and_record_corruption',
            'test_unsupported_file_extent_flags_and_out_of_range',
            'test_malformed_descriptor_and_truncated_or_bad_framing',
            'test_gdi_and_track_symlinks_refused')

    def test_copied_payload_and_traversal_limits(self):
        self.established_checks('test_all_resource_caps_bounded',
            'test_selected_file_count_boundary_excludes_generated_metadata',
            'test_optional_per_file_oversize_is_recorded_without_reading_asset',
            'test_optional_budget_omission_keeps_later_small_candidate',
            'test_dll_drv_candidates_precede_lib_within_byte_budget',
            'test_mandatory_byte_bounds_are_fatal_with_actionable_counts',
            'test_optional_empty_or_unsupported_extent_still_refused')

    def test_atomic_read_only_publication(self):
        self.established_checks('test_overwrite_and_broken_symlink_refused',
            'test_partial_archive_never_published',
            'test_crc_or_readback_failure_never_published',
            'test_concurrent_file_or_symlink_creation_preserved')

    def test_cli_default_custom_version_and_no_overwrite(self):
        before = self.hashes()
        for args, destination in (([], self.folder/'WinCE-startup.zip'),
                                  (['--output', str(self.output)], self.output)):
            result = subprocess.run([sys.executable, str(SCRIPT), str(self.gdi), *args],
                                    cwd=self.folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            with zipfile.ZipFile(destination) as archive:
                self.assertIsNone(archive.testzip())
                self.assertFalse(json.loads(archive.read('startup-info.json'))['audio_payload_copied'])
            created = destination.read_bytes()
            result = subprocess.run([sys.executable, str(SCRIPT), str(self.gdi), *args],
                                    cwd=self.folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(destination.read_bytes(), created)
        result = subprocess.run([sys.executable, str(SCRIPT), '--version'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Windows CE startup extractor', result.stdout)
        self.assertEqual(self.hashes(), before)


if __name__ == '__main__':
    unittest.main()
