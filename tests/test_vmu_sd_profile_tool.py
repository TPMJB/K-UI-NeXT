#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import struct
import sys
from pathlib import Path
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from vmu_sd_profile import decode_profile


def seal(data):
    struct.pack_into("<I", data, 16, 0)
    struct.pack_into("<I", data, 16, zlib.crc32(data))
    return bytes(data)


def fixture():
    data = bytearray(1536)
    data[:8] = b"KUIVMUR1"
    for at, value in ((8, 1), (12, 1536), (20, 1), (24, 131072), (28, 1), (120, 1)):
        struct.pack_into("<I", data, at, value)
    struct.pack_into("<QQQ", data, 32, 65536, 2048, 65536)
    data[56:120] = bytes(range(1, 65))
    struct.pack_into("<III", data, 128, 0, 8192, 256)
    return bytearray(seal(data))


class ProfileToolTest(unittest.TestCase):
    def test_canonical_mapping_and_summary(self):
        summary = decode_profile(fixture())
        self.assertEqual(summary["extents"], [dict(logical_block=0, lba=8192, blocks=256)])
        self.assertNotIn("cid", summary)

    def test_wire_corruption(self):
        data = fixture()
        for at in range(len(data)):
            changed = data.copy()
            changed[at] ^= 1
            with self.assertRaises(ValueError):
                decode_profile(changed)
        for size in (0, 64, 1535, 1537):
            with self.assertRaises(ValueError):
                decode_profile(bytes(data[:size]) + (b"x" if size == 1537 else b""))

    def test_resealed_invalid_fields(self):
        for at, fmt, value in ((8, "I", 2), (20, "I", 2), (24, "I", 512),
                               (28, "I", 2), (32, "Q", 0x100000001),
                               (48, "Q", 65537), (120, "I", 17),
                               (124, "I", 1), (128, "I", 1), (132, "I", 2047),
                               (136, "I", 255), (140, "I", 1), (1344, "I", 1)):
            data = fixture()
            struct.pack_into("<" + fmt, data, at, value)
            with self.assertRaises(ValueError, msg=str(at)):
                decode_profile(seal(data))

    def test_overlap_and_uncoalesced_extents(self):
        for next_lba in (8192, 8320):
            data = fixture()
            struct.pack_into("<I", data, 120, 2)
            struct.pack_into("<III", data, 128, 0, 8192, 128)
            struct.pack_into("<III", data, 140, 128, next_lba, 128)
            with self.assertRaises(ValueError):
                decode_profile(seal(data))


if __name__ == "__main__":
    unittest.main()
