#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the cache distinction on which the linked publication proof relies."""
from pathlib import Path
import copy
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from check_native_metadata_cache import OperandLines


class OperandLineTests(unittest.TestCase):
    def setUp(self):
        self.ram=bytearray(bytes(range(128)))
        self.cache=OperandLines(lambda a,n:bytes(self.ram[a:a+n]),
                               lambda a,b:self.ram.__setitem__(slice(a,a+len(b)),b))

    def test_dirty_ocbi_discards_without_writeback(self):
        self.cache.seed(32,bytes([0xa5])*32,True)
        self.cache.ocbi(0x8000003f)
        self.assertEqual(self.ram,bytearray(bytes(range(128))))
        self.assertEqual(self.cache.read(0x80000020,4),0x23222120)
        self.assertEqual(self.cache.writebacks,0)
        self.assertFalse(self.cache.lines[32].dirty)

    def test_dirty_ocbp_overwrites_new_backing(self):
        self.cache.seed(32,bytes([0xa5])*32,True)
        self.cache.ocbp(0x80000020)
        self.assertEqual(self.ram[32:64],bytes([0xa5])*32)
        self.assertEqual(self.cache.writebacks,1)

    def test_clean_ocbp_does_not_write_stale_bytes(self):
        self.cache.seed(32,bytes([0xa5])*32,False)
        self.cache.ocbp(0x80000020)
        self.assertEqual(self.ram,bytearray(bytes(range(128))))
        self.assertEqual(self.cache.writebacks,0)

    def test_neighboring_dirty_line_is_independent(self):
        self.cache.seed(0,bytes([0x37])*32,True)
        self.cache.seed(32,bytes([0xa5])*32,True)
        self.cache.ocbi(0x80000020)
        self.assertEqual(self.cache.lines[0].data,bytes([0x37])*32)
        self.assertTrue(self.cache.lines[0].dirty)

    def test_cache_read_stays_stale_until_invalidated(self):
        self.cache.seed(32,bytes([0xa5])*32,False)
        self.assertEqual(self.cache.read(0x80000020,4),0xa5a5a5a5)
        self.ram[32:36]=b'live'
        self.cache.ocbi(0x80000020)
        self.assertEqual(self.cache.read(0x80000020,4),int.from_bytes(b'live','little'))

    def test_cross_line_access_is_not_silently_approximated(self):
        with self.assertRaises(ValueError):self.cache.read(0x8000003f,4)

    def test_cloned_machine_cache_uses_its_own_backing(self):
        class Machine:
            def __init__(self):
                self.ram=bytearray(64)
                self.cache=OperandLines(self.block,self.store)
            def block(self,a,n):return bytes(self.ram[a:a+n])
            def store(self,a,b):self.ram[a:a+len(b)]=b
        before=Machine();after=copy.deepcopy(before)
        after.ram[32:36]=b'live'
        self.assertEqual(after.cache.read(0x80000020,4),int.from_bytes(b'live','little'))
        self.assertEqual(before.cache.read(0x80000020,4),0)


if __name__=='__main__':unittest.main()
