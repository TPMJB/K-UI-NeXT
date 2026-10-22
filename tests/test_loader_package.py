# SPDX-License-Identifier: GPL-3.0-only
import pathlib
import struct
import sys
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
from loader_package import inspect_probe
from runtime_package import envelope


class LoaderPackage(unittest.TestCase):
    def payload(self):
        data = bytearray(0x2010)
        struct.pack_into("<8s14I", data, 0x100, b"KUILDR01", 1, 64, 0x1000,
                         1600, 0x8ce00000, 16, 0x8ce00000, 0x8c010000,
                         0x8cd00000, 0x8cff0000, 0x2000, 0x100000, 0, 0)
        return data

    def test_envelope_and_layout(self):
        data = self.payload()
        result = inspect_probe(envelope(data, len(data), "012345abcdef"))
        self.assertEqual(result["resident_bytes"], 16)
        for offset in range(0x100, 0x140):
            with self.subTest(offset=offset):
                damaged = data.copy()
                damaged[offset] ^= 1
                with self.assertRaises(ValueError):
                    inspect_probe(envelope(damaged, len(damaged), "012345abcdef"))

    def test_no_stale_card_map_or_tail(self):
        for offset in (0x1000, 0x1000+1599):
            data = self.payload(); data[offset] = 1
            with self.assertRaises(ValueError):
                inspect_probe(envelope(data, len(data), "012345abcdef"))
        data = self.payload()
        with self.assertRaises(ValueError):
            inspect_probe(envelope(data, len(data)+4, "012345abcdef"))
        with self.assertRaises(ValueError):
            inspect_probe(envelope(data+bytes(4), len(data)+4, "012345abcdef"))

    def test_payload_corruption(self):
        data = self.payload()
        package = bytearray(envelope(data, len(data), "012345abcdef"))
        package[-1] ^= 1
        with self.assertRaises(ValueError): inspect_probe(package)
