#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generic native/WinCE CDDA extraction against original synthetic fixtures."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import sys
import unittest
from unittest.mock import patch
import zipfile

import test_armada_startup_bundle as fixture_tests
from games_fixture import DATA, RAW, SESSION, raw_sector, record

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/kui-cdda-startup-bundle.py'
SPEC = importlib.util.spec_from_file_location('cdda_startup_bundle', SCRIPT)
bundle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bundle)


class CDDAStartupBundleTests(unittest.TestCase):
    # Reuse synthetic disc authoring and established adversarial checks only.
    # No retail bytes, executables, or platform tools are needed by these tests.
    setUp = fixture_tests.ArmadaStartupBundleTests.setUp
    payload = fixture_tests.ArmadaStartupBundleTests.payload
    directory = fixture_tests.ArmadaStartupBundleTests.directory
    hashes = fixture_tests.ArmadaStartupBundleTests.hashes
    mandatory_bytes = fixture_tests.ArmadaStartupBundleTests.mandatory_bytes
    no_staging_files = fixture_tests.ArmadaStartupBundleTests.no_staging_files

    def fixture(self):
        fixture_tests.ArmadaStartupBundleTests.fixture(self)
        self.payload(0, 64, b'KUI-CDDA  ')
        self.payload(0, 128, b'Independent CDDA Fixture'.ljust(128))

    def native_fixture(self):
        self.fixture()
        self.payload(0, 56, b'0000000 ')
        self.payload(0, 96, b'1ST_READ.BIN'.ljust(16))
        self.payload(0, 128, b'Independent Native Quake-Style Fixture'.ljust(128))
        self.contents['1ST_READ.BIN'] = self.contents.pop('0WINCEOS.BIN')
        self.contents.pop('ARMADA.EXE')
        self.root_records = self.root_records[:2] + [
            record(b'1st_read.bin;1', SESSION+33, len(self.contents['1ST_READ.BIN']))
        ] + self.root_records[4:]
        self.root_offsets = self.directory(20, self.root_records)

    def cook_data_tracks(self):
        rows = [shlex.split(line) for line in self.gdi.read_text().splitlines()]
        for row in rows[1:]:
            if row[2] == '4':
                path = self.source / row[4]
                raw = path.read_bytes()
                path.write_bytes(b''.join(raw[i+16:i+16+DATA]
                                          for i in range(0, len(raw), RAW)))
                row[3] = str(DATA)
        self.gdi.write_text('\n'.join(' '.join(row) for row in rows)+'\n')

    def cue_fixture(self, windows_ce=False, cooked=False, stored_gap=0,
                    pregap=0, postgap=0, high_rem=True):
        """Author a per-track BIN/CUE layout independently of the extractor."""
        self.fixture() if windows_ce else self.native_fixture()
        if cooked:
            self.cook_data_tracks()
        audio = (self.source/'track02.raw').read_bytes()
        (self.source/'track02.bin').write_bytes(b'P'*stored_gap*RAW+audio)
        mode = 'MODE1/2048' if cooked else 'MODE1/2352'
        lines = ['REM SINGLE-DENSITY AREA', 'FILE "track01.bin" BINARY',
                 f'  TRACK 01 {mode}', '    INDEX 01 00:00:00']
        if postgap:
            lines.append(f'    POSTGAP 00:00:{postgap:02d}')
        lines += ['FILE "track02.bin" BINARY', '  TRACK 02 AUDIO']
        if pregap:
            lines.append(f'    PREGAP 00:00:{pregap:02d}')
        if stored_gap:
            lines.append('    INDEX 00 00:00:00')
        lines.append(f'    INDEX 01 00:00:{stored_gap:02d}')
        if high_rem:
            lines.append('REM HIGH-DENSITY AREA')
        lines += ['FILE "track03.bin" BINARY', f'  TRACK 03 {mode}',
                  '    INDEX 01 00:00:00']
        self.cue = self.source/'disc.cue'
        self.cue.write_text('\n'.join(lines)+'\n')
        return self.cue

    def combined_cue_fixture(self, high_rem=True):
        self.native_fixture()
        low = (self.source/'track01.bin').read_bytes()
        audio = (self.source/'track02.raw').read_bytes()
        high = self.track.read_bytes()
        self.combined = self.source/'combined.bin'
        self.combined.write_bytes(low+audio+high)
        lines = ['FILE "combined.bin" BINARY', '  TRACK 01 MODE1/2352',
                 '    INDEX 01 00:00:00', '  TRACK 02 AUDIO',
                 '    INDEX 01 00:00:04']
        if high_rem:
            lines.append('REM HIGH-DENSITY AREA')
        lines += ['  TRACK 03 MODE1/2352', '    INDEX 01 00:00:08']
        self.cue = self.source/'disc.cue'
        self.cue.write_text('\n'.join(lines)+'\n')
        return self.cue

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

    def assert_full_hashes(self, entries, report):
        files = {item['path']: item for item in report['files']}
        self.assertEqual(set(files), set(entries))
        for name, data in entries.items():
            self.assertEqual(files[name]['bytes'], len(data))
            self.assertEqual(files[name]['sha256'], hashlib.sha256(data).hexdigest())
            self.assertEqual(len(files[name]['sha256']), 64)

    def test_native_boot_and_driver_without_external_exe(self):
        self.native_fixture()
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        self.assertEqual(set(entries), {'IP.BIN', 'disc.gdi'} | set(self.contents))
        self.assertFalse(report['windows_ce'])
        self.assertEqual(report['title'], 'Independent Native Quake-Style Fixture')
        self.assertEqual(report['boot_path'], '1ST_READ.BIN')
        self.assertEqual(report['application_candidates'], [])
        self.assertEqual(entries['SYSTEM/AUDIO.DRV'], self.contents['SYSTEM/AUDIO.DRV'])
        self.assertEqual(report['data_encoding'], 'raw2352')
        self.assertFalse(report['audio_payload_copied'])
        self.assertFalse(report['audio_candidates']['usage_verified'])
        self.assert_full_hashes(entries, report)
        inventory = {item['path'] for item in report['inventory']}
        self.assertIn('SYSTEM/TEXTURE.BIN', inventory)
        self.assertNotIn('SYSTEM/TEXTURE.BIN', entries)
        bundle.write_bundle(self.output, entries, report)
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(set(archive.namelist()), set(entries) | {'startup-info.json'})
            for name, data in entries.items():
                self.assertEqual(archive.read(name), data)
            self.assertEqual(json.loads(archive.read('startup-info.json')), report)
        self.assertEqual(self.hashes(), before)
        self.no_staging_files()

    def test_actual_wince_flag_and_external_exe_remain_required(self):
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        self.assertTrue(report['windows_ce'])
        self.assertEqual(report['boot_path'], '0WINCEOS.BIN')
        self.assertEqual(report['application_candidates'], ['ARMADA.EXE'])
        self.assertEqual(entries['ARMADA.EXE'], self.contents['ARMADA.EXE'])
        self.assert_full_hashes(entries, report)
        with patch.object(bundle, 'FILE_COUNT_LIMIT', 1):
            with self.assertRaises(ValueError):
                bundle.extract(self.gdi)
        self.assertEqual(self.hashes(), before)
        self.native_fixture()
        before = self.hashes()
        with patch.object(bundle, 'FILE_COUNT_LIMIT', 1):
            entries, report = bundle.extract(self.gdi)
        self.assertEqual(set(entries), {'IP.BIN', 'disc.gdi', '1ST_READ.BIN'})
        self.assertTrue(report['omitted_files'])
        self.assertEqual(self.hashes(), before)

    def test_raw_and_cooked_data_preserve_payloads_and_full_sha256(self):
        for windows_ce in (False, True):
            with self.subTest(windows_ce=windows_ce):
                self.fixture() if windows_ce else self.native_fixture()
                before = self.hashes()
                raw_entries, raw_report = bundle.extract(self.gdi)
                self.assertEqual(self.hashes(), before)
                self.cook_data_tracks()
                before = self.hashes()
                cooked_entries, cooked_report = bundle.extract(self.gdi)
                self.assertEqual(raw_report['data_encoding'], 'raw2352')
                self.assertEqual(cooked_report['data_encoding'], 'cooked2048')
                self.assertEqual(cooked_report['windows_ce'], windows_ce)
                self.assertEqual({name: data for name, data in raw_entries.items()
                                  if name != 'disc.gdi'},
                                 {name: data for name, data in cooked_entries.items()
                                  if name != 'disc.gdi'})
                raw_hashes = {item['path']: item['sha256'] for item in raw_report['files']
                              if item['path'] != 'disc.gdi'}
                cooked_hashes = {item['path']: item['sha256'] for item in cooked_report['files']
                                 if item['path'] != 'disc.gdi'}
                self.assertEqual(raw_hashes, cooked_hashes)
                self.assert_full_hashes(cooked_entries, cooked_report)
                self.assertEqual(cooked_entries['disc.gdi'], self.gdi.read_bytes())
                self.assertEqual([item['sector_bytes'] for item in cooked_report['tracks']],
                                 [DATA, RAW, DATA])
                self.assertEqual(self.hashes(), before)

    def test_mixed_data_tracks_and_cross_track_file(self):
        raw = self.track.read_bytes()
        self.track.write_bytes(raw[:23*RAW])
        tail = b''.join(raw[i+16:i+16+DATA] for i in range(23*RAW, len(raw), RAW))
        (self.source/'tail.iso').write_bytes(tail)
        text = self.gdi.read_text().replace('3\n', '4\n', 1)
        self.gdi.write_text(text+f'4 {SESSION+23} 4 2048 tail.iso 0\n')
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        self.assertEqual(report['data_encoding'], 'mixed')
        self.assertEqual(entries['ARMADA.EXE'], self.contents['ARMADA.EXE'])
        self.assertEqual(entries['0WINCEOS.BIN'], self.contents['0WINCEOS.BIN'])
        self.assertEqual(report['tracks'][2]['data_encoding'], 'raw2352')
        self.assertEqual(report['tracks'][3]['data_encoding'], 'cooked2048')
        self.assert_full_hashes(entries, report)
        self.assertEqual(self.hashes(), before)

    def test_audio_track_payloads_never_opened_even_with_bin_suffix(self):
        self.native_fixture()
        high_audio = self.source/'high-audio.bin'
        high_audio.write_bytes(b'audio payload deliberately not read'.ljust(RAW, b'\0'))
        self.gdi.write_text(self.gdi.read_text().replace('3\n', '4\n', 1)+
                            f'4 {SESSION+64} 0 2352 high-audio.bin 0\n')
        audio_paths = {self.source/'track02.raw', high_audio}
        before = self.hashes()
        original_open = Path.open

        def no_audio_open(path, *args, **kwargs):
            if path in audio_paths:
                raise AssertionError('Audio track payload must not be opened')
            return original_open(path, *args, **kwargs)

        with patch.object(Path, 'open', no_audio_open):
            entries, report = bundle.extract(self.gdi)
        self.assertEqual(report['audio_candidates']['low_density_tracks'], [2])
        self.assertEqual(report['audio_candidates']['high_density_tracks'], [4])
        self.assertFalse(report['audio_payload_copied'])
        self.assertFalse(any(name in entries for name in ('track02.raw', 'high-audio.bin')))
        self.assertEqual(self.hashes(), before)

    def test_corrupt_mode1_invalid_iso_unsafe_names_and_overlap_refused(self):
        self.established_checks('test_duplicate_versions_case_and_unsafe_names',
            'test_directory_cycle_or_repeated_extent_refused',
            'test_iso_dual_endianness_and_record_corruption',
            'test_unsupported_file_extent_flags_and_out_of_range',
            'test_malformed_descriptor_and_truncated_or_bad_framing',
            'test_gdi_and_track_symlinks_refused')
        self.fixture()
        self.gdi.write_text(self.gdi.read_text().replace('2 4 0', '2 3 0'))
        self.reject_read_only()

    def test_optional_omissions_and_traversal_remain_bounded(self):
        self.established_checks('test_all_resource_caps_bounded',
            'test_selected_file_count_boundary_excludes_generated_metadata',
            'test_optional_per_file_oversize_is_recorded_without_reading_asset',
            'test_optional_budget_omission_keeps_later_small_candidate',
            'test_dll_drv_candidates_precede_lib_within_byte_budget',
            'test_mandatory_byte_bounds_are_fatal_with_actionable_counts',
            'test_optional_empty_or_unsupported_extent_still_refused')

    def test_include_selects_only_requested_inventory_file(self):
        self.native_fixture()
        self.cook_data_tracks()
        before = self.hashes()
        ordinary_entries, _ = bundle.extract(self.gdi)
        entries, report = bundle.extract(self.gdi,
            include=('system/texture.bin', 'SYSTEM/TEXTURE.BIN'))
        self.assertEqual(set(entries)-set(ordinary_entries), {'SYSTEM/TEXTURE.BIN'})
        self.assertEqual(entries['SYSTEM/TEXTURE.BIN'],
                         b'unselected nested asset'.ljust(DATA, b'\0'))
        self.assertNotIn('SCENE.BIN', entries)
        self.assertEqual(report['selected_file_count'], len(self.contents)+1)
        self.assert_full_hashes(entries, report)
        with patch.object(bundle, 'FILE_COUNT_LIMIT', 1):
            with self.assertRaises(ValueError):
                bundle.extract(self.gdi, include=('SYSTEM/TEXTURE.BIN',))
        self.assertEqual(self.hashes(), before)

    def test_include_missing_directory_or_unsafe_path_refused_read_only(self):
        before = self.hashes()
        for path in ('MISSING.BIN', 'SYSTEM', '../SCENE.BIN', '/SCENE.BIN',
                     'SYSTEM//TEXTURE.BIN', 'SYSTEM/../SCENE.BIN', 'SYSTEM\\TEXTURE.BIN'):
            with self.subTest(path=path):
                with self.assertRaises(ValueError):
                    bundle.extract(self.gdi, include=(path,))
                self.assertEqual(self.hashes(), before)
                self.assertFalse(self.output.exists())

    def test_no_overwrite_and_atomic_read_only_publication(self):
        self.established_checks('test_overwrite_and_broken_symlink_refused',
            'test_partial_archive_never_published',
            'test_crc_or_readback_failure_never_published',
            'test_concurrent_file_or_symlink_creation_preserved')

    def test_cue_native_and_wince_raw_and_cooked_preserve_original_descriptor(self):
        for windows_ce in (False, True):
            for cooked in (False, True):
                with self.subTest(windows_ce=windows_ce, cooked=cooked):
                    cue = self.cue_fixture(windows_ce=windows_ce, cooked=cooked)
                    original = b'\xef\xbb\xbf'+cue.read_bytes().replace(b'\n', b'\r\n')
                    cue.write_bytes(original)
                    before = self.hashes()
                    entries, report = bundle.extract(cue)
                    self.assertEqual(report['descriptor_format'], 'cue')
                    self.assertEqual(report['windows_ce'], windows_ce)
                    self.assertEqual(report['session_lba'], SESSION)
                    self.assertEqual(report['data_encoding'],
                                     'cooked2048' if cooked else 'raw2352')
                    self.assertEqual(set(entries), {'IP.BIN', 'disc.cue'} | set(self.contents))
                    self.assertEqual(entries['disc.cue'], original)
                    for name, data in self.contents.items():
                        self.assertEqual(entries[name], data)
                    self.assert_full_hashes(entries, report)
                    self.assertEqual(self.hashes(), before)

    def test_cue_stored_and_nonstored_gaps_use_program_offsets(self):
        for stored_gap, pregap, expected_audio_start in ((2, 0, 8), (0, 3, 9)):
            with self.subTest(stored_gap=stored_gap, pregap=pregap):
                cue = self.cue_fixture(stored_gap=stored_gap, pregap=pregap, postgap=2)
                before = self.hashes()
                audio = self.source/'track02.bin'
                original_open = Path.open

                def no_audio_open(path, *args, **kwargs):
                    if path == audio:
                        raise AssertionError('Per-track audio BIN must not be opened')
                    return original_open(path, *args, **kwargs)

                with patch.object(Path, 'open', no_audio_open):
                    entries, report = bundle.extract(cue)
                tracks = report['tracks']
                self.assertEqual(tracks[0]['end_lba'], 4)
                self.assertEqual(tracks[1]['start_lba'], expected_audio_start)
                self.assertEqual(tracks[1]['end_lba'], expected_audio_start+4)
                self.assertEqual(tracks[1]['file_offset_bytes'], stored_gap*RAW)
                self.assertEqual(tracks[2]['start_lba'], SESSION)
                self.assertEqual(tracks[2]['file_offset_bytes'], 0)
                self.assertEqual(entries['1ST_READ.BIN'], self.contents['1ST_READ.BIN'])
                self.assertEqual(self.hashes(), before)

    def test_combined_cue_seeks_past_audio_and_infers_raw_msf_without_rem(self):
        for high_rem in (False, True):
            with self.subTest(high_rem=high_rem):
                cue = self.combined_cue_fixture(high_rem=high_rem)
                before = self.hashes()
                original_open = Path.open
                combined = self.combined
                reads = []

                class DataOnlyFile:
                    def __init__(self, source):
                        self.source = source

                    def __enter__(self):
                        return self

                    def __exit__(self, *args):
                        return self.source.__exit__(*args)

                    def __getattr__(self, name):
                        return getattr(self.source, name)

                    def read(self, size=-1):
                        start = self.source.tell()
                        end = combined.stat().st_size if size < 0 else start+size
                        if start < 8*RAW and end > 4*RAW:
                            raise AssertionError('Combined BIN audio payload must not be read')
                        reads.append((start, size))
                        return self.source.read(size)

                def data_only_open(path, *args, **kwargs):
                    source = original_open(path, *args, **kwargs)
                    return DataOnlyFile(source) if path == combined else source

                with patch.object(Path, 'open', data_only_open):
                    entries, report = bundle.extract(cue)
                self.assertEqual(report['session_lba'], SESSION)
                self.assertEqual(report['tracks'][1]['start_lba'], 4)
                self.assertEqual(report['tracks'][1]['end_lba'], 8)
                self.assertEqual(report['tracks'][2]['start_lba'], SESSION)
                self.assertEqual(report['tracks'][2]['file_offset_bytes'], 8*RAW)
                self.assertTrue(any(start == 8*RAW for start, _ in reads))
                self.assertEqual(entries['1ST_READ.BIN'], self.contents['1ST_READ.BIN'])
                self.assertEqual(entries['SYSTEM/AUDIO.DRV'], self.contents['SYSTEM/AUDIO.DRV'])
                self.assertFalse(report['audio_payload_copied'])
                self.assert_full_hashes(entries, report)
                self.assertEqual(self.hashes(), before)

    def test_cue_raw_msf_conflicts_invalid_bcd_and_unanchored_cooked_refused(self):
        for change in ('rem_conflict', 'invalid_bcd', 'unanchored_cooked'):
            with self.subTest(change=change):
                cue = self.cue_fixture(high_rem=(change == 'rem_conflict'),
                                       cooked=(change == 'unanchored_cooked'))
                if change == 'rem_conflict':
                    raw = bytearray(self.track.read_bytes())
                    for i in range(len(raw)//RAW):
                        raw[i*RAW+12:i*RAW+15] = raw_sector(SESSION+i+1, bytes(DATA))[12:15]
                    self.track.write_bytes(raw)
                elif change == 'invalid_bcd':
                    with self.track.open('r+b') as source:
                        source.seek(12)
                        source.write(b'\xfa\x02\x00')
                before = self.hashes()
                with self.assertRaises(ValueError):
                    bundle.extract(cue)
                self.assertEqual(self.hashes(), before)
                self.assertFalse(self.output.exists())

    def test_cue_malformed_missing_unsafe_reused_and_overlapping_indexes_refused(self):
        cue = self.combined_cue_fixture()
        original = cue.read_text()
        invalid = [original.replace('    INDEX 01 00:00:08\n', ''),
                   original.replace('    INDEX 01 00:00:08',
                                    '    INDEX 01 00:00:08\n    INDEX 01 00:00:09'),
                   original.replace('INDEX 01 00:00:08', 'INDEX 01 00:00:03'),
                   original.replace('INDEX 01 00:00:08', 'INDEX 01 00:60:00'),
                   original.replace('TRACK 03 MODE1/2352', 'TRACK 03 MODE2/2352'),
                   original.replace('"combined.bin"', '"../combined.bin"'),
                   original.replace('"combined.bin"', '"missing.bin"'),
                   original+'FILE "combined.bin" BINARY\n  TRACK 04 AUDIO\n'
                            '    INDEX 01 00:00:00\n']
        for text in invalid:
            with self.subTest(cue=text):
                cue.write_text(text)
                before = self.hashes()
                with self.assertRaises((ValueError, OSError)):
                    bundle.extract(cue)
                self.assertEqual(self.hashes(), before)
                self.assertFalse(self.output.exists())

    def test_cue_late_data_extended_minutes_never_reads_sparse_audio(self):
        cue = self.cue_fixture()
        tail_lba = 548000
        boot = self.contents['1ST_READ.BIN']
        tail = self.source/'tail.bin'
        tail.write_bytes(b''.join(raw_sector(tail_lba+i,
            boot[i*DATA:(i+1)*DATA].ljust(DATA, b'\0'))
            for i in range((len(boot)+DATA-1)//DATA)))
        # games_fixture intentionally encodes Dreamcast minute 121 as 0xC1.
        self.assertGreater(tail.read_bytes()[12] >> 4, 9)
        self.directory(20, self.root_records[:2]+[
            record(b'1ST_READ.BIN;1', tail_lba, len(boot))]+self.root_records[3:])
        audio = self.source/'long-audio.bin'
        with audio.open('wb') as source:
            source.truncate((tail_lba-(SESSION+64))*RAW)
        cue.write_text(cue.read_text()+
            'FILE "long-audio.bin" BINARY\n  TRACK 04 AUDIO\n    INDEX 01 00:00:00\n'
            'FILE "tail.bin" BINARY\n  TRACK 05 MODE1/2352\n    INDEX 01 00:00:00\n')

        def small_source_hashes():
            return {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in self.source.iterdir() if path != audio and path.is_file()}

        before = small_source_hashes()
        audio_before = audio.stat()
        original_open = Path.open

        def no_audio_open(path, *args, **kwargs):
            if path in (audio, self.source/'track02.bin'):
                raise AssertionError('Sparse audio payload must never be opened')
            return original_open(path, *args, **kwargs)

        with patch.object(Path, 'open', no_audio_open):
            entries, report = bundle.extract(cue)
        self.assertEqual(report['session_lba'], SESSION)
        self.assertEqual(report['tracks'][3]['end_lba'], tail_lba)
        self.assertEqual(report['tracks'][4]['start_lba'], tail_lba)
        self.assertEqual(entries['1ST_READ.BIN'], boot)
        self.assertEqual({item['path']: item['lba'] for item in report['files']}['1ST_READ.BIN'],
                         tail_lba)
        self.assert_full_hashes(entries, report)
        self.assertEqual(small_source_hashes(), before)
        audio_after = audio.stat()
        self.assertEqual((audio_after.st_size, audio_after.st_mtime_ns, audio_after.st_ino),
                         (audio_before.st_size, audio_before.st_mtime_ns, audio_before.st_ino))

    def test_cue_cli_and_failed_write_preserve_atomic_no_overwrite(self):
        cue = self.cue_fixture()
        before = self.hashes()
        command = [sys.executable, str(SCRIPT), str(cue), '--output', str(self.output)]
        result = subprocess.run(command, cwd=self.folder, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.read('disc.cue'), cue.read_bytes())
            self.assertNotIn('disc.gdi', archive.namelist())
        created = self.output.read_bytes()
        result = subprocess.run(command, cwd=self.folder, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.output.read_bytes(), created)
        self.output.unlink()
        original_write = zipfile.ZipFile.writestr
        calls = 0

        def failing_write(archive, *args, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError('controlled CUE bundle write failure')
            return original_write(archive, *args, **kwargs)

        with patch.object(zipfile.ZipFile, 'writestr', failing_write):
            with self.assertRaisesRegex(OSError, 'controlled CUE bundle write failure'):
                bundle.write_bundle(self.output, *bundle.extract(cue))
        self.assertFalse(self.output.exists())
        self.assertEqual(self.hashes(), before)
        self.no_staging_files()

    def test_cli_default_custom_include_version_and_no_overwrite(self):
        self.native_fixture()
        before = self.hashes()
        for args, destination in (([], self.folder/'CDDA-startup.zip'),
                                  (['--output', str(self.output)], self.output)):
            command = [sys.executable, str(SCRIPT), str(self.gdi),
                       '--include', 'SYSTEM/TEXTURE.BIN', *args]
            result = subprocess.run(command, cwd=self.folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            with zipfile.ZipFile(destination) as archive:
                self.assertIsNone(archive.testzip())
                self.assertIn('SYSTEM/TEXTURE.BIN', archive.namelist())
                saved = json.loads(archive.read('startup-info.json'))
                self.assertFalse(saved['windows_ce'])
                self.assertFalse(saved['audio_payload_copied'])
            created = destination.read_bytes()
            result = subprocess.run(command, cwd=self.folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(destination.read_bytes(), created)
        result = subprocess.run([sys.executable, str(SCRIPT), '--version'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('CDDA startup extractor', result.stdout)
        self.assertEqual(self.hashes(), before)


if __name__ == '__main__':
    unittest.main()
