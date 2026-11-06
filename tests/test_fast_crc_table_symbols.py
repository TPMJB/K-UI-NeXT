#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CRC discovery must retain local symbols with identical C names."""
import struct
import unittest

from test_smash_native_fast_crc import linked_byte_table,table_bytes


def fixture():
    data=bytearray(64+2048)
    data[:32]=struct.pack('<IIIBBH',1,0x8ce01000,1024,1,0,2)+\
        struct.pack('<IIIBBH',1,0x8ce01400,1024,1,0,2)
    data[64:]=table_bytes()*2
    sections=[(0,)*10,(0,2,0,0,0,32,0,0,0,16),
        (0,1,2,0x8ce01000,64,2048,0,0,4,0)]
    # The name dictionary loses the first _table.0, as a linked multi-TU ELF
    # may do. Discovery must inspect the two complete symbol records instead.
    return (bytes(data),None,[],sections,{'_table.0':(0x8ce01400,1024,1,2)})


class TableSymbols(unittest.TestCase):
    def test_duplicate_local_names_bind_exact_helper_table(self):
        self.assertEqual(linked_byte_table(fixture(),struct.pack('<I',0x8ce01000)),
            (0x8ce01000,2))

    def test_missing_or_ambiguous_helper_table_is_refused(self):
        for body in (bytes(4),struct.pack('<II',0x8ce01000,0x8ce01400)):
            with self.subTest(body=body),self.assertRaises(ValueError):
                linked_byte_table(fixture(),body)

    def test_writable_or_corrupt_table_is_refused(self):
        for writable in (False,True):
            image=list(fixture())
            if writable:
                image[3]=list(image[3]);section=list(image[3][2]);section[2]=3
                image[3][2]=tuple(section)
            else:
                data=bytearray(image[0]);data[64]^=1;image[0]=bytes(data)
            with self.subTest(writable=writable),self.assertRaises(ValueError):
                linked_byte_table(image,struct.pack('<I',0x8ce01000))


if __name__=='__main__':unittest.main()
