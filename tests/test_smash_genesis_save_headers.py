#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise header-only collection with authored, non-executable GDI data."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from games_fixture import DATA, RAW, SESSION, make_fixture, record

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/kui-smash-genesis-save-headers.py'
SPEC = importlib.util.spec_from_file_location('smash_genesis_headers', SCRIPT)
bundle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bundle)


class HeaderCollectorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)
        self.source = self.folder / 'original'
        self.output = self.folder / 'headers.zip'
        make_fixture(self.source, title=b'Independent Genesis Save Header Fixture')
        self.gdi = self.source / 'disc.gdi'
        self.track = self.source / 'track03.bin'
        self.roms = {'ONE/ONE.SGA': DATA * 2, 'TWO/TWO.SGA': DATA * 2}
        self.addCleanup(patch.stopall)
        patch.object(bundle, 'ROMS', self.roms).start()
        self.first = self.header(b'FIRST SYNTHETIC GAME', 0xf8, 0x3f)
        self.second = self.header(b'SECOND SYNTHETIC GAME', 0, 0)
        self.root = [record(b'\0', SESSION + 20, DATA, True),
                     record(b'\1', SESSION + 20, DATA, True),
                     record(b'1ST_READ.BIN;1', SESSION + 21, DATA * 2),
                     record(b'one', SESSION + 30, DATA, True),
                     record(b'TWO', SESSION + 31, DATA, True)]
        self.one = [record(b'\0', SESSION + 30, DATA, True),
                    record(b'\1', SESSION + 20, DATA, True),
                    record(b'one.sga;1', SESSION + 40, DATA * 2)]
        self.two = [record(b'\0', SESSION + 31, DATA, True),
                    record(b'\1', SESSION + 20, DATA, True),
                    record(b'TWO.SGA;1', SESSION + 42, DATA * 2)]
        self.directory(20, self.root)
        self.directory(30, self.one)
        self.directory(31, self.two)
        self.payload(40, 0, self.first)
        self.payload(42, 0, self.second)
        ip = self.read_payload(0, 16 * DATA)
        self.ip_hash = hashlib.sha256(ip).hexdigest()

    @staticmethod
    def header(title, kind, high):
        data = bytearray((i * 37 + 11) & 255 for i in range(512))
        data[0x100:0x110] = b'SEGA GENESIS    '
        data[0x120:0x150] = title.ljust(48, b' ')
        data[0x150:0x180] = title.ljust(48, b' ')
        data[0x1b0:0x1bc] = b'\0' * 12
        if kind:
            data[0x1b0:0x1b4] = b'RA' + bytes((kind, 0x20))
            data[0x1ba] = high
            data[0x1bb] = 0xff
        return bytes(data)

    def payload(self, sector, offset, data):
        with self.track.open('r+b') as file:
            file.seek(sector * RAW + 16 + offset)
            file.write(data)

    def directory(self, sector, entries):
        data = bytearray(DATA)
        at = 0
        for entry in entries:
            data[at:at + len(entry)] = entry
            at += len(entry)
        self.payload(sector, 0, data)

    def read_payload(self, sector, size):
        raw = self.track.read_bytes()
        return b''.join(raw[(sector + i) * RAW + 16:(sector + i) * RAW + 16 + DATA]
                        for i in range((size + DATA - 1) // DATA))[:size]

    def source_state(self):
        return {path.name: (len(path.read_bytes()), hashlib.sha256(path.read_bytes()).hexdigest(),
                            path.stat().st_mtime_ns)
                for path in self.source.iterdir() if path.is_file()}

    def extract(self):
        return bundle.extract(self.gdi, expected_ip_sha256=self.ip_hash)

    def reject(self):
        before = self.source_state()
        with self.assertRaises((ValueError, OSError, UnicodeError)):
            self.extract()
        self.assertEqual(self.source_state(), before)
        self.assertFalse(self.output.exists())

    def test_exact_header_bytes_metadata_and_verified_output(self):
        before = self.source_state()
        entries, report = self.extract()
        expected = {'headers/ONE/ONE.SGA.header.bin': self.first,
                    'headers/TWO/TWO.SGA.header.bin': self.second,
                    'disc.gdi': self.gdi.read_bytes()}
        self.assertEqual(entries, expected)
        self.assertEqual(report['ip_sha256'], self.ip_hash)
        self.assertEqual(report['session_lba'], SESSION)
        self.assertEqual(report['header_payload_bytes'], 1024)
        self.assertFalse(report['whole_ROM_payload_copied'])
        self.assertFalse(report['cdda_track_payload_copied'])
        rows = {row['path']: row for row in report['headers']}
        self.assertEqual(set(rows), set(self.roms))
        for path, header, sector, expected_bytes in (('ONE/ONE.SGA', self.first, 40, 8192),
                                                     ('TWO/TWO.SGA', self.second, 42, 0)):
            row = rows[path]
            self.assertEqual(row['lba'], SESSION + sector)
            self.assertEqual(row['rom_bytes'], DATA * 2)
            self.assertEqual(row['header_bytes'], 512)
            self.assertEqual(row['header_sha256'], hashlib.sha256(header).hexdigest())
            self.assertEqual(row['engine_sram_bytes'], expected_bytes)
            self.assertEqual(row['save_descriptor'], bundle.header_info(header))
        bundle.write_bundle(self.output, entries, report)
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(set(archive.namelist()), set(entries) | {'save-headers.json'})
            for name, data in expected.items():
                self.assertEqual(archive.read(name), data)
            self.assertEqual(json.loads(archive.read('save-headers.json')), report)
        self.assertEqual(self.source_state(), before)

    def test_no_full_rom_tail_or_cdda_payload_is_read(self):
        # All ROMs claim two sectors, but their second sectors deliberately do
        # not have raw Mode1 framing. Reading whole ROM files would fail.
        with self.track.open('r+b') as file:
            for sector in (41, 43):
                file.seek(sector * RAW)
                file.write(b'Invalid unselected ROM tail')
        for path in self.source.iterdir():
            path.chmod(0o444)
        before = self.source_state()
        original_open = Path.open

        def open_without_audio(path, *args, **kwargs):
            if path.name == 'track02.raw':
                raise AssertionError('Audio payload must not be opened')
            return original_open(path, *args, **kwargs)

        with patch.object(Path, 'open', open_without_audio):
            entries, _ = self.extract()
        self.assertEqual(entries['headers/ONE/ONE.SGA.header.bin'], self.first)
        self.assertEqual(entries['headers/TWO/TWO.SGA.header.bin'], self.second)
        self.assertEqual(self.source_state(), before)

    def test_missing_required_rom_is_rejected_without_partial_zip(self):
        self.directory(30, self.one[:2])
        self.reject()

    def test_duplicate_case_normalized_rom_is_rejected(self):
        self.directory(30, self.one + [record(b'ONE.SGA;1', SESSION + 40, DATA * 2)])
        self.reject()

    def test_multi_extent_and_short_selected_file_are_rejected(self):
        for change in ('multi', 'short'):
            with self.subTest(change=change):
                selected = record(b'one.sga;1', SESSION + 40,
                                  511 if change == 'short' else DATA * 2)
                if change == 'multi':
                    selected[25] = 0x80
                self.directory(30, self.one[:2] + [selected])
                self.reject()

    def test_wrong_ip_identity_is_rejected_without_source_mutation(self):
        self.payload(0, 128, b'Changed owner title')
        self.reject()

    def test_non_mode1_selected_header_is_rejected(self):
        with self.track.open('r+b') as file:
            file.seek(40 * RAW + 15)
            file.write(b'\x02')
        self.reject()

    def test_selected_header_outside_track_is_rejected(self):
        with self.track.open('r+b') as file:
            file.truncate(40 * RAW)
        self.reject()

    def test_selected_header_inside_track_but_rom_extent_beyond_end_is_rejected(self):
        # TWO's first sector and its complete 512-byte header remain readable,
        # while its declared second sector is outside all source tracks.
        with self.track.open('r+b') as file:
            file.truncate(43 * RAW)
        self.assertEqual(self.read_payload(42, 512), self.second)
        self.reject()

    def test_existing_output_is_never_overwritten(self):
        entries, report = self.extract()
        self.output.write_bytes(b'Existing output must survive')
        with self.assertRaises((ValueError, FileExistsError)):
            bundle.write_bundle(self.output, entries, report)
        self.assertEqual(self.output.read_bytes(), b'Existing output must survive')

    def test_publication_failure_leaves_no_partial_zip_or_temporary(self):
        entries, report = self.extract()
        before = set(self.folder.iterdir())
        with patch.object(bundle.os, 'link', side_effect=OSError('Publication denied')):
            with self.assertRaises(OSError):
                bundle.write_bundle(self.output, entries, report)
        self.assertEqual(set(self.folder.iterdir()), before)
        self.assertFalse(self.output.exists())

    def test_cli_wrong_owner_produces_no_output(self):
        before = self.source_state()
        process = subprocess.run([sys.executable, str(SCRIPT), str(self.gdi),
                                  '--output', str(self.output)],
                                 capture_output=True, text=True)
        self.assertNotEqual(process.returncode, 0)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.source_state(), before)

    def test_source_engine_descriptor_cases(self):
        for kind, expected in ((0xf0, 8192), (0xf8, 8192), (0xe0, 16384), (0x80, 0)):
            with self.subTest(kind=kind):
                self.assertEqual(bundle.engine_sram_bytes(self.header(b'ROM', kind, 0x3f)), expected)
        data = bytearray(self.header(b'ROM', 0xf8, 0x3f))
        for offset, value in ((0x1b0, 0), (0x1b1, 0), (0x1b3, 0)):
            changed = bytearray(data)
            changed[offset] = value
            self.assertEqual(bundle.engine_sram_bytes(changed), 0)


if __name__ == '__main__':
    unittest.main()
