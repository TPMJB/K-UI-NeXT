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

    def chr_asset(self, name=b'ZAC00.CHR;1', sector=50, size=128, words=(64,20,32,3,2)):
        self.assets.append(record(name,SESSION+sector,size));self.write_directory(30,self.assets)
        data=bytearray(max(size,20));struct.pack_into('<5I',data,0,*words)
        if size>=56:
            struct.pack_into('<3I',data,20,0x0c300004,0x0c300008,0x0c30000c)
            struct.pack_into('<6I',data,32,1,0,0x0d200010,2,0,0x0d200020)
        for at in range(0,size,DATA):self.payload(sector+at//DATA,0,data[at:at+DATA])
        return data

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

    def test_opt_in_chr_layouts_export_only_bounded_integer_metadata(self):
        self.chr_asset();before=self.hashes();r=probe.collect(self.gdi,include_resource_layouts=True)
        self.assertEqual(self.hashes(),before)
        self.assertEqual(r['schema'],'kui.doa2.filesystem-resource-layouts.v2')
        self.assertTrue(r['asset_metadata_requested']);self.assertTrue(r['asset_metadata_collected'])
        self.assertFalse(r['asset_payload_copied']);self.assertFalse(r['executable_payload_copied'])
        layouts=r['resource_layouts'];self.assertEqual((layouts['files'],layouts['available'],layouts['unavailable']),(1,1,0))
        self.assertEqual(layouts['selected_field_bytes'],56)
        entry=layouts['entries'][0]
        self.assertEqual(entry['status'],'observed')
        self.assertEqual(entry['header_words'],['00000040','00000014','00000020','00000003','00000002'])
        self.assertEqual(entry['pointer_table']['words'],['0c300004','0c300008','0c30000c'])
        self.assertEqual(entry['record_table']['words'],['00000001','00000000','0d200010','00000002','00000000','0d200020'])
        self.assertEqual(r['reads']['resource_header_bytes'],20)
        self.assertEqual(r['reads']['resource_table_bytes'],32+56)  # Prefixes are honestly included.
        self.assertEqual(r['reads']['resource_source_bytes'],3*RAW)
        self.assertEqual(r['reads']['asset_payload_bytes'],0)
        # The default mode must not begin reading assets because CHR files exist.
        normal=probe.collect(self.gdi)
        self.assertNotIn('resource_layouts',normal);self.assertNotIn('resource_header_bytes',normal['reads'])

    def test_chr_ranges_cross_logical_sectors_and_support_cooked_tracks(self):
        data=self.chr_asset(size=4096,words=(2088,2044,2060,3,2))
        struct.pack_into('<5I',data,0,2088,2044,2060,3,2)
        struct.pack_into('<3I',data,2044,0x0c300004,0x0c300008,0x0c30000c)
        struct.pack_into('<6I',data,2060,1,0,0x0d200010,2,0,0x0d200020)
        for at in range(0,len(data),DATA):self.payload(50+at//DATA,0,data[at:at+DATA])
        raw=probe.collect(self.gdi,include_resource_layouts=True)
        self.assertEqual(raw['resource_layouts']['available'],1)
        self.assertEqual(raw['reads']['resource_source_bytes'],4*RAW)
        self.cooked();cooked=probe.collect(self.gdi,include_resource_layouts=True)
        self.assertEqual(raw['resource_layouts'],cooked['resource_layouts'])
        self.assertEqual(cooked['reads']['resource_source_bytes'],4*DATA)

    def test_chr_unknown_counts_offsets_overlap_and_endianness_keep_header_without_table_reads(self):
        cases=[((64,20,32,4097,2),'count'),((64,21,32,3,2),'unaligned'),
               ((64,20,120,3,2),'leaves'),((64,20,32,4,2),'overlap'),
               ((0x40000000,0x14000000,0x20000000,0x03000000,0x02000000),'Unsupported')]
        self.chr_asset()
        for words,reason in cases:
            with self.subTest(words=words):
                self.payload(50,0,struct.pack('<5I',*words));before=self.hashes()
                r=probe.collect(self.gdi,include_resource_layouts=True)
                self.assertEqual(self.hashes(),before)
                row=r['resource_layouts']['entries'][0]
                self.assertEqual(row['status'],'unavailable');self.assertIn(reason,row['reason'])
                self.assertEqual(row['header_words'],[f'{w:08x}' for w in words])
                self.assertNotIn('resource_table_bytes',r['reads'])

    def test_chr_short_file_and_bad_mode1_framing_are_reported_unavailable(self):
        self.chr_asset(size=12)
        r=probe.collect(self.gdi,include_resource_layouts=True)
        self.assertIn('shorter',r['resource_layouts']['entries'][0]['reason'])
        self.assertFalse(r['asset_metadata_collected']);self.assertNotIn('resource_header_bytes',r['reads'])
        self.assets[-1]=record(b'ZAC00.CHR;1',SESSION+50,128);self.write_directory(30,self.assets)
        with self.track.open('r+b') as handle:handle.seek(50*RAW);handle.write(b'X')
        before=self.hashes();r=probe.collect(self.gdi,include_resource_layouts=True)
        self.assertEqual(self.hashes(),before)
        self.assertIn('Mode1',r['resource_layouts']['entries'][0]['reason'])
        self.assertEqual(r['reads']['resource_source_bytes'],RAW)
        self.assertFalse(r['asset_metadata_collected'])

    def test_chr_file_per_file_and_global_limits_never_follow_unsafe_ranges(self):
        self.chr_asset();before=self.hashes()
        for setting,limit,reason in [('MAX_RESOURCE_TABLE_BYTES',32,'per-file'),
                                     ('MAX_RESOURCE_METADATA_BYTES',19,'global'),
                                     ('MAX_RESOURCE_SOURCE_BYTES',RAW-1,'global')]:
            with self.subTest(setting=setting),patch.object(probe,setting,limit):
                r=probe.collect(self.gdi,include_resource_layouts=True)
                self.assertIn(reason,r['resource_layouts']['entries'][0]['reason'])
                self.assertNotIn('resource_table_bytes',r['reads'])
                self.assertEqual(self.hashes(),before)
        with patch.object(probe,'MAX_RESOURCE_METADATA_BYTES',70):
            r=probe.collect(self.gdi,include_resource_layouts=True)
            row=r['resource_layouts']['entries'][0]
            self.assertIn('global',row['reason']);self.assertIn('pointer_table',row)
            self.assertNotIn('record_table',row);self.assertEqual(r['reads']['resource_source_bytes'],2*RAW)
        self.chr_asset(name=b'TIN00.CHR;1',sector=52)
        with patch.object(probe,'MAX_RESOURCE_FILES',1):
            r=probe.collect(self.gdi,include_resource_layouts=True)
            self.assertEqual(r['resource_layouts']['status'],'unavailable')
            self.assertEqual(r['resource_layouts']['unavailable'],2)
            self.assertNotIn('resource_header_bytes',r['reads'])

    def test_chr_metadata_is_read_only_after_exact_boot_identity(self):
        self.chr_asset();self.payload(21,17,b'X')
        with patch.object(probe.Disc,'resource_range',side_effect=AssertionError('Asset read before identity')):
            self.refused('SHA256')

    def test_resource_layout_auto_selection_and_ambiguity_commands_preserve_flag(self):
        self.chr_asset();other=self.game/"copy 'special'.gdi";other.write_bytes(self.gdi.read_bytes())
        output=self.root/"layout 'report'.zip";before=self.hashes();error=io.StringIO()
        with patch.object(sys,'argv',['probe',str(self.game),'--resource-layouts','--output',str(output)]),contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(error):
            with self.assertRaises(SystemExit):probe.main()
        self.assertEqual(self.hashes(),before)
        commands=[shlex.split(line.strip()) for line in error.getvalue().splitlines() if line.startswith('  ')]
        self.assertEqual(len(commands),2)
        for command in commands:
            self.assertEqual(command[4:],['--output',str(output),'--resource-layouts'])
        other.unlink()
        with patch.object(sys,'argv',['probe',str(self.game),'--resource-layouts','--output',str(output)]),contextlib.redirect_stdout(io.StringIO()):probe.main()
        with zipfile.ZipFile(output) as archive:
            self.assertEqual(archive.namelist(),['startup-info.json'])
            report=json.loads(archive.read('startup-info.json'))
        self.assertEqual(report['resource_layouts']['available'],1)

    def test_resource_layout_default_output_is_new_and_does_not_overwrite_old_metadata(self):
        self.chr_asset();old=self.root/'DOA2-filesystem-metadata.zip';old.write_bytes(b'previous report')
        cwd=Path.cwd()
        try:
            os.chdir(self.root)
            with patch.object(sys,'argv',['probe','--gdi',str(self.gdi),'--resource-layouts']),contextlib.redirect_stdout(io.StringIO()):probe.main()
        finally:os.chdir(cwd)
        self.assertEqual(old.read_bytes(),b'previous report')
        self.assertTrue((self.root/'DOA2-resource-layout-metadata.zip').is_file())


if __name__=='__main__':unittest.main()
