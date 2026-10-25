#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only metadata collector tests; synthetic sectors contain no retail code."""
import hashlib
import contextlib
import io
import importlib.util
import json
import os
from pathlib import Path
import shlex
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from games_fixture import DATA, RAW, SESSION, make_fixture, record
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('doa2_metadata_probe', ROOT/'tools/kui-doa2-filesystem-probe.py')
probe = importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(probe)


class ProbeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory();self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name);self.game = self.root/'game';self.gdi = self.game/'disc.gdi'
        make_fixture(self.game, title=b'DEAD OR ALIVE 2')
        self.track = self.game/'track03.bin'
        self.boot = bytes((i*17+3)&255 for i in range(DATA))+bytes((i*29+7)&255 for i in range(DATA))
        self.addCleanup(patch.stopall)
        patch.object(probe, 'EXPECTED_BYTES', len(self.boot)).start()
        patch.object(probe, 'EXPECTED_SHA256', hashlib.sha256(self.boot).hexdigest()).start()
        self.root_entries = [record(b'\0',SESSION+20,DATA,True),record(b'\1',SESSION+20,DATA,True),
            record(b'1ST_READ.BIN;1',SESSION+21,len(self.boot)),record(b'DOA2',SESSION+30,DATA,True)]
        self.write_directory(20,self.root_entries)
        self.assets = [record(b'\0',SESSION+30,DATA,True),record(b'\1',SESSION+20,DATA,True),
            record(b'AICADRV.BIN;1',SESSION+40,3001),record(b'FIGHT_SE.BIN;1',SESSION+42,100),
            record(b'EMPTY.BIN;1',0,0)]
        self.write_directory(30,self.assets)

    def payload(self, sector, offset, data):
        with self.track.open('r+b') as handle:
            handle.seek(sector*RAW+16+offset);handle.write(data)

    def write_directory(self, sector, entries):
        payload=bytearray(DATA);at=0
        for entry in entries:payload[at:at+len(entry)]=entry;at+=len(entry)
        self.payload(sector,0,payload)

    def hashes(self):
        return {str(p.relative_to(self.root)):hashlib.sha256(p.read_bytes()).hexdigest()
                for p in self.root.rglob('*') if p.is_file() and not p.is_symlink()}

    def refused(self, text='.'):
        before=self.hashes()
        with self.assertRaisesRegex((ValueError,OSError),text):probe.collect(self.gdi)
        self.assertEqual(self.hashes(),before)

    def cooked(self):
        for filename in ('track01.bin','track03.bin'):
            path=self.game/filename;raw=path.read_bytes()
            path.write_bytes(b''.join(raw[i+16:i+16+DATA] for i in range(0,len(raw),RAW)))
        self.gdi.write_text(self.gdi.read_text().replace('4 2352','4 2048'))

    def test_complete_metadata_no_asset_reads_or_payload(self):
        before=self.hashes();r=probe.collect(self.gdi)
        self.assertEqual(self.hashes(),before)
        self.assertEqual([(e['path'],e['lba'],e['bytes'],e['directory']) for e in r['entries']],
            [('1ST_READ.BIN',SESSION+21,len(self.boot),False),('DOA2',SESSION+30,DATA,True),
             ('DOA2/AICADRV.BIN',SESSION+40,3001,False),('DOA2/EMPTY.BIN',0,0,False),
             ('DOA2/FIGHT_SE.BIN',SESSION+42,100,False)])
        self.assertEqual(r['reads'],dict(metadata_bytes=32768+DATA*3,boot_bytes=len(self.boot),asset_payload_bytes=0))
        self.assertEqual(r['resource_header_fields'],[])
        self.assertEqual([(d['path'],d['lba'],d['bytes']) for d in r['iso_metadata']['directories']],
                         [('/',SESSION+20,DATA),('DOA2',SESSION+30,DATA)])
        self.assertFalse(r['asset_payload_copied']);self.assertFalse(r['executable_payload_copied'])
        # Unread asset sectors need not have Mode1 framing: metadata collection
        # cannot silently become a payload reader or a full-disc integrity claim.
        with self.track.open('r+b') as handle:handle.seek(40*RAW);handle.write(bytes(RAW))
        self.assertEqual(probe.collect(self.gdi)['entries'],r['entries'])

    def test_cooked_and_raw_have_identical_logical_metadata(self):
        raw=probe.collect(self.gdi);self.cooked();cooked=probe.collect(self.gdi)
        for key in ('entries','iso_metadata','boot_sha256','boot_bytes','ip_sha256','reads','session_lba'):
            self.assertEqual(raw[key],cooked[key])
        self.assertNotEqual(raw['gdi_sha256'],cooked['gdi_sha256'])

    def test_boot_identity_is_exact_and_not_only_a_title(self):
        self.payload(21,17,b'X');self.refused('SHA256')

    def test_boot_length_is_pinned(self):
        self.root_entries[2]=record(b'1ST_READ.BIN;1',SESSION+21,len(self.boot)-1)
        self.write_directory(20,self.root_entries);self.refused('byte length')

    def test_native_title_and_peripheral_flags(self):
        self.payload(0,128,b'OTHER TITLE'.ljust(128,b' '));self.refused('Different IP title')
        self.payload(0,128,b'DEAD OR ALIVE 2'.ljust(128,b' '));self.payload(0,56,b'0000001 ')
        self.refused('Windows CE')

    def test_every_file_extent_is_checked_without_payload(self):
        for lba,size in ((SESSION+63,DATA+1),(SESSION-1,1),(719850,0xffffffff)):
            self.assets[2]=record(b'AICADRV.BIN;1',lba,size);self.write_directory(30,self.assets)
            self.refused('extent')

    def test_audio_and_gaps_cannot_supply_iso_extents(self):
        self.assets[2]=record(b'AICADRV.BIN;1',SESSION+62,DATA*4);self.write_directory(30,self.assets)
        self.refused('data tracks')

    def test_duplicate_paths_and_boot_names_refused(self):
        self.assets.append(record(b'aicadrv.bin;1',SESSION+44,1));self.write_directory(30,self.assets)
        self.refused('duplicate')
        self.assets[-1]=record(b'1ST_READ.BIN;1',SESSION+21,len(self.boot));self.write_directory(30,self.assets)
        self.refused('boot path')

    def test_alias_cycle_and_multiextent_refused(self):
        self.assets.append(record(b'LOOP',SESSION+20,DATA,True));self.write_directory(30,self.assets)
        self.refused('cycle or aliased')
        self.assets.pop();self.assets[2][25]=0x80;self.write_directory(30,self.assets)
        self.refused('multiple extents')

    def test_endian_mismatch_and_crossing_sector_refused(self):
        self.assets[2][6]^=1;self.write_directory(30,self.assets);self.refused('byte orders')
        entries=[record((str(i)+'A'*220).encode(),0,0) for i in range(7)]
        entries.append(record(b'LAST'+b'B'*203,0,0))
        self.write_directory(30,entries);self.payload(30,2018,b'\x28');self.refused('crosses')

    def test_bounded_traversal_and_paths(self):
        with patch.object(probe.iso,'DIRECTORY_COUNT_LIMIT',1):self.refused('directory count')
        with patch.object(probe,'MAX_ISO_PATH',6):self.refused('Unsafe')
        with patch.object(probe.iso,'SCAN_ENTRY_LIMIT',2):self.refused('entry count')

    def test_raw_framing_and_track_geometry(self):
        with self.track.open('r+b') as handle:handle.seek(20*RAW);handle.write(b'X')
        self.refused('Mode1')
        self.gdi.write_text(self.gdi.read_text().replace('track03.bin 0','track03.bin 1'))
        self.refused('file offset')

    def test_symlinks_and_truncated_tracks_refused(self):
        link=self.root/'linked.gdi';link.symlink_to(self.gdi)
        with self.assertRaisesRegex(ValueError,'symlink'):probe.collect(link)
        self.track.write_bytes(self.track.read_bytes()[:-1]);self.refused('whole number')

    def test_auto_select_exact_identity_and_duplicate_refusal(self):
        other=self.root/'other';make_fixture(other,title=b'OTHER NATIVE')
        wrong=self.root/'different-doa2';make_fixture(wrong,title=b'DEAD OR ALIVE 2')
        with (wrong/'track03.bin').open('r+b') as handle:
            handle.seek(21*RAW+16);handle.write(b'X')
        selected,r=probe.select(self.root);self.assertEqual(selected,self.gdi)
        self.assertEqual(r['boot_sha256'],hashlib.sha256(self.boot).hexdigest())
        duplicate=self.game/'copy.gdi';duplicate.write_bytes(self.gdi.read_bytes())
        with self.assertRaisesRegex(ValueError,'Multiple exact'):probe.select(self.root)
        duplicate.unlink()
        with patch.object(probe,'MAX_GDIS',1):
            with self.assertRaisesRegex(ValueError,'bounded'):probe.select(self.root)

    def test_multiple_exact_cli_lists_runnable_quoted_commands_and_keeps_output(self):
        copies = [self.gdi, self.game/"copy 'special'.gdi", self.game/'third copy.gdi']
        for path in copies[1:]:path.write_bytes(self.gdi.read_bytes())
        output=self.root/"report 'final'.zip";before=self.hashes();error=io.StringIO()
        with patch.object(sys,'argv',['probe',str(self.game),'--output',str(output)]),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(error):
            with self.assertRaises(SystemExit) as stopped:probe.main()
        self.assertEqual(stopped.exception.code,1);self.assertEqual(self.hashes(),before)
        commands=[shlex.split(line.strip()) for line in error.getvalue().splitlines() if line.startswith('  ')]
        self.assertEqual(len(commands),len(copies))
        self.assertEqual({command[3] for command in commands},{str(path) for path in copies})
        for command in commands:
            self.assertEqual(command[:3],['python3',str(Path(probe.__file__).resolve()),'--gdi'])
            self.assertEqual(command[4:],['--output',str(output)])
        with patch.object(sys,'argv',commands[0][1:]),contextlib.redirect_stdout(io.StringIO()):probe.main()
        with zipfile.ZipFile(output) as archive:self.assertEqual(archive.namelist(),['startup-info.json'])

    def test_explicit_gdi_directory_explains_file_argument_separately_from_symlink(self):
        output=self.root/'directory-error.zip';before=self.hashes();error=io.StringIO()
        with patch.object(sys,'argv',['probe','--gdi',str(self.game),'--output',str(output)]),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(error):
            with self.assertRaises(SystemExit) as stopped:probe.main()
        self.assertEqual(stopped.exception.code,1);self.assertEqual(self.hashes(),before)
        self.assertIn('--gdi requires a regular .gdi file, not a directory',error.getvalue())
        self.assertIn('SD root without --gdi',error.getvalue());self.assertNotIn('symlink',error.getvalue())

    def test_cli_publishes_metadata_only_outside_source_and_refuses_inside(self):
        output=self.root/'cli.zip'
        with patch.object(sys,'argv',['probe','--gdi',str(self.gdi),'--output',str(output)]),contextlib.redirect_stdout(io.StringIO()):
            probe.main()
        with zipfile.ZipFile(output) as archive:self.assertEqual(archive.namelist(),['startup-info.json'])
        inside=self.game/'never.zip'
        with patch.object(sys,'argv',['probe',str(self.game),'--output',str(inside)]),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as stopped:probe.main()
        self.assertEqual(stopped.exception.code,1);self.assertFalse(inside.exists())

    def test_output_is_one_metadata_json_and_never_overwrites(self):
        destination=self.root/'metadata.zip';report=probe.collect(self.gdi)
        before=self.hashes();probe.iso.write_bundle(destination,{},report)
        with zipfile.ZipFile(destination) as archive:
            self.assertEqual(archive.namelist(),['startup-info.json'])
            self.assertEqual(json.loads(archive.read('startup-info.json')),report)
        self.assertEqual({k:v for k,v in self.hashes().items() if k!='metadata.zip'},before)
        saved=destination.read_bytes()
        with self.assertRaisesRegex(ValueError,'already exists'):probe.iso.write_bundle(destination,{},report)
        self.assertEqual(destination.read_bytes(),saved)


if __name__=='__main__':unittest.main()
