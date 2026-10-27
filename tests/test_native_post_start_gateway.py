#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Probe SH immediate semantics independently of the compiled mask fixtures."""
from pathlib import Path
import struct
import sys
import unittest

sys.path[:0]=[str(Path(__file__).resolve().parents[1]/'tools'),
              str(Path(__file__).resolve().parents[1]/'tests')]
from check_native_maple_delay import PostStartGateway


class PostStartDecoderTests(unittest.TestCase):
    def cpu(self,value):
        cpu=object.__new__(PostStartGateway)
        cpu.r=[0]*16;cpu.r[0]=value;cpu.sr=0;cpu.executed=[]
        return cpu

    def test_cmp_immediate_sign_extends(self):
        for opcode,value,expected in ((0x88f0,0xf0,False),(0x88f0,0xfffffff0,True),
                                      (0x8860,0x60,True)):
            with self.subTest(opcode=opcode,value=value):
                cpu=self.cpu(value);cpu.plain(opcode,0)
                self.assertEqual(bool(cpu.sr&1),expected)

    def test_xor_and_tst_zero_extend_immediates(self):
        cpu=self.cpu(0xf0);cpu.plain(0xcaf0,0);cpu.plain(0xc8ff,2)
        self.assertEqual(cpu.r[0],0);self.assertTrue(cpu.sr&1)
        cpu=self.cpu(0xfffffff0);cpu.plain(0xcaf0,0)
        self.assertEqual(cpu.r[0],0xffffff00)
        cpu.plain(0x2008,2);self.assertFalse(cpu.sr&1)
        cpu=self.cpu(0);cpu.plain(0x2008,0);self.assertTrue(cpu.sr&1)

    def test_bt_uses_actual_comparison_result(self):
        cpu=self.cpu(0x60);cpu.pc=0;cpu.resume=8;cpu.refusal=100
        code=struct.pack('<4H',0x8860,0x8901,9,9)
        cpu.code=lambda address,count:code[address:address+count]
        self.assertIs(cpu.run(),cpu);self.assertEqual(cpu.pc,8)


if __name__=='__main__':unittest.main()
