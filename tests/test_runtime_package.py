# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import runtime_package as runtime


def elf_fixture():
    data = bytearray(264)
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    runtime.ELF_HEADER.pack_into(data, 0, ident, 2, 42, 1, runtime.ADDRESS,
        52, 0, 12, 52, 32, 2, 0, 0, 0)
    runtime.PROGRAM_HEADER.pack_into(data, 52, 1, 256, runtime.ADDRESS,
        runtime.ADDRESS, 4, 4, 5, 4)
    runtime.PROGRAM_HEADER.pack_into(data, 84, 1, 260, runtime.ADDRESS + 16,
        runtime.ADDRESS + 16, 4, 64, 6, 4)
    data[256:264] = b"\x09\x00\x09\x00DATA"
    return data


class RuntimePackageTests(unittest.TestCase):
    def test_load_segments_gaps_and_bss(self):
        payload, memory = runtime.flatten_elf(elf_fixture())
        self.assertEqual(payload, b"\x09\x00\x09\x00" + bytes(12) + b"DATA")
        self.assertEqual(memory, 80)
        package = runtime.envelope(payload, memory, "0123456789ab")
        result = runtime.verify(package)
        self.assertEqual(result["payload_bytes"], 20)
        self.assertEqual(result["memory_bytes"], 80)
        self.assertEqual(struct.unpack_from("<I", package, 32)[0], zlib.crc32(payload))

    def test_reject_wrong_elf_architecture_entry_and_truncation(self):
        for offset, fmt, value in ((18, "H", 3), (24, "I", runtime.ADDRESS + 4),
                                   (28, "I", 0xFFFFFFF0), (42, "H", 16)):
            data = elf_fixture()
            struct.pack_into("<" + fmt, data, offset, value)
            with self.assertRaises(ValueError): runtime.flatten_elf(data)
        with self.assertRaises(ValueError): runtime.flatten_elf(elf_fixture()[:-1])

    def test_reject_overlap_dynamic_and_out_of_range_segments(self):
        for offset, value in ((84, 2), (84 + 8, runtime.ADDRESS + 2),
                              (84 + 20, runtime.MAX_MEMORY + 1), (84 + 16, 65)):
            data = elf_fixture()
            struct.pack_into("<I", data, offset, value)
            with self.assertRaises(ValueError): runtime.flatten_elf(data)

    def test_rejection_fixtures_and_trailing_data(self):
        good = runtime.envelope(b"abcdefgh", 16, "0123456789ab")
        for name, data in runtime.rejection_cases(good).items():
            with self.subTest(name=name), self.assertRaises(ValueError): runtime.verify(data)
        with self.assertRaises(ValueError): runtime.verify(good + b"extra")

    def test_size_alignment_and_build_id(self):
        for payload, memory, build in ((b"abc", 4, "0123456789ab"),
                (b"abcd", 3, "0123456789ab"), (b"abcd", runtime.MAX_MEMORY + 4, "0123456789ab"),
                (b"abcd", 4, "bad\nversion!")):
            with self.assertRaises(ValueError): runtime.envelope(payload, memory, build)


if __name__ == "__main__":
    unittest.main()
