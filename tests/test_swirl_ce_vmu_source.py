#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise exact-owner Swirl metadata and its independently decoded geometry.

Owner code stays external. No mutation of a source file grants runtime or
save-write authority; failed exact identities must stop before ROM parsing.
"""
import argparse
from pathlib import Path
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from armada_ce_rom_audit import ROMError
from swirl_ce_vmu_owner_audit import audit,SwirlSourceROM
from swirl_ce_vmu_geometry_audit import audit as geometry

class Source(unittest.TestCase):
    def test_original_and_geometry(self):
        report=audit(self.kernel);g=geometry(self.kernel)
        self.assertEqual(report['status'],'PASS')
        self.assertFalse(report['padding']['all_zero'])
        self.assertFalse(report['padding']['writable'])
        self.assertTrue(report['padding']['relocation_required'])
        self.assertEqual(g['geometry']['KData'],'0x8c1d1800')
        self.assertEqual(g['maple_static_geometry']['extension_bytes'],0x1a0)
        self.assertEqual(g['maple_RX_ownership']['function_bytes'],0x150)
        self.assertFalse(report['runtime_admitted'])
    def test_changed_owner_refused_before_metadata(self):
        data=self.kernel.read_bytes()
        offsets=[0,16,2047,2048,0x800+0xa6270,0x800+0xa6fb4,
                 0x800+0xa7c64,0x800+0x1bd61c,len(data)-1]
        with tempfile.TemporaryDirectory(prefix='swirl-source-mutations-') as tmp:
            p=Path(tmp)/'0WINCEOS.BIN'
            for offset in offsets:
                with self.subTest(offset=offset):
                    changed=bytearray(data);changed[offset]^=1;p.write_bytes(changed)
                    with self.assertRaisesRegex(ROMError,'kernel SHA256'):SwirlSourceROM(p)
            for changed in (data[:-1],data+b'\0'):
                p.write_bytes(changed)
                with self.assertRaisesRegex(ROMError,'kernel size'):SwirlSourceROM(p)
    def test_vc2_is_separate_owner(self):
        if not self.vc2:self.skipTest('VC2 owner not supplied')
        with self.assertRaisesRegex(ROMError,'kernel size'):SwirlSourceROM(self.vc2)

def main():
    p=argparse.ArgumentParser();p.add_argument('--kernel',type=Path,required=True);p.add_argument('--vc2',type=Path)
    a,args=p.parse_known_args();Source.kernel=a.kernel;Source.vc2=a.vc2
    unittest.main(argv=[sys.argv[0]]+args)
if __name__=='__main__':main()
