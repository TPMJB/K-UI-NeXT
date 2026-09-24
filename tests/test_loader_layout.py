#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Malformed executable checks independent of the SH toolchain."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_loader_layout import EH, PH, SH, SYM, LOW, inspect_elf


def executable():
    data = bytearray(1024)
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    EH.pack_into(data, 0, ident, 2, 42, 1, LOW, 52, 0x200, 0, 52, 32, 1, 40, 3, 0)
    PH.pack_into(data, 52, 1, 0x100, LOW, LOW, 16, 32, 7, 4)
    data[0x100:0x110] = bytes(range(16))
    SH.pack_into(data, 0x200 + 40, 0, 2, 0, 0, 0x340, 32, 2, 1, 4, 16)
    SH.pack_into(data, 0x200 + 80, 0, 3, 0, 0, 0x300, 8, 0, 0, 1, 0)
    data[0x300:0x308] = b"\0_start\0"
    SYM.pack_into(data, 0x350, 1, LOW, 0, 0x12, 0, 0xFFF1)
    return data


class LoaderLayoutTests(unittest.TestCase):
    def test_valid(self):
        image = inspect_elf(executable(), LOW, LOW + 0x100000)
        self.assertEqual(image["payload"], bytes(range(16)))
        self.assertEqual(image["memory_end"], LOW + 32)

    def test_truncation(self):
        for size in (0, 51, 83, 0x100, 0x250, 0x357):
            with self.subTest(size=size), self.assertRaises(ValueError):
                inspect_elf(executable()[:size], LOW, LOW + 0x100000)

    def test_bad_headers_and_segments(self):
        for offset, fmt, value in ((18, "H", 3), (24, "I", LOW + 4),
                                   (42, "H", 0), (48, "H", 0),
                                   (52, "I", 2), (52, "I", 3), (52, "I", 7),
                                   (60, "I", LOW - 4), (64, "I", LOW + 4),
                                   (68, "I", 33), (72, "I", 0),
                                   (72, "I", 0x100001), (76, "I", 6)):
            data = executable()
            struct.pack_into("<" + fmt, data, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                inspect_elf(data, LOW, LOW + 0x100000)

    def test_unresolved_symbol(self):
        data = executable()
        struct.pack_into("<H", data, 0x350 + 14, 0)
        with self.assertRaisesRegex(ValueError, "Unresolved"):
            inspect_elf(data, LOW, LOW + 0x100000)

    def test_unallocated_common(self):
        data = executable()
        struct.pack_into("<H", data, 0x350 + 14, 0xFFF2)
        with self.assertRaisesRegex(ValueError, "common"):
            inspect_elf(data, LOW, LOW + 0x100000)

    def test_bad_symbol_name(self):
        data = executable()
        struct.pack_into("<I", data, 0x350, 999)
        with self.assertRaisesRegex(ValueError, "symbol name"):
            inspect_elf(data, LOW, LOW + 0x100000)

    def test_overlapping_segments(self):
        data = executable()
        struct.pack_into("<H", data, 44, 2)
        PH.pack_into(data, 84, 1, 0x100, LOW + 8, LOW + 8, 16, 16, 7, 4)
        with self.assertRaisesRegex(ValueError, "Overlapping"):
            inspect_elf(data, LOW, LOW + 0x100000)


if __name__ == "__main__":
    unittest.main()
