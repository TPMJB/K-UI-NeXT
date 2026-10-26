#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic raw/cooked discs; no retail assets are embedded in the tests."""
import contextlib
import hashlib
import importlib.util
import io
import json
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
SPEC = importlib.util.spec_from_file_location('rayman_resource_probe', ROOT/'tools/kui-rayman-resource-probe.py')
probe = importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(probe)


class ProbeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory();self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name);self.game = self.root/'game';self.gdi = self.game/'disc.gdi'
        make_fixture(self.game, title=probe.EXPECTED_TITLE.encode())
        self.track = self.game/'track03.bin'
        self.boot = bytes((i*17+3)&255 for i in range(DATA))+bytes((i*29+7)&255 for i in range(DATA))
        raw = self.track.read_bytes()
        ip = b''.join(raw[i*RAW+16:i*RAW+16+DATA] for i in range(16))
        self.addCleanup(patch.stopall)
        patch.object(probe, 'EXPECTED_IP_SHA256', hashlib.sha256(ip).hexdigest()).start()
        patch.object(probe, 'EXPECTED_BYTES', len(self.boot)).start()
        patch.object(probe, 'EXPECTED_SHA256', hashlib.sha256(self.boot).hexdigest()).start()
        self.root_entries = [record(b'\0',SESSION+20,DATA,True),record(b'\1',SESSION+20,DATA,True),
            record(b'1ST_READ.BIN;1',SESSION+21,len(self.boot)),record(b'DCDATA',SESSION+30,DATA,True)]
        self.assets = [record(b'\0',SESSION+30,DATA,True),record(b'\1',SESSION+20,DATA,True),
            record(b'FIX.DAT;1',SESSION+40,8192),record(b'UBISOFT.IMG;1',SESSION+50,4096)]
        self.directory(20,self.root_entries);self.directory(30,self.assets)
        words = list(range(64));words[0xa0//4]=7;words[0xa4//4]=0x8cce3b00;words[0xc0//4]=0x8cc00000
        self.header = struct.pack('<64I', *words)
        self.payload(40,0,self.header)
        # If an implementation accidentally follows an absolute pointer as an
        # ISO range, this lies far beyond our tiny validated source track.
        self.original_hashes = self.hashes()

    def payload(self, sector, offset, data):
        with self.track.open('r+b') as source:
            source.seek(sector*RAW+16+offset);source.write(data)

    def directory(self, sector, entries):
        data=bytearray(DATA);at=0
        for entry in entries:data[at:at+len(entry)]=entry;at+=len(entry)
        self.payload(sector,0,data)

    def hashes(self):
        return {str(p.relative_to(self.root)):hashlib.sha256(p.read_bytes()).hexdigest()
                for p in self.root.rglob('*') if p.is_file() and not p.is_symlink()}

    def refused(self, text):
        before=self.hashes()
        with self.assertRaisesRegex((ValueError,OSError),text):probe.collect(self.gdi)
        self.assertEqual(self.hashes(),before)

    def test_exact_prefix_metadata_and_source_unchanged(self):
        report=probe.collect(self.gdi)
        self.assertEqual(self.hashes(),self.original_hashes)
        self.assertEqual(report['schema'],'kui.rayman.fix-header-metadata.v1')
        self.assertEqual(report['fix_dat']['file_bytes'],8192)
        self.assertEqual(report['fix_dat']['lba'],SESSION+40)
        self.assertEqual(report['fix_dat']['prefix_sha256'],hashlib.sha256(self.header).hexdigest())
        self.assertEqual(len(report['fix_dat']['words_le']),64)
        self.assertEqual(report['fix_dat']['selected_fields']['pair_table_pointer']['value'],'8cce3b00')
        self.assertEqual(report['fix_dat']['selected_fields']['pair_count']['unsigned'],7)
        self.assertIsNone(report['fix_dat']['live_allocation_base'])
        self.assertFalse(report['fix_dat']['pointer_targets_followed'])
        self.assertEqual(report['fix_dat']['table_ranges_read'],[])
        self.assertEqual(report['reads']['resource_header_bytes'],256)
        self.assertEqual(report['reads']['resource_source_bytes'],RAW)
        self.assertEqual(report['reads'].get('resource_table_bytes',0),0)
        self.assertFalse(report['executable_payload_copied'])
        self.assertFalse(report['asset_payload_copied'])

    def test_cooked_sectors(self):
        for filename in ('track01.bin','track03.bin'):
            path=self.game/filename;raw=path.read_bytes()
            path.write_bytes(b''.join(raw[i+16:i+16+DATA] for i in range(0,len(raw),RAW)))
        self.gdi.write_text(self.gdi.read_text().replace('4 2352','4 2048'))
        before=self.hashes();report=probe.collect(self.gdi)
        self.assertEqual(self.hashes(),before)
        self.assertEqual(report['reads']['resource_source_bytes'],DATA)
        self.assertEqual(report['fix_dat']['prefix_sha256'],hashlib.sha256(self.header).hexdigest())

    def test_identity_and_startup_fail_before_asset_read(self):
        cases=[('EXPECTED_IP_SHA256','0'*64,'IP SHA256'),
               ('EXPECTED_BYTES',len(self.boot)+1,'byte length'),
               ('EXPECTED_SHA256','0'*64,'startup SHA256')]
        for name,value,message in cases:
            reads=[];original=probe.Disc.read
            def observe(disc,lba,size,purpose='metadata'):
                reads.append(purpose);return original(disc,lba,size,purpose)
            with patch.object(probe,name,value),patch.object(probe.Disc,'read',observe):self.refused(message)
            self.assertNotIn('resource_header',reads)

    def test_only_one_prefix_no_tables_or_pointer_targets(self):
        _,disc,entry=probe.inspect(self.gdi)
        with self.assertRaisesRegex(ValueError,'prohibited'):disc.read(entry['lba'],4,'resource_table')
        with self.assertRaisesRegex(ValueError,'Only one'):disc.read(entry['lba'],255,'resource_header')
        self.assertEqual(disc.header_reads,0)
        disc.resource_range(entry,0,256,'resource_header')
        with self.assertRaisesRegex(ValueError,'Only one'):disc.resource_range(entry,0,256,'resource_header')
        self.assertEqual(disc.header_reads,1)

    def test_missing_short_duplicate_and_unsafe_resource(self):
        self.assets[2]=record(b'OTHER.DAT;1',SESSION+40,8192);self.directory(30,self.assets)
        self.refused('Unique DCDATA/FIX.DAT')
        self.assets[2]=record(b'FIX.DAT;1',SESSION+40,255);self.directory(30,self.assets)
        self.refused('256-byte prefix')
        self.assets[2]=record(b'FIX.DAT;1',SESSION+40,8192)
        self.assets.append(record(b'fix.dat;1',SESSION+40,8192));self.directory(30,self.assets)
        self.refused('duplicate')
        self.assets.pop();self.assets[2]=record(b'FIX.DAT;1',SESSION+63,8192);self.directory(30,self.assets)
        self.refused('data tracks')

    def test_cycle_endian_flags_and_raw_framing(self):
        original=self.track.read_bytes()
        self.assets.append(record(b'LOOP',SESSION+20,DATA,True));self.directory(30,self.assets)
        self.refused('cycle or aliased')
        self.assets.pop();self.assets[2][6]^=1;self.directory(30,self.assets)
        self.refused('byte orders')
        self.assets[2][6]^=1;self.assets[2][25]=0x80;self.directory(30,self.assets)
        self.refused('multiple extents')
        self.track.write_bytes(original)
        with self.track.open('r+b') as source:source.seek(40*RAW);source.write(b'X')
        self.refused('Mode1')

    def test_read_budgets_fail_closed(self):
        for name,value in [('MAX_METADATA_BYTES',32767),('MAX_SOURCE_BYTES',RAW*15),
                           ('MAX_SCAN_SOURCE_BYTES',RAW*15)]:
            with patch.object(probe,name,value):self.refused('limit|budget')
        # Exhaustion during an automatic scan must not be converted into an
        # ignored unsupported-disc error followed by partial collection.
        with patch.object(probe,'MAX_SCAN_SOURCE_BYTES',RAW*15):
            with self.assertRaises(probe.ReadLimitError):probe.select(self.root)

    def test_gdi_path_errors_and_symlinks(self):
        with self.assertRaisesRegex(ValueError,'not a directory'):probe.collect(self.game)
        link=self.root/'linked.gdi';link.symlink_to(self.gdi)
        with self.assertRaisesRegex(ValueError,'symlink'):probe.collect(link)
        track=self.game/'track03.bin';saved=track.read_bytes();track.unlink()
        other=self.root/'source.bin';other.write_bytes(saved);track.symlink_to(other)
        with self.assertRaisesRegex(ValueError,'symlink'):probe.collect(self.gdi)

    def test_duplicates_print_quoted_commands_without_asset_reads(self):
        copies=[self.gdi,self.game/"copy 'special'.gdi",self.game/'third copy.gdi']
        for path in copies[1:]:path.write_bytes(self.gdi.read_bytes())
        output=self.root/"Rayman report 'final'.zip";error=io.StringIO();seen=[]
        original=probe.Disc.read
        def observe(disc,lba,size,purpose='metadata'):
            seen.append(purpose);return original(disc,lba,size,purpose)
        with patch.object(probe.Disc,'read',observe),patch.object(sys,'argv',['probe',str(self.game),'--output',str(output)]),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(error):
            with self.assertRaises(SystemExit) as stopped:probe.main()
        self.assertEqual(stopped.exception.code,1);self.assertNotIn('resource_header',seen)
        commands=[shlex.split(line.strip()) for line in error.getvalue().splitlines() if line.startswith('  ')]
        self.assertEqual(len(commands),3)
        self.assertEqual({c[3] for c in commands},{str(path) for path in copies})
        for command in commands:
            self.assertEqual(command[:3],['python3',str(Path(probe.__file__).resolve()),'--gdi'])
            self.assertEqual(command[4:],['--output',str(output)])

    def test_cli_no_argument_gdi_explains_actual_file(self):
        error=io.StringIO()
        with patch.object(sys,'argv',['probe',str(self.game),'--gdi']),contextlib.redirect_stderr(error):
            with self.assertRaises(SystemExit) as stopped:probe.main()
        self.assertEqual(stopped.exception.code,2)
        self.assertIn('full path to a .gdi file',error.getvalue())
        self.assertIn('omit --gdi entirely',error.getvalue())

    def test_output_zip_json_only_nonoverwrite_and_outside_source(self):
        output=self.root/'Rayman2-FIX-layout-metadata.zip'
        args=['probe','--gdi',str(self.gdi),'--output',str(output)]
        with patch.object(sys,'argv',args),contextlib.redirect_stdout(io.StringIO()):probe.main()
        with zipfile.ZipFile(output) as archive:
            self.assertEqual(archive.namelist(),['startup-info.json'])
            report=json.loads(archive.read('startup-info.json'))
        self.assertEqual(report['fix_dat']['prefix_bytes'],256)
        before=output.read_bytes()
        with patch.object(sys,'argv',args),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):probe.main()
        self.assertEqual(output.read_bytes(),before)
        args[-1]=str(self.game/'forbidden.zip')
        with patch.object(sys,'argv',args),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):probe.main()
        self.assertFalse((self.game/'forbidden.zip').exists())
        self.assertEqual({k:v for k,v in self.hashes().items() if k.startswith('game/')},
                         {k:v for k,v in self.original_hashes.items() if k.startswith('game/')})


if __name__=='__main__':unittest.main()
