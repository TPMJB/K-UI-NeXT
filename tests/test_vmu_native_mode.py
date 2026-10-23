#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Canonical bounded runtime selector; corrupted/unknown modes never install."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import vmu_native_mode as mode

class ModeCodecTests(unittest.TestCase):
    def test_canonical_wire_and_modes(self):
        canonical=b'KUINVM01'+struct.pack('<6I',1,32,0,0,0,0)
        self.assertEqual(mode.template_mode(),canonical)
        for choice in (mode.NORMAL,mode.CONTROL,mode.PROFILE,mode.LEASE_ONLY):
            wire=mode.fill_mode(canonical,choice)
            self.assertEqual(mode.decode_mode(wire),choice)
            self.assertEqual(wire[:16],canonical[:16]);self.assertEqual(wire[20:],bytes(12))
            self.assertEqual(mode.fill_mode(wire,0),canonical)
            self.assertEqual(mode.find_mode(b'prefix'+wire+b'tail'),6)
            self.assertEqual(mode.find_mode(b'KUINVM01'+bytes(24)+wire),32)
        self.assertIsNone(mode.find_mode(b'no selector'))

    def test_every_bit_header_reserved_and_unknown_modes(self):
        canonical=mode.template_mode()
        for index in range(32):
            for bit in range(8):
                wire=bytearray(canonical);wire[index]^=1<<bit
                with self.subTest(index=index,bit=bit):
                    if index==16 and bit<2:self.assertEqual(mode.decode_mode(wire),1<<bit)
                    else:
                        with self.assertRaises(ValueError):mode.decode_mode(wire)
        for length in (0,1,8,16,31,33,64):
            with self.subTest(length=length),self.assertRaises(ValueError):mode.decode_mode((canonical*2)[:length])
        for choice in (-1,4,0xffffffff,None,True,False,'1',1.0):
            with self.subTest(choice=choice),self.assertRaises(ValueError):mode.fill_mode(canonical,choice)

    def test_duplicate_valid_selectors_refused(self):
        for first in range(4):
            for second in range(4):
                with self.subTest(first=first,second=second),self.assertRaisesRegex(ValueError,'duplicated'):
                    mode.find_mode(mode.fill_mode(mode.template_mode(),first)+mode.fill_mode(mode.template_mode(),second))

if __name__=='__main__':unittest.main()
