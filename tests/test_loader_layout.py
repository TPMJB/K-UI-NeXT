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

    def test_file_metadata_is_distinct_from_runtime_symbols(self):
        data = executable()
        label = b"_sd_reader.c.12345678\0"
        data[0x308:0x308 + len(label)] = label
        strings = list(SH.unpack_from(data, 0x250)); strings[5] += len(label)
        SH.pack_into(data, 0x250, *strings)
        table = list(SH.unpack_from(data, 0x228)); table[5] += SYM.size
        SH.pack_into(data, 0x228, *table)
        SYM.pack_into(data, 0x360, 8, 0, 0, 4, 0, 0xFFF1)
        self.assertNotIn(label[:-1].decode(), inspect_elf(data, LOW, LOW + 0x100000)["symbols"])
        # A real local function with the same label stays visible to the
        # forbidden-device-symbol audit; names alone never grant exemption.
        SYM.pack_into(data, 0x360, 8, LOW + 4, 0, 2, 0, 0xFFF1)
        self.assertEqual(inspect_elf(data, LOW, LOW + 0x100000)["symbols"][label[:-1].decode()], LOW + 4)
        SYM.pack_into(data, 0x360, 8, LOW + 4, 0, 4, 0, 0xFFF1)
        with self.assertRaisesRegex(ValueError, "file metadata"):
            inspect_elf(data, LOW, LOW + 0x100000)
        # LTO source-unit anchors are hidden weak NOTYPE symbols in an
        # unallocated debug section, rather than STT_FILE on this SH compiler.
        struct.pack_into("<H", data, 48, 4)
        SH.pack_into(data, 0x278, 0, 1, 0, 0, 0x380, 16, 0, 0, 1, 0)
        SYM.pack_into(data, 0x360, 8, 4, 0, 0x20, 2, 3)
        self.assertNotIn(label[:-1].decode(), inspect_elf(data, LOW, LOW + 0x100000)["symbols"])
        # Even in that section, a real function is retained for later checks.
        SYM.pack_into(data, 0x360, 8, 4, 0, 0x22, 2, 3)
        self.assertIn(label[:-1].decode(), inspect_elf(data, LOW, LOW + 0x100000)["symbols"])
        # The same weak NOTYPE shape in loaded memory is not debug metadata.
        SH.pack_into(data, 0x278, 0, 1, 2, LOW, 0x100, 16, 0, 0, 1, 0)
        SYM.pack_into(data, 0x360, 8, LOW + 4, 0, 0x20, 2, 3)
        self.assertIn(label[:-1].decode(), inspect_elf(data, LOW, LOW + 0x100000)["symbols"])

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
