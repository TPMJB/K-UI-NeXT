#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Native startup extraction using original, non-executable raw-GDI fixtures."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import zlib

from games_fixture import DATA, RAW, SESSION, make_fixture, record

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/kui-native-startup-bundle.py'
SPEC = importlib.util.spec_from_file_location('native_startup_bundle', SCRIPT)
bundle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bundle)


class NativeStartupBundleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.source = self.folder / 'original'
        self.gdi = self.source / 'disc.gdi'
        self.track = self.source / 'track03.bin'
        self.output = self.folder / 'startup.zip'
        self.fixture()

    def payload(self, sector, offset, content):
        with self.track.open('r+b') as output:
            output.seek(sector * RAW + 16 + offset)
            output.write(content)

    def directory(self, sector, entries):
        data = bytearray(DATA)
        position, offsets = 0, []
        for entry in entries:
            offsets.append(position)
            data[position:position + len(entry)] = entry
            position += len(entry)
        self.payload(sector, 0, data)
        return offsets

    def fixture(self):
        make_fixture(self.source, title=b'Independent Native Startup Fixture')
        self.boot = (bytes((i * 17 + 3) & 255 for i in range(DATA)) +
                     bytes((i * 29 + 7) & 255 for i in range(DATA)))
        self.root_records = [
            record(b'\0', SESSION + 20, DATA, True),
            record(b'\1', SESSION + 20, DATA, True),
            record(b'1st_read.bin;1', SESSION + 21, len(self.boot)),
            record(b'UNRELATED.EXE;1', SESSION + 24, 100),
            record(b'System', SESSION + 30, DATA, True)]
        self.root_offsets = self.directory(20, self.root_records)
        self.system_records = [
            record(b'\0', SESSION + 30, DATA, True),
            record(b'\1', SESSION + 20, DATA, True),
            record(b'GRAPHICS.DLL;1', SESSION + 24, 100),
            record(b'AUDIO.DRV;1', SESSION + 24, 100),
            record(b'ENGINE.LIB;1', SESSION + 24, 100),
            record(b'TEXTURE.BIN;1', SESSION + 24, 100)]
        self.directory(30, self.system_records)

    def hashes(self):
        return {str(path.relative_to(self.source)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in self.source.rglob('*') if path.is_file()}

    def reject_read_only(self, expression=None):
        before = self.hashes()
        with self.assertRaisesRegex((ValueError, OSError), expression or '.'):
            bundle.extract(self.gdi)
        self.assertEqual(self.hashes(), before)
        self.assertFalse(self.output.exists())

    def no_staging_files(self):
        self.assertEqual({path.name for path in self.folder.iterdir()},
                         {'original'} | ({self.output.name} if os.path.lexists(self.output) else set()))

    def test_only_native_startup_exact_bytes_and_metadata_hashes(self):
        original = b'\xef\xbb\xbf' + self.gdi.read_bytes().replace(b'\n', b'\r\n')
        self.gdi.write_bytes(original)
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        self.assertEqual(set(entries), {'IP.BIN', 'disc.gdi', '1ST_READ.BIN'})
        raw = self.track.read_bytes()
        ip = b''.join(raw[i * RAW + 16:i * RAW + 16 + DATA] for i in range(16))
        self.assertEqual(entries['IP.BIN'], ip)
        self.assertEqual(entries['disc.gdi'], original)
        self.assertEqual(entries['1ST_READ.BIN'], self.boot)
        self.assertFalse(report['windows_ce'])
        self.assertEqual(report['title'], 'Independent Native Startup Fixture')
        self.assertEqual(report['product'], 'KUI-TEST')
        self.assertEqual(report['version'], 'V1.000')
        self.assertEqual(report['region'], 'JUE')
        self.assertEqual(report['boot_candidates'], ['1ST_READ.BIN'])
        self.assertEqual(report['archive_file_count'], 4)
        self.assertFalse(report['audio_payload_copied'])
        self.assertEqual(report['tracks'][2]['file_bytes'], 64 * RAW)
        files = {item['path']: item for item in report['files']}
        for name, data in entries.items():
            self.assertEqual(files[name]['bytes'], len(data))
            self.assertEqual(files[name]['sha256'], hashlib.sha256(data).hexdigest())
            self.assertEqual(files[name]['crc32'], f'{zlib.crc32(data):08x}')
        self.assertEqual(files['1ST_READ.BIN']['lba'], SESSION + 21)
        pvd = raw[16 * RAW + 16:16 * RAW + 16 + DATA]
        self.assertEqual(report['iso_metadata']['primary_volume_descriptor']['sha256'],
                         hashlib.sha256(pvd).hexdigest())
        self.assertEqual([item['path'] for item in report['iso_metadata']['directories']],
                         ['/', 'SYSTEM/'])
        bundle.write_bundle(self.output, entries, report)
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(set(archive.namelist()), set(entries) | {'startup-info.json'})
            for name, data in entries.items():
                self.assertEqual(archive.read(name), data)
            self.assertEqual(json.loads(archive.read('startup-info.json')), report)
        self.assertEqual(self.hashes(), before)
        self.no_staging_files()

    def test_ip_selects_other_boot_and_accepts_non_ce_peripheral_bits(self):
        self.payload(0, 56, b'0000010 ')
        self.payload(0, 96, b'CUSTOM.EXE'.ljust(16))
        self.directory(20, self.root_records[:2] + [
            record(b'CuStOm.ExE;1', SESSION + 21, 17)] + self.root_records[3:])
        entries, report = bundle.extract(self.gdi)
        self.assertEqual(set(entries), {'IP.BIN', 'disc.gdi', 'CUSTOM.EXE'})
        self.assertEqual(entries['CUSTOM.EXE'], self.boot[:17])
        self.assertEqual(report['peripheral_flags'], 16)

    def test_unrelated_runtime_assets_and_audio_are_never_read(self):
        with self.track.open('r+b') as output:
            output.seek(24 * RAW)
            output.write(b'corrupt unselected sector')
        before = self.hashes()
        original_open = Path.open

        def no_audio_open(path, *args, **kwargs):
            if path.suffix.lower() == '.raw':
                raise AssertionError('Audio payload must not be opened')
            return original_open(path, *args, **kwargs)

        with patch.object(Path, 'open', no_audio_open):
            entries, _ = bundle.extract(self.gdi)
        self.assertEqual(set(entries), {'IP.BIN', 'disc.gdi', '1ST_READ.BIN'})
        self.assertEqual(self.hashes(), before)

    def test_unique_nested_boot_and_all_duplicate_basename_ambiguity(self):
        self.directory(20, self.root_records[:2] + self.root_records[3:])
        nested = record(b'1ST_READ.BIN;1', SESSION + 21, len(self.boot))
        self.directory(30, self.system_records + [nested])
        entries, report = bundle.extract(self.gdi)
        self.assertEqual(report['boot_path'], 'SYSTEM/1ST_READ.BIN')
        self.assertEqual(entries['SYSTEM/1ST_READ.BIN'], self.boot)
        self.directory(20, self.root_records)
        self.reject_read_only('Ambiguous.*1ST_READ.BIN.*SYSTEM/1ST_READ.BIN')
        self.directory(20, self.root_records[:2] + self.root_records[3:])
        self.directory(30, self.system_records + [nested,
                       record(b'DEEP', SESSION + 31, DATA, True)])
        self.directory(31, [record(b'\0', SESSION + 31, DATA, True),
                            record(b'\1', SESSION + 30, DATA, True), nested])
        self.reject_read_only('Ambiguous')

    def test_ce_missing_invalid_ip_and_reserved_names_refused(self):
        for offset, value in ((56, b'0000001 '), (56, b'0000011 '),
                              (56, b'00000G0 '), (63, b'0'), (0, b'X'),
                              (96, b'ABSENT.BIN'.ljust(16)),
                              (96, b'../BOOT.BIN'.ljust(16))):
            with self.subTest(offset=offset, value=value):
                self.fixture()
                self.payload(0, offset, value)
                self.reject_read_only()
        for name in (b'IP.BIN', b'DISC.GDI'):
            with self.subTest(name=name):
                self.fixture()
                self.payload(0, 96, name.ljust(16))
                self.directory(20, self.root_records[:2] + [
                    record(name + b';1', SESSION + 21, 100)])
                self.reject_read_only('metadata')
        self.fixture()
        self.directory(20, self.root_records[:2] + [
            record(b'STARTUP-INFO.JSON', SESSION + 30, DATA, True)])
        self.directory(30, self.system_records + [self.root_records[2]])
        self.reject_read_only('metadata')

    def test_unsafe_duplicate_and_unsupported_versions_refused(self):
        for name in (b'1ST_READ.BIN;2', b'1st_read.bin;1', b'../EVIL.BIN;1',
                     b'/EVIL.BIN;1', b'BAD\\EVIL.BIN;1', b'BAD:EVIL;1',
                     b'.;1', b'..;1', b'BAD\0.BIN;1', b'BAD\xff.BIN;1'):
            with self.subTest(name=name):
                self.fixture()
                self.directory(20, self.root_records + [record(name, SESSION + 24, 10)])
                self.reject_read_only()

    def test_selected_paths_cannot_alias_metadata_or_parents_on_windows(self):
        self.payload(0, 96, b'IP.BIN.'.ljust(16))
        self.directory(20, self.root_records[:2] + [
            record(b'IP.BIN.;1', SESSION + 21, len(self.boot))])
        self.reject_read_only('unsafe')
        for parent in (b'.. ', b'IP.BIN.', b'DISC.GDI '):
            with self.subTest(parent=parent):
                self.fixture()
                self.directory(20, self.root_records[:2] + [
                    record(parent, SESSION + 30, DATA, True)])
                self.directory(30, self.system_records + [self.root_records[2]])
                self.reject_read_only('unsafe')

    def test_selected_extent_guards_and_bounds(self):
        for field, value in ((1, b'\1'), (25, b'\x80'), (26, b'\1'), (27, b'\1')):
            with self.subTest(field=field):
                self.fixture()
                self.payload(20, self.root_offsets[2] + field, value)
                self.reject_read_only()
        for extent, size in ((SESSION + 64, 1), (0, 1), (SESSION + 21, 0),
                             (SESSION + 21, bundle.FILE_LIMIT + 1)):
            with self.subTest(extent=extent, size=size):
                self.fixture()
                self.directory(20, self.root_records[:2] + [
                    record(b'1ST_READ.BIN;1', extent, size)] + self.root_records[3:])
                self.reject_read_only()

    def test_directory_corruption_cycles_and_traversal_caps(self):
        changes = ((16, 130, b'\xfe'), (16, 156 + 6, b'\xfe'),
                   (16, 156 + 25, b'\0'), (20, self.root_offsets[2] + 14, b'\xfe'),
                   (20, self.root_offsets[2], b'\1'),
                   (20, self.root_offsets[2] + 32, b'\xff'))
        for sector, offset, value in changes:
            with self.subTest(sector=sector, offset=offset):
                self.fixture()
                self.payload(sector, offset, value)
                self.reject_read_only()
        for name, sector in ((b'LOOP', 20), (b'ALIAS', 30)):
            self.fixture()
            self.directory(20, self.root_records + [record(name, SESSION + sector, DATA, True)])
            self.reject_read_only('cycle|aliased')
        self.fixture()
        for limit, value in (('DIR_LIMIT', DATA - 1), ('TOTAL_DIR_LIMIT', DATA),
                             ('DIRECTORY_COUNT_LIMIT', 1), ('SCAN_ENTRY_LIMIT', 2)):
            with self.subTest(limit=limit), patch.object(bundle, limit, value):
                self.reject_read_only('limit')

    def test_gdi_metadata_framing_and_symlinks_refused(self):
        original = self.gdi.read_text()
        changes = (original.replace('3\n', '4\n', 1),
                   original.replace('2352 track03', '2048 track03'),
                   original.replace('track03.bin 0', 'track03.bin 16'),
                   original.replace('track03.bin', '../track03.bin'),
                   original.replace('track03.bin', '/track03.bin'),
                   original.replace('2 4 0', '2 3 0'),
                   '100\n', '')
        for descriptor in changes:
            with self.subTest(descriptor=descriptor):
                self.fixture()
                self.gdi.write_text(descriptor)
                self.reject_read_only()
        self.fixture()
        self.track.write_bytes(self.track.read_bytes()[:-1])
        self.reject_read_only()
        for position in (0, 16 * RAW + 15, 21 * RAW, 30 * RAW + 15):
            with self.subTest(position=position):
                self.fixture()
                with self.track.open('r+b') as output:
                    output.seek(position)
                    output.write(b'X')
                self.reject_read_only()
        self.fixture()
        renamed = self.track.with_name('real-track.bin')
        self.track.rename(renamed)
        self.track.symlink_to(renamed.name)
        self.reject_read_only('symlink')
        self.track.unlink()
        renamed.rename(self.track)
        descriptor = self.gdi.with_name('original.gdi')
        self.gdi.rename(descriptor)
        self.gdi.symlink_to(descriptor.name)
        self.reject_read_only('symlink')

    def test_quoted_contiguous_tracks_work_but_audio_or_gap_fail(self):
        raw = self.track.read_bytes()
        self.track.rename(self.source / 'data track.bin')
        (self.source / 'data track.bin').write_bytes(raw[:22 * RAW])
        (self.source / 'tail track.bin').write_bytes(raw[22 * RAW:])
        descriptor = self.gdi.read_text().replace('3\n', '4\n', 1)
        descriptor = descriptor.replace('track03.bin', '"data track.bin"')
        descriptor += f'4 {SESSION + 22} 4 2352 "tail track.bin" 0\n'
        self.gdi.write_text(descriptor)
        before = self.hashes()
        entries, _ = bundle.extract(self.gdi)
        self.assertEqual(entries['1ST_READ.BIN'], self.boot)
        self.assertEqual(self.hashes(), before)
        self.gdi.write_text(descriptor.replace(f'4 {SESSION + 22} 4', f'4 {SESSION + 22} 0'))
        self.reject_read_only('data tracks')
        self.gdi.write_text(descriptor.replace(f'4 {SESSION + 22} 4', f'4 {SESSION + 23} 4'))
        self.reject_read_only('data tracks')

    def test_output_no_overwrite_even_with_symlink_or_publication_race(self):
        extracted = bundle.extract(self.gdi)
        before = self.hashes()
        for symlink in (False, True):
            with self.subTest(symlink=symlink):
                if symlink:
                    self.output.symlink_to('absent-target')
                else:
                    self.output.write_bytes(b'original output')
                with self.assertRaises(ValueError):
                    bundle.write_bundle(self.output, *extracted)
                self.assertEqual(self.output.is_symlink(), symlink)
                if not symlink:
                    self.assertEqual(self.output.read_bytes(), b'original output')
                self.output.unlink()
        original_link = os.link

        def competing_output(stage, destination):
            Path(destination).write_bytes(b'concurrent output')
            return original_link(stage, destination)

        with patch.object(bundle.os, 'link', competing_output):
            with self.assertRaises(FileExistsError):
                bundle.write_bundle(self.output, *extracted)
        self.assertEqual(self.output.read_bytes(), b'concurrent output')
        self.assertFalse((self.folder / 'absent-target').exists())
        self.assertEqual(self.hashes(), before)
        self.no_staging_files()

    def test_partial_zip_or_failed_integrity_never_published(self):
        before = self.hashes()
        original_write = zipfile.ZipFile.writestr
        calls = 0

        def failing_write(archive, *args, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError('controlled write failure')
            return original_write(archive, *args, **kwargs)

        with patch.object(zipfile.ZipFile, 'writestr', failing_write):
            with self.assertRaisesRegex(OSError, 'controlled write failure'):
                bundle.write_bundle(self.output, *bundle.extract(self.gdi))
        self.assertFalse(self.output.exists())
        self.no_staging_files()
        for method, result in (('testzip', 'IP.BIN'), ('read', b'wrong bytes')):
            with self.subTest(method=method), patch.object(zipfile.ZipFile, method, return_value=result):
                with self.assertRaises(ValueError):
                    bundle.write_bundle(self.output, *bundle.extract(self.gdi))
            self.assertFalse(self.output.exists())
            self.no_staging_files()
        self.assertEqual(self.hashes(), before)

    def test_cli_default_custom_version_no_overwrite_and_ce_failure(self):
        before = self.hashes()
        for args, destination in (([], self.folder / 'Native-startup.zip'),
                                  (['--output', 'Silver-startup.zip'], self.folder / 'Silver-startup.zip')):
            result = subprocess.run([sys.executable, str(SCRIPT), str(self.gdi), *args],
                                    cwd=self.folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            with zipfile.ZipFile(destination) as archive:
                self.assertEqual(set(archive.namelist()),
                                 {'IP.BIN', '1ST_READ.BIN', 'disc.gdi', 'startup-info.json'})
            created = destination.read_bytes()
            result = subprocess.run([sys.executable, str(SCRIPT), str(self.gdi), *args],
                                    cwd=self.folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(destination.read_bytes(), created)
            destination.unlink()
        self.assertEqual(self.hashes(), before)
        self.payload(0, 56, b'0000001 ')
        before = self.hashes()
        result = subprocess.run([sys.executable, str(SCRIPT), str(self.gdi)],
                                cwd=self.folder, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn('Windows CE', result.stderr)
        self.assertEqual(self.hashes(), before)
        self.no_staging_files()
        result = subprocess.run([sys.executable, str(SCRIPT), '--version'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'K-UI native startup extractor v1')


if __name__ == '__main__':
    unittest.main()
