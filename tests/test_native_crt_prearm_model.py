#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Truth checks for the extra scalar forms in the compiled CRT audit."""
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from check_native_crt_prearm import PrearmCPU, expected_patch, GAME, MASK


class ScalarTruthTests(unittest.TestCase):
    def cpu(self):
        cpu=PrearmCPU.__new__(PrearmCPU)
        cpu.r=[0]*16;cpu.sr=0;cpu.executing=0
        cpu.memory=bytes(0x1000000);cpu.ram={};cpu.cache={};cpu.cache_on=True
        cpu.entry=GAME+0x100;cpu.pr=0;cpu.symbols={'_readonly_patch_entry':(0,)}
        cpu.writes=[];cpu.flushes=[];cpu.ccr_writes=[]
        cpu.readback_fault=cpu.cached_verify_fault=False
        return cpu

    def test_actual_alignment_veneer_literals(self):
        for address in (GAME+0x100,GAME+0x102,GAME+0x11e):
            target=0x8c004abc;patch=expected_patch(address,target)
            opcode,jump,slot=struct.unpack_from('<3H',patch)
            literal=((address+4)&~3)+4*(opcode&255)
            self.assertEqual((jump,slot),(0x402b,0x0009))
            self.assertEqual(struct.unpack_from('<I',patch,literal-address)[0],target)

    def test_byte_and_halfword_postincrement_sign_extend_and_alias(self):
        for size,opcode,values in ((1,0x6004,(0,0x7f,0x80,0xff)),(2,0x6005,(0,0x7fff,0x8000,0xffff))):
            for value in values:
                for same in (False,True):
                    cpu=self.cpu();cpu.put(GAME,value.to_bytes(size,'little'));cpu.r[2]=GAME
                    n=2 if same else 3;cpu.plain(opcode|(n<<8)|(2<<4),0)
                    expected=value if value<(1<<(8*size-1)) else value-(1<<(8*size))
                    self.assertEqual(cpu.r[n],expected&MASK)
                    if not same:self.assertEqual(cpu.r[2],GAME+size)

    def test_halfword_displacement_and_index_store(self):
        cpu=self.cpu();cpu.r[1]=GAME;cpu.r[0]=0x1234abcd
        cpu.plain(0x8112,0)
        self.assertEqual(cpu.read(GAME+4,2),0xabcd)
        cpu.r[0]=6;cpu.r[3]=GAME;cpu.r[2]=0xffff9876;cpu.plain(0x0325,0)
        self.assertEqual(cpu.read(GAME+6,2),0x9876)

    def test_dt_shlr16_negc_wrap_and_t(self):
        for value in (0,1,2,0x7fffffff,0xffffffff):
            for carry in (0,1):
                cpu=self.cpu();cpu.r[2]=value;cpu.sr=0x600000f0|carry;cpu.plain(0x4210,0)
                self.assertEqual(cpu.r[2],(value-1)&MASK);self.assertEqual(cpu.sr&1,int(value==1))
                cpu.r[2]=value;cpu.plain(0x4229,0);self.assertEqual(cpu.r[2],value>>16)
                cpu.r[2]=value;cpu.sr=0x600000f0|carry;cpu.plain(0x632a,0)
                self.assertEqual(cpu.r[3],(-value-carry)&MASK)
                self.assertEqual(cpu.sr&1,int(value+carry!=0));self.assertEqual(cpu.sr&~1,0x600000f0)


if __name__=='__main__':unittest.main()
