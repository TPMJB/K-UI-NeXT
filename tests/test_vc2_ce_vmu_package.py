#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Mutate authored VC2 helper artifacts; no retail game inputs are needed."""
from pathlib import Path
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import check_vc2_ce_vmu_package as package
from check_loader_layout import EH,PH,SH,SYM

HELPER=Path(os.environ.get('KUI_VC2_HELPER_BUILD',ROOT/'build/vc2-ce-vmu-helper'))
LATE_BUILD=Path(os.environ.get('KUI_VC2_LATE_BUILD',ROOT/'build/vc2-ce-vmu-late'))


class PackageTests(unittest.TestCase):
    def setUp(self):
        if not (HELPER/'helper.elf').exists():self.skipTest('Build authored VC2 helper first')
        self.temporary=tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory=Path(self.temporary.name)/'helper'
        shutil.copytree(HELPER,self.directory,ignore=shutil.ignore_patterns('*.o','*.su','*.d','*.map'))
        # Persistent helpers include one fixed call into the independently
        # linked low raw reader. Its compiler graph belongs beside helper/.
        for path in HELPER.parent.glob('resident-sci.elf.ltrans*.ci'):
            shutil.copyfile(path,self.directory.parent/path.name)
        if not (self.directory/'bridge.bin').exists():
            image=package.read_elf(self.directory/'helper.elf')
            bridge=package.sections(image)['.vc2_maple_bridge']
            (self.directory/'bridge.bin').write_bytes(image[0][bridge[4]:bridge[4]+bridge[5]])

    def change(self,name,offset):
        path=self.directory/name;data=bytearray(path.read_bytes());data[offset]^=1;path.write_bytes(data)

    def test_final_helper_and_zero_tail(self):
        result=package.audit_helper(self.directory)
        self.assertEqual(result['prepared_bytes'],49152)
        self.assertLess(result['stack']['roots']['kui_armada_ce_maple_traffic_run']['c_bytes']+80,1536)

    def test_zero_tail_corruption_is_refused(self):
        self.change('prepared.bin',49151)
        with self.assertRaisesRegex(ValueError,'Prepared zero tail'):
            package.audit_helper(self.directory)

    def test_initialized_binary_corruption_is_refused(self):
        self.change('helper.bin',0)
        with self.assertRaisesRegex(ValueError,'Helper binary/ELF'):
            package.audit_helper(self.directory)

    def test_immutable_crc_binding_is_refused(self):
        path=self.directory/'vc2_ce_vmu_symbols.h'
        text=path.read_text()
        import re
        text,count=re.subn(r'(#define KUI_VC2_VMU_BLOB_CRC32 )0x[0-9a-f]+',
                           r'\g<1>0x00000000',text)
        self.assertEqual(count,1)
        path.write_text(text)
        with self.assertRaisesRegex(ValueError,'Immutable helper CRC'):
            package.audit_helper(self.directory)

    def test_bridge_source_corruption_is_refused(self):
        self.change('bridge.bin',84)
        with self.assertRaisesRegex(ValueError,'Mapped bridge layout'):
            package.audit_helper(self.directory)

    def test_bad_physical_load_range_is_refused(self):
        path=self.directory/'helper.elf';data=bytearray(path.read_bytes());header=EH.unpack_from(data)
        offset=header[5]
        # First PT_LOAD must start at physical8cfc4000; moving its physical
        # backing out of reservation must fail even though VMA stays intact.
        row=list(PH.unpack_from(data,offset));self.assertEqual(row[0],1)
        row[3]=0x8cf00000;PH.pack_into(data,offset,*row);path.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'physical range'):
            package.audit_helper(self.directory)

    def test_p1_mutable_workspace_is_refused(self):
        path=self.directory/'helper.elf';data=bytearray(path.read_bytes());header=EH.unpack_from(data)
        image=package.read_elf(path);sections=package.sections(image)
        target=sections['.workspace']
        for i,row in enumerate(image[3]):
            if row==target:
                changed=list(row);changed[3]-=0x20000000
                SH.pack_into(data,header[6]+i*SH.size,*changed);break
        else:self.fail('No workspace')
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'runtime segment|zeroed P2|zero-file load ranges'):
            package.audit_helper(self.directory)

    def test_output_cannot_replace_a_proof_input(self):
        output=self.directory/'helper.bin';before=output.read_bytes()
        result=subprocess.run([sys.executable,str(ROOT/'tools/check_vc2_ce_vmu_package.py'),
            str(self.directory),'--helper-only','--output',str(output)],
            capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('Report aliases a proof input',result.stderr)
        self.assertEqual(output.read_bytes(),before)

    def probe_symbols(self):
        symbols=package.read_elf(self.directory/'helper.elf')[4]
        if package.WRITE_PROBE_MARKER not in symbols:
            self.skipTest('Build authored VC2 write probe for these policy mutations')
        return symbols.copy()

    def test_probe_has_no_storage_or_durable_ack_service(self):
        symbols=self.probe_symbols()
        self.assertTrue(package.write_probe_profile(symbols))
        report=package.audit_helper(self.directory)['write_probe']
        self.assertFalse(report['durable_commit_ack_enabled'])
        self.assertFalse(report['storage_services_linked'])
        self.assertEqual(report['snapshot_words'],20)

    def test_probe_missing_marker_is_refused(self):
        symbols=self.probe_symbols();del symbols[package.WRITE_PROBE_MARKER]
        with self.assertRaisesRegex(ValueError,'exact absolute1'):
            package.write_probe_profile(symbols)

    def test_probe_nonabsolute_marker_is_refused(self):
        symbols=self.probe_symbols();values=list(symbols[package.WRITE_PROBE_MARKER])
        values[3]=1;symbols[package.WRITE_PROBE_MARKER]=tuple(values)
        with self.assertRaisesRegex(ValueError,'exact absolute1'):
            package.write_probe_profile(symbols)

    def test_probe_storage_service_is_refused(self):
        symbols=self.probe_symbols()
        symbols['_kui_armada_ce_vmu_sd_commit']=symbols[package.WRITE_PROBE_ENTRY]
        with self.assertRaisesRegex(ValueError,'links storage/runtime service'):
            package.write_probe_profile(symbols)

    def test_probe_runtime_continuation_is_refused(self):
        symbols=self.probe_symbols();symbols['_a2_continuation']=symbols[package.WRITE_PROBE_ENTRY]
        with self.assertRaisesRegex(ValueError,'links storage/runtime service'):
            package.write_probe_profile(symbols)

    def test_probe_missing_commit_callback_is_refused(self):
        symbols=self.probe_symbols();del symbols['_probe_commit_pending']
        with self.assertRaisesRegex(ValueError,'callback/snapshot is missing'):
            package.write_probe_profile(symbols)

    def test_probe_commit_callback_outside_helper_is_refused(self):
        symbols=self.probe_symbols();values=list(symbols['_probe_commit_pending'])
        values[0]=0x8c010000;symbols['_probe_commit_pending']=tuple(values)
        with self.assertRaisesRegex(ValueError,'leaves helper reservation'):
            package.write_probe_profile(symbols)


class LatePackageTests(unittest.TestCase):
    def setUp(self):
        names=('resident-sci.elf','stage.elf','stage.bin','vc2-raw.bin','vc2_late_symbols.h')
        if not all((LATE_BUILD/name).is_file() for name in names):
            self.skipTest('Build authored VC2 late loader first')
        self.temporary=tempfile.TemporaryDirectory();self.addCleanup(self.temporary.cleanup)
        self.directory=Path(self.temporary.name)
        for name in names:shutil.copyfile(LATE_BUILD/name,self.directory/name)
        import re
        header=(LATE_BUILD/'helper/vc2_ce_vmu_symbols.h').read_text()
        self.prepared_crc=re.search(r'KUI_VC2_VMU_PREPARED_CRC32 (0x[0-9a-f]+)',header)[1]

    def audit(self):
        persistent=package.PERSIST_MARKER in package.read_elf(LATE_BUILD/'helper/helper.elf')[4]
        return package.audit_late_resident(self.directory,
            package.read_elf(self.directory/'resident-sci.elf'),
            package.read_elf(self.directory/'stage.elf'),self.prepared_crc,persistent)

    def mutate_header(self,field,value,section=False):
        path=self.directory/'resident-sci.elf';data=bytearray(path.read_bytes())
        image=package.read_elf(path);header=image[1]
        if section:
            target=package.sections(image)['.vc2_late_raw'];index=image[3].index(target)
            row=list(target);row[field]=value
            SH.pack_into(data,header[6]+index*SH.size,*row)
        else:
            index,next_row=next((i,p) for i,p in enumerate(image[2]) if p[2]==package.RAW_P2)
            row=list(next_row);row[field]=value
            PH.pack_into(data,header[5]+index*PH.size,*row)
        path.write_bytes(data)

    def test_final_late_reader_is_separately_bound(self):
        checked,result=self.audit()
        self.assertLessEqual(checked['memory_end'],0x8c007ea0)
        self.assertEqual(result['raw_padded_bytes'],512)
        self.assertEqual(result['helper_file_bytes'],49152)

    def test_raw_physical_alias_corruption_is_refused(self):
        self.mutate_header(3,package.RAW_P1+4)
        with self.assertRaisesRegex(ValueError,'physical load range'):
            self.audit()

    def test_raw_writable_permission_is_refused(self):
        self.mutate_header(6,7)
        with self.assertRaisesRegex(ValueError,'permissions'):
            self.audit()

    def test_raw_section_alias_corruption_is_refused(self):
        self.mutate_header(3,package.RAW_P2+4,section=True)
        with self.assertRaisesRegex(ValueError,'bounded P2'):
            self.audit()

    def test_last_padded_byte_corruption_is_refused(self):
        path=self.directory/'vc2-raw.bin';data=bytearray(path.read_bytes());data[-1]^=1
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'padded file differs'):
            self.audit()

    def test_stage_raw_copy_corruption_is_refused(self):
        path=self.directory/'stage.elf';image=package.read_elf(path);data=bytearray(path.read_bytes())
        address=image[4]['__vc2_late_raw_start'][0]
        for segment in image[2]:
            if segment[0]==1 and segment[2]<=address<segment[2]+segment[4]:
                data[segment[1]+address-segment[2]]^=1;break
        else:self.fail('No stage raw segment')
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'Stage embeds another late raw reader'):
            self.audit()

    def test_seed_helper_crc_corruption_is_refused(self):
        path=self.directory/'stage.elf';image=package.read_elf(path);data=bytearray(path.read_bytes())
        address=image[4]['_kui_vc2_vmu_late_seed'][0]+20
        for segment in image[2]:
            if segment[0]==1 and segment[2]<=address<segment[2]+segment[4]:
                data[segment[1]+address-segment[2]]^=1;break
        else:self.fail('No stage seed segment')
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'Late stage seed binding'):
            self.audit()

    def test_old_display_cursor_alias_is_refused(self):
        path=self.directory/'resident-sci.elf';image=package.read_elf(path)
        data=bytearray(path.read_bytes());found=False
        for section in image[3]:
            if section[1]!=2:continue
            strings=image[3][section[6]]
            names=package.region(data,strings[4],strings[5],'symbol strings')
            for at in range(section[4],section[4]+section[5],SYM.size):
                values=list(SYM.unpack_from(data,at));offset=values[0]
                if names[offset:].split(b'\0',1)[0]!=b'_row':continue
                values[1]=image[4]['_manifest'][0]+248
                SYM.pack_into(data,at,*values);found=True
        self.assertTrue(found,'No display cursor symbol')
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'Late diagnostic cursor overlaps'):
            self.audit()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--helper',type=Path)
    parser.add_argument('--late-build',type=Path)
    args,remaining=parser.parse_known_args()
    if args.helper:HELPER=args.helper
    if args.late_build:LATE_BUILD=args.late_build
    unittest.main(argv=[sys.argv[0]]+remaining)
