# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import retail_package
import zso_package as layout
from runtime_package import envelope


class ZsoPackage(unittest.TestCase):
    def payload(self, stage_bytes=128):
        payload = bytearray(layout.STAGE_BLOB_OFFSET + stage_bytes)
        payload[layout.HEADER_OFFSET:layout.HEADER_OFFSET + layout.HEADER_BYTES] = layout.relocation_header(stage_bytes)
        return payload

    @staticmethod
    def package(payload, memory=None):
        return envelope(payload, len(payload) if memory is None else memory, "012345abcdef")

    def test_exact_independent_header_tuple_and_supported_scope(self):
        header = struct.pack("<8s14I", b"KUIRZB01", 1, 64, 4096, 4096,
                             0x8CE00000, 128, 0x8CE00000, 0x8C010000,
                             0xC00000, 0x8CFF0000, 8192, 0x8C004000, 0x8C007800, 0)
        self.assertEqual(layout.relocation_header(128), header)
        for size in (4, 128, 0x100000):
            with self.subTest(size=size):
                result = layout.inspect_zso(self.package(self.payload(size)))
                self.assertEqual(result["stage_bytes"], size)
                self.assertEqual(result["resident_address"], "0x8c004000")
                self.assertEqual(result["manifest_magic"], "KUIRZS01")
                self.assertEqual(result["physical_file_fragments"], 31)
                self.assertEqual(result["reader"], "Standard")
                self.assertEqual(result["transport"], "SCI")
                self.assertIn("console", result["abi"])

    def test_every_inner_header_byte_checked_after_outer_checksum_rebuilt(self):
        clean = self.payload()
        for offset in range(layout.HEADER_OFFSET, layout.HEADER_OFFSET + layout.HEADER_BYTES):
            payload = clean.copy()
            payload[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "relocation header"):
                layout.inspect_zso(self.package(payload))

    def test_manifest_must_be_completely_blank(self):
        for offset in (0, 1, 511, 512, 2047, 4095):
            payload = self.payload()
            payload[layout.MAP_OFFSET + offset] = 1
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "blank card-specific"):
                layout.inspect_zso(self.package(payload))

    def test_ram_patch_capabilities_are_forbidden_even_unaligned_or_populated(self):
        for relative in (0, 1, 4, 63):
            for populated in (False, True):
                payload = self.payload(256)
                seed = bytearray(retail_package.RAM_PATCH_SEED)
                if populated:
                    seed[16] = 1
                start = layout.STAGE_BLOB_OFFSET + relative
                payload[start:start + len(seed)] = seed
                with self.subTest(relative=relative, populated=populated), self.assertRaisesRegex(ValueError, "RAM patch"):
                    layout.inspect_zso(self.package(payload))

    def test_no_ordinary_or_ce_package_admission_in_either_direction(self):
        for ce, low in ((False, False), (False, True), (True, False)):
            payload = self.payload()
            payload[layout.HEADER_OFFSET:layout.HEADER_OFFSET + layout.HEADER_BYTES] = \
                retail_package.relocation_header(128, ce=ce, low=low)
            with self.subTest(ce=ce, low=low), self.assertRaisesRegex(ValueError, "relocation header"):
                layout.inspect_zso(self.package(payload))
        for ce in (False, True):
            with self.subTest(ce=ce), self.assertRaisesRegex(ValueError, "relocation header"):
                retail_package.inspect_retail(self.package(self.payload()), ce=ce)

    def test_size_alignment_memory_and_header_stage_length(self):
        for size in (0, layout.STAGE_MAX_BYTES + 4):
            with self.subTest(size=size), self.assertRaisesRegex(ValueError, "staging size"):
                layout.inspect_zso(self.package(self.payload(size)))
        payload = self.payload()
        with self.assertRaisesRegex(ValueError, "staging size"):
            layout.inspect_zso(self.package(payload, len(payload) + 4))
        for data in (payload[:-4], payload + bytes(4)):
            with self.assertRaisesRegex(ValueError, "relocation header"):
                layout.inspect_zso(self.package(data))
        for size in (0, 3, 7, 129, 0x100004):
            changed = payload.copy()
            struct.pack_into("<I", changed, layout.HEADER_OFFSET + 28, size)
            with self.subTest(size=size), self.assertRaisesRegex(ValueError, "relocation header"):
                layout.inspect_zso(self.package(changed))

    def test_runtime_envelope_corruption_and_truncation(self):
        package = self.package(self.payload())
        for offset in (0, 40, 60, 64, len(package) - 1):
            changed = bytearray(package)
            changed[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                layout.inspect_zso(changed)
        for size in (0, 63, 64, len(package) - 1):
            with self.subTest(size=size), self.assertRaises(ValueError):
                layout.inspect_zso(package[:size])


class LzoPackage(unittest.TestCase):
    def package(self, codec="lzo"):
        payload = bytearray(layout.STAGE_BLOB_OFFSET + 128)
        payload[layout.HEADER_OFFSET:layout.HEADER_OFFSET + layout.HEADER_BYTES] = layout.relocation_header(128, codec)
        return envelope(payload, len(payload), "012345abcdef")

    def test_exact_typed_contract_and_no_codec_substitution(self):
        result = layout.inspect_zso(self.package(), "lzo")
        self.assertEqual(result["manifest_magic"], "KUIRLS01")
        self.assertIn("LZO1X", result["codec"])
        expected = bytearray(layout.relocation_header(128))
        expected[:8] = b"KUIRLB01"
        self.assertEqual(layout.relocation_header(128, "lzo"), expected)
        for selected, packaged in (("lz4", "lzo"), ("lzo", "lz4")):
            with self.subTest(selected=selected), self.assertRaisesRegex(ValueError, "relocation header"):
                layout.inspect_zso(self.package(packaged), selected)

    def test_every_header_and_map_byte_remains_checked(self):
        clean = self.package()[64:]
        for offset in (*range(layout.HEADER_OFFSET, layout.HEADER_OFFSET + layout.HEADER_BYTES),
                       layout.MAP_OFFSET, layout.MAP_OFFSET + layout.MAP_BYTES - 1):
            payload = bytearray(clean)
            payload[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                layout.inspect_zso(envelope(payload, len(payload), "012345abcdef"), "lzo")


class CsoPackage(unittest.TestCase):
    def package(self, codec="cso"):
        payload = bytearray(layout.STAGE_BLOB_OFFSET + 128)
        payload[layout.HEADER_OFFSET:layout.HEADER_OFFSET + layout.HEADER_BYTES] = layout.relocation_header(128, codec)
        return envelope(payload, len(payload), "012345abcdef")

    def test_exact_ciso_contract_and_every_codec_substitution_are_distinct(self):
        expected = struct.pack("<8s14I", b"KUIRCB01", 1, 64, 4096, 4096,
                               0x8CE00000, 128, 0x8CE00000, 0x8C010000,
                               0xC00000, 0x8CFF0000, 8192, 0x8C004000, 0x8C007800, 0)
        self.assertEqual(layout.relocation_header(128, "cso"), expected)
        result = layout.inspect_zso(self.package(), "cso")
        self.assertEqual(result["manifest_magic"], "KUIRCS01")
        self.assertIn("CISO v1 raw DEFLATE", result["codec"])
        self.assertEqual((result["transport"], result["reader"]), ("SCI", "Standard"))
        for selected, packaged in (("cso", "lz4"), ("cso", "lzo"), ("lz4", "cso"), ("lzo", "cso")):
            with self.subTest(selected=selected, packaged=packaged), self.assertRaisesRegex(ValueError, "relocation header"):
                layout.inspect_zso(self.package(packaged), selected)

    def test_every_typed_header_byte_and_manifest_boundaries_remain_checked(self):
        clean = self.package()[64:]
        for offset in (*range(layout.HEADER_OFFSET, layout.HEADER_OFFSET + layout.HEADER_BYTES),
                       layout.MAP_OFFSET, layout.MAP_OFFSET + layout.MAP_BYTES - 1):
            payload = bytearray(clean)
            payload[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                layout.inspect_zso(envelope(payload, len(payload), "012345abcdef"), "cso")


if __name__ == "__main__":
    unittest.main()
