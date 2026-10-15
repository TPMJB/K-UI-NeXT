#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic raw-GDI/ISO hierarchy tests; no retail game bytes or execution."""
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

from games_fixture import DATA, RAW, SESSION, dual32, make_fixture, record

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/kui-armada-startup-bundle.py'
SPEC = importlib.util.spec_from_file_location('armada_startup_bundle', SCRIPT)
bundle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bundle)


class ArmadaStartupBundleTests(unittest.TestCase):
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
        output = bytearray(DATA)
        offsets = []
        position = 0
        for item in entries:
            offsets.append(position)
            output[position:position+len(item)] = item
            position += len(item)
        self.payload(sector, 0, output)
        return offsets

    def large_root(self, extra_records):
        """Pack an original multi-sector directory without crossing records."""
        items = [record(b'\0', SESSION+40, DATA, True),
                 record(b'\1', SESSION+40, DATA, True)] + self.root_records[2:] + extra_records
        output = bytearray(DATA)
        position = 0
        for item in items:
            if len(item) > DATA-position % DATA:
                position = (position // DATA+1)*DATA
            required = position+len(item)
            if required > len(output):
                output.extend(bytes(DATA*((required+DATA-1)//DATA)-len(output)))
            output[position:required] = item
            position = required
        size = len(output)
        root = record(b'\0', SESSION+40, size, True)
        output[:len(root)] = root
        parent = record(b'\1', SESSION+40, size, True)
        output[len(root):len(root)+len(parent)] = parent
        self.payload(16, 156, root)
        for offset in range(0, size, DATA):
            self.payload(40+offset//DATA, 0, output[offset:offset+DATA])

    def mandatory_bytes(self):
        return 32768 + len(self.gdi.read_bytes()) + len(self.contents['0WINCEOS.BIN']) +\
               len(self.contents['ARMADA.EXE'])

    def fixture(self):
        make_fixture(self.source, title=b'ARMADA')
        self.payload(0, 56, b'0000001 ')
        self.payload(0, 64, b'T40301N   ')
        self.payload(0, 96, b'0WINCEOS.BIN    ')
        self.contents = {
            '0WINCEOS.BIN': bytes((i * 7 + 3) & 255 for i in range(4097)),
            'ARMADA.EXE': bytes((i * 13 + 7) & 255 for i in range(4096)),
            'SYSTEM/GRAPHICS.DLL': bytes((i * 5 + 11) & 255 for i in range(1103)),
            'SYSTEM/AUDIO.DRV': bytes((i * 3 + 17) & 255 for i in range(333)),
            'SYSTEM/ENGINE.LIB': bytes((i * 11 + 23) & 255 for i in range(451)),
            'SYSTEM/DEEP/EXTRA.DLL': bytes((i * 19 + 29) & 255 for i in range(113)),
        }
        self.sectors = {
            '0WINCEOS.BIN': 33, 'ARMADA.EXE': 22,
            'SYSTEM/GRAPHICS.DLL': 25, 'SYSTEM/AUDIO.DRV': 26,
            'SYSTEM/ENGINE.LIB': 27, 'SYSTEM/DEEP/EXTRA.DLL': 28,
        }
        for name, data in self.contents.items():
            start = self.sectors[name]
            for i in range((len(data)+DATA-1)//DATA):
                chunk = data[i*DATA:(i+1)*DATA]
                self.payload(start+i, 0, chunk.ljust(DATA, b'\0'))
        self.payload(24, 0, b'unselected scene asset'.ljust(DATA, b'\0'))
        self.payload(29, 0, b'unselected nested asset'.ljust(DATA, b'\0'))
        self.root_records = [
            record(b'\0', SESSION+20, DATA, True),
            record(b'\1', SESSION+20, DATA, True),
            record(b'0winceos.bin;1', SESSION+33, 4097),
            record(b'ArMaDa.ExE;1', SESSION+22, 4096),
            record(b'SCENE.BIN;1', SESSION+24, DATA),
            record(b'System', SESSION+30, DATA, True),
        ]
        self.root_offsets = self.directory(20, self.root_records)
        self.system_records = [
            record(b'\0', SESSION+30, DATA, True),
            record(b'\1', SESSION+20, DATA, True),
            record(b'Graphics.Dll;1', SESSION+25, 1103),
            record(b'Audio.Drv;1', SESSION+26, 333),
            record(b'Engine.Lib;1', SESSION+27, 451),
            record(b'Deep', SESSION+31, DATA, True),
            record(b'TEXTURE.BIN;1', SESSION+29, DATA),
        ]
        self.system_offsets = self.directory(30, self.system_records)
        self.directory(31, [
            record(b'\0', SESSION+31, DATA, True),
            record(b'\1', SESSION+30, DATA, True),
            record(b'Extra.Dll;1', SESSION+28, 113),
        ])

    def hashes(self):
        return {str(path.relative_to(self.source)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in self.source.rglob('*') if path.is_file()}

    def reject_read_only(self):
        before = self.hashes()
        with self.assertRaises((ValueError, OSError)):
            bundle.extract(self.gdi)
        self.assertEqual(self.hashes(), before)
        self.assertFalse(self.output.exists())

    def no_staging_files(self):
        self.assertEqual({path.name for path in self.folder.iterdir()},
                         {'original'} | ({self.output.name} if os.path.lexists(self.output) else set()))

    def test_exact_case_normalized_hierarchy_and_original_gdi(self):
        original_gdi = b'\xef\xbb\xbf' + self.gdi.read_bytes().replace(b'\n', b'\r\n')
        self.gdi.write_bytes(original_gdi)
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        raw = self.track.read_bytes()
        expected_ip = b''.join(raw[i*RAW+16:i*RAW+16+DATA] for i in range(16))
        self.assertEqual(set(entries), {'IP.BIN', 'disc.gdi'} | set(self.contents))
        self.assertEqual(entries['IP.BIN'], expected_ip)
        self.assertEqual(entries['disc.gdi'], original_gdi)
        for name, data in self.contents.items():
            self.assertEqual(entries[name], data)
        files = {item['path']: item for item in report['files']}
        for name, data in self.contents.items():
            self.assertEqual(files[name]['bytes'], len(data))
            self.assertEqual(files[name]['lba'], SESSION+self.sectors[name])
            self.assertEqual(files[name]['crc32'], f'{zlib.crc32(data):08x}')
            self.assertEqual(files[name]['sha256'], hashlib.sha256(data).hexdigest())
        self.assertEqual(report['boot_path'], '0WINCEOS.BIN')
        self.assertEqual(report['boot_candidates'], ['0WINCEOS.BIN'])
        self.assertEqual(report['application_status'], 'found')
        self.assertEqual(report['application_candidates'], ['ARMADA.EXE'])
        self.assertEqual(report['warnings'], [])
        self.assertEqual(report['candidate_file_count'], len(self.contents))
        self.assertEqual(report['selected_file_count'], len(self.contents))
        self.assertEqual(report['archive_file_count'], len(self.contents)+3)
        self.assertEqual(report['omitted_files'], [])
        inventory = {item['path']: item for item in report['inventory']}
        self.assertEqual(inventory['SCENE.BIN']['lba'], SESSION+24)
        self.assertEqual(inventory['SCENE.BIN']['bytes'], DATA)
        self.assertEqual(inventory['SCENE.BIN']['flags'], 0)
        self.assertEqual(inventory['SYSTEM']['flags'], 2)
        self.assertEqual(inventory['SYSTEM/TEXTURE.BIN']['lba'], SESSION+29)
        bundle.write_bundle(self.output, entries, report)
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(set(archive.namelist()), set(entries) | {'startup-info.json'})
            for name, data in entries.items():
                self.assertEqual(archive.read(name), data)
            self.assertEqual(json.loads(archive.read('startup-info.json')), report)
        self.assertEqual(self.hashes(), before)
        self.no_staging_files()

    def test_quoted_track_name_and_cross_track_file(self):
        raw = self.track.read_bytes()
        self.track.rename(self.source / 'data track.bin')
        renamed = self.source / 'data track.bin'
        renamed.write_bytes(raw[:23*RAW])
        (self.source / 'tail track.bin').write_bytes(raw[23*RAW:])
        text = self.gdi.read_text().replace('3\n', '4\n', 1)
        text = text.replace('track03.bin', '"data track.bin"')
        text += f'4 {SESSION+23} 4 2352 "tail track.bin" 0\n'
        self.gdi.write_text(text)
        before = self.hashes()
        entries, _ = bundle.extract(self.gdi)
        self.assertEqual(entries['ARMADA.EXE'], self.contents['ARMADA.EXE'])
        self.assertEqual(entries['SYSTEM/DEEP/EXTRA.DLL'], self.contents['SYSTEM/DEEP/EXTRA.DLL'])
        self.assertEqual(self.hashes(), before)

    def test_native_wrong_title_product_and_boot_refused(self):
        mutations = [(56, b'0000000 '), (128, b'NOT ARMADA'),
                     (64, b'T99999N   '), (96, b'1ST_READ.BIN    ')]
        for offset, value in mutations:
            with self.subTest(offset=offset):
                self.fixture()
                self.payload(0, offset, value)
                self.reject_read_only()

    def test_missing_required_kernel_still_refused(self):
        self.directory(20, self.root_records[:2]+self.root_records[3:])
        before = self.hashes()
        with self.assertRaisesRegex(ValueError, 'kernel|0WINCEOS'):
            bundle.extract(self.gdi)
        self.assertEqual(self.hashes(), before)
        self.assertFalse(self.output.exists())

    def test_nested_armada_executable_preserves_path(self):
        self.directory(20, self.root_records[:3]+self.root_records[4:])
        self.directory(30, self.system_records+[
            record(b'Armada.Exe;1', SESSION+22, len(self.contents['ARMADA.EXE']))])
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        self.assertNotIn('ARMADA.EXE', entries)
        self.assertEqual(entries['SYSTEM/ARMADA.EXE'], self.contents['ARMADA.EXE'])
        self.assertEqual(report['application_candidates'], ['SYSTEM/ARMADA.EXE'])
        self.assertEqual(report['application_status'], 'found')
        self.assertEqual(report['warnings'], [])
        self.assertEqual(self.hashes(), before)

    def test_differently_named_executable_discovered(self):
        alternate = record(b'MainGame.Exe;1', SESSION+22, len(self.contents['ARMADA.EXE']))
        self.directory(20, self.root_records[:3]+[alternate]+self.root_records[4:])
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        self.assertNotIn('ARMADA.EXE', entries)
        self.assertEqual(entries['MAINGAME.EXE'], self.contents['ARMADA.EXE'])
        self.assertEqual(report['application_candidates'], ['MAINGAME.EXE'])
        self.assertEqual(report['application_status'], 'found')
        self.assertEqual(report['warnings'], [])
        self.assertEqual(self.hashes(), before)

    def test_same_executable_basename_keeps_both_ambiguous_paths(self):
        self.directory(30, self.system_records+[
            record(b'Armada.Exe;1', SESSION+25, len(self.contents['SYSTEM/GRAPHICS.DLL']))])
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        self.assertEqual(entries['ARMADA.EXE'], self.contents['ARMADA.EXE'])
        self.assertEqual(entries['SYSTEM/ARMADA.EXE'], self.contents['SYSTEM/GRAPHICS.DLL'])
        self.assertNotEqual(entries['ARMADA.EXE'], entries['SYSTEM/ARMADA.EXE'])
        self.assertEqual(report['application_candidates'], ['ARMADA.EXE', 'SYSTEM/ARMADA.EXE'])
        self.assertEqual(report['application_status'], 'ambiguous')
        self.assertTrue(report['warnings'])
        bundle.write_bundle(self.output, entries, report)
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.read('ARMADA.EXE'), self.contents['ARMADA.EXE'])
            self.assertEqual(archive.read('SYSTEM/ARMADA.EXE'), self.contents['SYSTEM/GRAPHICS.DLL'])
        self.assertEqual(self.hashes(), before)

    def test_no_executable_produces_actionable_kernel_bundle(self):
        self.directory(20, self.root_records[:3]+self.root_records[4:])
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        self.assertEqual(entries['0WINCEOS.BIN'], self.contents['0WINCEOS.BIN'])
        self.assertFalse(any(path.endswith('.EXE') for path in entries))
        self.assertEqual(report['application_candidates'], [])
        self.assertEqual(report['application_status'], 'not_found')
        self.assertTrue(report['warnings'])
        self.assertIn('SCENE.BIN', {item['path'] for item in report['inventory']})
        bundle.write_bundle(self.output, entries, report)
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.read('0WINCEOS.BIN'), self.contents['0WINCEOS.BIN'])
            saved = json.loads(archive.read('startup-info.json'))
            self.assertEqual(saved['application_status'], 'not_found')
            self.assertTrue(saved['warnings'])
        self.assertEqual(self.hashes(), before)

    def test_unique_nested_kernel_discovered_from_ip_basename(self):
        self.directory(20, self.root_records[:2]+self.root_records[3:])
        self.directory(30, self.system_records+[
            record(b'0winceos.bin;1', SESSION+33, len(self.contents['0WINCEOS.BIN']))])
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        self.assertNotIn('0WINCEOS.BIN', entries)
        self.assertEqual(entries['SYSTEM/0WINCEOS.BIN'], self.contents['0WINCEOS.BIN'])
        self.assertEqual(report['boot_path'], 'SYSTEM/0WINCEOS.BIN')
        self.assertEqual(report['boot_candidates'], ['SYSTEM/0WINCEOS.BIN'])
        self.assertEqual(self.hashes(), before)

    def test_multiple_nonroot_kernels_refused_with_candidate_paths(self):
        self.directory(20, self.root_records[:2]+self.root_records[3:])
        self.directory(30, self.system_records+[
            record(b'0winceos.bin;1', SESSION+33, len(self.contents['0WINCEOS.BIN']))])
        self.directory(31, [record(b'\0', SESSION+31, DATA, True),
                            record(b'\1', SESSION+30, DATA, True),
                            record(b'0winceos.bin;1', SESSION+33, len(self.contents['0WINCEOS.BIN']))])
        before = self.hashes()
        with self.assertRaises(ValueError) as raised:
            bundle.extract(self.gdi)
        self.assertIn('SYSTEM/0WINCEOS.BIN', str(raised.exception))
        self.assertIn('SYSTEM/DEEP/0WINCEOS.BIN', str(raised.exception))
        self.assertEqual(self.hashes(), before)
        self.assertFalse(self.output.exists())

    def test_root_kernel_preferred_over_nested_basename(self):
        self.directory(30, self.system_records+[
            record(b'0winceos.bin;1', SESSION+33, len(self.contents['0WINCEOS.BIN']))])
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        self.assertEqual(report['boot_path'], '0WINCEOS.BIN')
        self.assertEqual(report['boot_candidates'], ['0WINCEOS.BIN', 'SYSTEM/0WINCEOS.BIN'])
        self.assertEqual(entries['0WINCEOS.BIN'], self.contents['0WINCEOS.BIN'])
        self.assertEqual(self.hashes(), before)

    def test_duplicate_versions_case_and_unsafe_names(self):
        for name in (b'ARMADA.EXE;2', b'armada.exe;1', b'0WINCEOS.BIN;2',
                     b'../EVIL.DLL;1', b'/EVIL.DLL;1', b'BAD\\EVIL.DRV;1',
                     b'.;1', b'..;1', b'BAD\x00.DLL;1'):
            with self.subTest(name=name):
                self.fixture()
                self.directory(20, self.root_records+[record(name, SESSION+25, 32)])
                self.reject_read_only()

    def test_directory_cycle_or_repeated_extent_refused(self):
        for name, sector in ((b'LOOP', 20), (b'ALIAS', 30)):
            with self.subTest(name=name):
                self.fixture()
                self.directory(20, self.root_records+[record(name, SESSION+sector, DATA, True)])
                self.reject_read_only()

    def test_iso_dual_endianness_and_record_corruption(self):
        changes = [(16, 130, b'\xfe'), (16, 156+6, b'\xfe'),
                   (16, 156+14, b'\xfe'),
                   (20, self.root_offsets[2]+6, b'\xfe'),
                   (20, self.root_offsets[2]+14, b'\xfe'),
                   (30, self.system_offsets[2]+6, b'\xfe'),
                   (20, self.root_offsets[2], b'\x01'),
                   (20, self.root_offsets[2]+32, b'\xff')]
        for sector, offset, value in changes:
            with self.subTest(sector=sector, offset=offset):
                self.fixture()
                self.payload(sector, offset, value)
                self.reject_read_only()
        self.fixture()
        padding = [record(f'F{i:03d}.BIN;1'.encode(), SESSION+24, 32)
                   for i in range(37)]
        offsets = self.directory(20, self.root_records+padding)
        self.payload(20, offsets[-1], b'\xff')
        self.reject_read_only()

    def test_unsupported_file_extent_flags_and_out_of_range(self):
        for offset, value in ((1, b'\1'), (25, b'\x80'), (26, b'\1'), (27, b'\1')):
            with self.subTest(offset=offset):
                self.fixture()
                self.payload(20, self.root_offsets[2]+offset, value)
                self.reject_read_only()
        self.fixture()
        item = bytearray(self.root_records[2])
        dual32(item, 2, SESSION+64)
        self.directory(20, self.root_records[:2]+[item]+self.root_records[3:])
        self.reject_read_only()

    def test_malformed_descriptor_and_truncated_or_bad_framing(self):
        original = self.gdi.read_text()
        descriptors = [original.replace('3\n', '4\n', 1),
                       original.replace('4 2352 track03', '4 2048 track03'),
                       original.replace('track03.bin 0', 'track03.bin 16'),
                       original.replace('track03.bin', '../track03.bin'),
                       original.replace('track03.bin', '/track03.bin'),
                       original.replace('2 4 0', '2 3 0')]
        for text in descriptors:
            with self.subTest(text=text):
                self.fixture()
                self.gdi.write_text(text)
                self.reject_read_only()
        self.fixture()
        self.track.write_bytes(self.track.read_bytes()[:-1])
        self.reject_read_only()
        for position, value in ((33*RAW+15, b'\2'), (22*RAW, b'X'), (30*RAW+15, b'\2')):
            with self.subTest(position=position):
                self.fixture()
                with self.track.open('r+b') as output:
                    output.seek(position)
                    output.write(value)
                self.reject_read_only()

    def test_gdi_and_track_symlinks_refused(self):
        for source in (self.gdi, self.track):
            with self.subTest(source=source.name):
                self.fixture()
                renamed = source.with_name(source.name+'.real')
                source.rename(renamed)
                source.symlink_to(renamed.name)
                self.reject_read_only()
                source.unlink()
                renamed.rename(source)

    def test_all_resource_caps_bounded(self):
        limits = {'TOTAL_LIMIT': 4500,
                  'DIR_LIMIT': DATA-1, 'DIRECTORY_COUNT_LIMIT': 2,
                  'SCAN_ENTRY_LIMIT': 4, 'TOTAL_DIR_LIMIT': DATA*2-1}
        for constant, value in limits.items():
            with self.subTest(constant=constant):
                self.fixture()
                with patch.object(bundle, constant, value):
                    self.reject_read_only()
        self.fixture()
        item = bytearray(self.root_records[2])
        dual32(item, 10, 32769)
        self.directory(20, self.root_records[:2]+[item]+self.root_records[3:])
        with patch.object(bundle, 'FILE_LIMIT', 32768):
            self.reject_read_only()
        # FILE_LIMIT also admits the fixed32KiB IP; exercise that exact boundary.
        self.fixture()
        data = bytes((i*13+7)&255 for i in range(32768))
        for i in range(len(data)//DATA):
            self.payload(40+i, 0, data[i*DATA:(i+1)*DATA])
        item = bytearray(self.root_records[3])
        dual32(item, 2, SESSION+40)
        dual32(item, 10, len(data))
        self.directory(20, self.root_records[:3]+[item]+self.root_records[4:])
        with patch.object(bundle, 'FILE_LIMIT', 32768):
            self.assertEqual(bundle.extract(self.gdi)[0]['ARMADA.EXE'], data)

    def test_more_than_64_candidate_files_create_valid_bounded_zip(self):
        extras = [record(f'OBJECT{i:03d}.LIB;1'.encode(), SESSION+25, 16)
                  for i in range(70)]
        self.large_root(extras)
        before = self.hashes()
        entries, report = bundle.extract(self.gdi)
        self.assertEqual(report['candidate_file_count'], 76)
        self.assertEqual(report['selected_file_count'], 76)
        self.assertEqual(report['archive_file_count'], 79)
        self.assertEqual(report['omitted_files'], [])
        self.assertLessEqual(report['selected_bytes'], 32*1024*1024)
        for i in range(70):
            self.assertEqual(entries[f'OBJECT{i:03d}.LIB'],
                             self.contents['SYSTEM/GRAPHICS.DLL'][:16])
        bundle.write_bundle(self.output, entries, report)
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(len(archive.namelist()), 79)
            self.assertIsNone(archive.testzip())
            self.assertEqual(json.loads(archive.read('startup-info.json')), report)
        self.assertEqual(self.hashes(), before)

    def test_selected_file_count_boundary_excludes_generated_metadata(self):
        before = self.hashes()
        with patch.object(bundle, 'FILE_COUNT_LIMIT', 6):
            entries, report = bundle.extract(self.gdi)
        self.assertEqual(report['selected_file_count'], 6)
        self.assertEqual(report['archive_file_count'], 9)
        self.assertEqual(report['omitted_files'], [])
        self.assertEqual(set(entries), {'IP.BIN', 'disc.gdi'} | set(self.contents))
        with patch.object(bundle, 'FILE_COUNT_LIMIT', 5):
            entries, report = bundle.extract(self.gdi)
        self.assertEqual(report['selected_file_count'], 5)
        self.assertEqual(report['archive_file_count'], 8)
        self.assertEqual(report['omitted_files'], [{
            'path': 'SYSTEM/ENGINE.LIB', 'lba': SESSION+27,
            'bytes': 451, 'reason': 'file_count_limit'}])
        self.assertNotIn('SYSTEM/ENGINE.LIB', entries)
        self.assertTrue(report['warnings'])
        with patch.object(bundle, 'FILE_COUNT_LIMIT', 1):
            with self.assertRaises(ValueError) as raised:
                bundle.extract(self.gdi)
        self.assertIn('2', str(raised.exception))
        self.assertIn('1', str(raised.exception))
        self.assertEqual(self.hashes(), before)

    def test_optional_per_file_oversize_is_recorded_without_reading_asset(self):
        self.directory(20, self.root_records+[
            record(b'HUGE.LIB;1', SESSION+60, 32769),
            record(b'LATER.DRV;1', SESSION+25, 16)])
        # An omitted asset's bytes are not read or trusted. Keep the selected
        # startup framing intact while making its hypothetical first sector bad.
        with self.track.open('r+b') as output:
            output.seek(60*RAW+15)
            output.write(b'\x02')
        before = self.hashes()
        with patch.object(bundle, 'FILE_LIMIT', 32768):
            entries, report = bundle.extract(self.gdi)
        self.assertNotIn('HUGE.LIB', entries)
        self.assertEqual(entries['LATER.DRV'], self.contents['SYSTEM/GRAPHICS.DLL'][:16])
        self.assertEqual(report['omitted_files'], [{
            'path': 'HUGE.LIB', 'lba': SESSION+60,
            'bytes': 32769, 'reason': 'per_file_limit'}])
        self.assertIn('HUGE.LIB', {item['path'] for item in report['inventory']})
        self.assertTrue(report['warnings'])
        self.assertEqual(self.hashes(), before)

    def test_optional_budget_omission_keeps_later_small_candidate(self):
        self.directory(20, self.root_records+[
            record(b'A-BIG.DLL;1', SESSION+24, 1200),
            record(b'Z-SMALL.DRV;1', SESSION+25, 40)])
        before = self.hashes()
        with patch.object(bundle, 'TOTAL_LIMIT', self.mandatory_bytes()+1000):
            entries, report = bundle.extract(self.gdi)
        omitted = {item['path']: item for item in report['omitted_files']}
        self.assertEqual(omitted['A-BIG.DLL'], {
            'path': 'A-BIG.DLL', 'lba': SESSION+24,
            'bytes': 1200, 'reason': 'total_byte_budget'})
        self.assertNotIn('A-BIG.DLL', entries)
        self.assertEqual(entries['Z-SMALL.DRV'], self.contents['SYSTEM/GRAPHICS.DLL'][:40])
        self.assertEqual(entries['SYSTEM/ENGINE.LIB'], self.contents['SYSTEM/ENGINE.LIB'])
        self.assertLessEqual(report['selected_bytes'], self.mandatory_bytes()+1000)
        self.assertTrue(report['warnings'])
        self.assertEqual(self.hashes(), before)

    def test_dll_drv_candidates_precede_lib_within_byte_budget(self):
        before = self.hashes()
        size = self.mandatory_bytes()+1103+333+113
        with patch.object(bundle, 'TOTAL_LIMIT', size):
            entries, report = bundle.extract(self.gdi)
        self.assertIn('SYSTEM/GRAPHICS.DLL', entries)
        self.assertIn('SYSTEM/AUDIO.DRV', entries)
        self.assertIn('SYSTEM/DEEP/EXTRA.DLL', entries)
        self.assertNotIn('SYSTEM/ENGINE.LIB', entries)
        self.assertEqual(report['selected_bytes'], size)
        self.assertEqual(report['omitted_files'][0]['reason'], 'total_byte_budget')
        self.assertEqual(self.hashes(), before)

    def test_mandatory_byte_bounds_are_fatal_with_actionable_counts(self):
        before = self.hashes()
        with patch.object(bundle, 'TOTAL_LIMIT', self.mandatory_bytes()-1):
            with self.assertRaises(ValueError) as raised:
                bundle.extract(self.gdi)
        self.assertIn(f'{self.mandatory_bytes():,}', str(raised.exception))
        self.assertIn(f'{self.mandatory_bytes()-1:,}', str(raised.exception))
        for index, path in ((2, '0WINCEOS.BIN'), (3, 'ARMADA.EXE')):
            with self.subTest(path=path):
                self.fixture()
                changed = bytearray(self.root_records[index])
                dual32(changed, 10, 32769)
                self.directory(20, self.root_records[:index]+[changed]+self.root_records[index+1:])
                with patch.object(bundle, 'FILE_LIMIT', 32768):
                    with self.assertRaises(ValueError) as raised:
                        bundle.extract(self.gdi)
                self.assertIn(path, str(raised.exception).upper())
                self.assertIn('32,769', str(raised.exception))
                self.assertIn('32,768', str(raised.exception))
        self.fixture()
        self.assertEqual(self.hashes(), before)

    def test_optional_empty_or_unsupported_extent_still_refused(self):
        for size, flags in ((0, 0), (451, 0x80)):
            with self.subTest(size=size, flags=flags):
                self.fixture()
                changed = record(b'Engine.Lib;1', SESSION+27, size)
                changed[25] = flags
                self.directory(30, self.system_records[:4]+[changed]+self.system_records[5:])
                before = self.hashes()
                with self.assertRaises(ValueError):
                    bundle.extract(self.gdi)
                self.assertEqual(self.hashes(), before)
                self.assertFalse(self.output.exists())

    def test_overwrite_and_broken_symlink_refused(self):
        extracted = bundle.extract(self.gdi)
        before = self.hashes()
        self.output.write_bytes(b'original output')
        with self.assertRaises(ValueError):
            bundle.write_bundle(self.output, *extracted)
        self.assertEqual(self.output.read_bytes(), b'original output')
        self.output.unlink()
        self.output.symlink_to('absent-target')
        with self.assertRaises(ValueError):
            bundle.write_bundle(self.output, *extracted)
        self.assertTrue(self.output.is_symlink())
        self.assertFalse((self.folder / 'absent-target').exists())
        self.assertEqual(self.hashes(), before)
        self.no_staging_files()

    def test_partial_archive_never_published(self):
        before = self.hashes()
        original = zipfile.ZipFile.writestr
        calls = 0

        def failing_write(archive, *args, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError('controlled write failure')
            return original(archive, *args, **kwargs)

        with patch.object(zipfile.ZipFile, 'writestr', failing_write):
            with self.assertRaisesRegex(OSError, 'controlled write failure'):
                bundle.write_bundle(self.output, *bundle.extract(self.gdi))
        self.assertFalse(self.output.exists())
        self.assertEqual(self.hashes(), before)
        self.no_staging_files()

    def test_crc_or_readback_failure_never_published(self):
        before = self.hashes()
        with patch.object(zipfile.ZipFile, 'testzip', return_value='IP.BIN'):
            with self.assertRaises(ValueError):
                bundle.write_bundle(self.output, *bundle.extract(self.gdi))
        self.assertFalse(self.output.exists())
        self.no_staging_files()
        original = zipfile.ZipFile.read

        def mismatching_read(archive, name, *args, **kwargs):
            data = original(archive, name, *args, **kwargs)
            return data+b'X' if name == 'IP.BIN' else data

        with patch.object(zipfile.ZipFile, 'read', mismatching_read):
            with self.assertRaises(ValueError):
                bundle.write_bundle(self.output, *bundle.extract(self.gdi))
        self.assertFalse(self.output.exists())
        self.assertEqual(self.hashes(), before)
        self.no_staging_files()

    def test_concurrent_file_or_symlink_creation_preserved(self):
        original = os.link
        before = self.hashes()
        for symlink in (False, True):
            with self.subTest(symlink=symlink):
                def competing_output(stage, destination, *args, **kwargs):
                    destination = Path(destination)
                    if symlink:
                        destination.symlink_to('absent-target')
                    else:
                        destination.write_bytes(b'concurrent output')
                    return original(stage, destination, *args, **kwargs)

                with patch.object(bundle.os, 'link', competing_output):
                    with self.assertRaises(FileExistsError):
                        bundle.write_bundle(self.output, *bundle.extract(self.gdi))
                if symlink:
                    self.assertTrue(self.output.is_symlink())
                    self.assertFalse((self.folder / 'absent-target').exists())
                else:
                    self.assertEqual(self.output.read_bytes(), b'concurrent output')
                self.assertEqual(self.hashes(), before)
                self.no_staging_files()
                self.output.unlink()

    def test_cli_default_explicit_and_read_only_failure(self):
        before = self.hashes()
        result = subprocess.run([sys.executable, str(SCRIPT), str(self.gdi)],
                                cwd=self.folder, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        default = self.folder / 'Armada-startup.zip'
        self.assertTrue(default.exists())
        created = default.read_bytes()
        result = subprocess.run([sys.executable, str(SCRIPT), str(self.gdi)],
                                cwd=self.folder, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(default.read_bytes(), created)
        default.unlink()
        result = subprocess.run([sys.executable, str(SCRIPT), str(self.gdi),
                                 '--output', str(self.output)],
                                cwd=self.folder, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
        self.assertEqual(self.hashes(), before)
        self.output.unlink()
        self.payload(0, 56, b'0000000 ')
        before = self.hashes()
        result = subprocess.run([sys.executable, str(SCRIPT), str(self.gdi)],
                                cwd=self.folder, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertFalse(default.exists())
        self.assertEqual(self.hashes(), before)
        self.no_staging_files()


    def test_cli_version_identifies_corrected_helper_without_disc(self):
        result = subprocess.run([sys.executable, str(SCRIPT), '--version'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.strip(), 'K-UI Armada startup extractor v2')
        self.assertEqual(result.stderr, '')


if __name__ == '__main__':
    unittest.main()
