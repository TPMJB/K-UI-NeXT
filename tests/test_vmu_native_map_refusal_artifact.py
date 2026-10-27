#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Adversarial checks of the real relaxed GD-map terminal instructions."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
import check_vmu_native_map_refusal as audit

class MapRefusalArtifact(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path=Path(os.environ.get('KUI_NATIVE_ARTIFACT_DIR',ROOT/'build/evolution-lease-map-dev'))/'resident-sci.elf'
        if not cls.path.exists():raise unittest.SkipTest('build the native persistent resident first')
        cls.image=audit.read_elf(cls.path)
        cls.disassembly=subprocess.check_output([str(ROOT/'.deps/sh-elf/bin/sh-elf-objdump'),'-d',str(cls.path)],text=True)

    def test_complete_refusal_terminal_route_and_mutations(self):
        result=audit.check(self.path,self_test=True)
        self.assertEqual(result['mutation_rejections'],316)
        self.assertEqual(result['resident_memory_end'],'0x8c007800')
        self.assertEqual(result['diagnostic_detail'],'00000100')

    def test_coherent_wrong_typed_control_pointer_changes_signature(self):
        name='_observe_map';signature=audit.signature(self.image,self.disassembly,name)
        offset=next(r[0] for r in signature[2] if r[1]=='_kui_vmu_observe_boot_control')
        address,body,section=audit.symbol_bytes(self.image,name,2)
        changed=bytearray(self.image[0]);target=self.image[4]['_kui_vmu_native_heap_original'][0]+0x20000000
        struct.pack_into('<I',changed,section[4]+address-section[3]+offset,target)
        self.assertNotEqual(audit.signature((bytes(changed),)+self.image[1:],self.disassembly,name)[:2],signature[:2])

    def test_refusal_cannot_enter_another_real_typed_function(self):
        name='_observe_map';signature=audit.signature(self.image,self.disassembly,name)
        offset=next(r[0] for r in signature[2] if r[1]=='_kui_vmu_observe_low_failure')
        address,body,section=audit.symbol_bytes(self.image,name,2)
        target=self.image[4]['_map_guest'][0];pc=address+offset
        delta=(target-pc-4)//2;self.assertTrue(-2048<=delta<2048)
        changed=bytearray(self.image[0]);struct.pack_into('<H',changed,section[4]+address-section[3]+offset,0xb000|(delta&4095))
        self.assertNotEqual(audit.signature((bytes(changed),)+self.image[1:],self.disassembly,name)[:2],signature[:2])

if __name__=='__main__':unittest.main()
