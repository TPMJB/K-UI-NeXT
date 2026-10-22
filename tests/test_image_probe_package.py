# SPDX-License-Identifier: GPL-3.0-only
import pathlib
import re
import struct
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import image_probe_package as layout
from image_probe_package import inspect_image_probe
from loader_package import inspect_probe
from runtime_package import envelope


class ImageProbePackage(unittest.TestCase):
    def payload(self, resident_bytes=16):
        data = bytearray(layout.RESIDENT_BLOB_OFFSET + resident_bytes)
        layout.HEADER.pack_into(
            data, layout.HEADER_OFFSET, layout.MAGIC, layout.VERSION,
            layout.HEADER_BYTES, layout.MANIFEST_OFFSET, layout.MANIFEST_BYTES,
            layout.RESIDENT_ADDRESS, resident_bytes, layout.RESIDENT_ADDRESS,
            layout.CLIENT_ADDRESS, layout.CLIENT_STACK, layout.RESIDENT_STACK,
            layout.RESIDENT_BLOB_OFFSET, layout.CLIENT_MAX_BYTES, 0, 0,
        )
        return data

    def packaged(self, data):
        return envelope(data, len(data), "012345abcdef")

    def test_constants_match_native_contract(self):
        text = (ROOT / "include/kui/image_loader_layout.h").read_text()
        names = {
            "KUI_IMAGE_PACKAGE_VERSION": layout.VERSION,
            "KUI_IMAGE_PACKAGE_HEADER_OFFSET": layout.HEADER_OFFSET,
            "KUI_IMAGE_PACKAGE_HEADER_BYTES": layout.HEADER_BYTES,
            "KUI_IMAGE_MANIFEST_OFFSET": layout.MANIFEST_OFFSET,
            "KUI_IMAGE_MANIFEST_BYTES": layout.MANIFEST_BYTES,
            "KUI_IMAGE_RESIDENT_BLOB_OFFSET": layout.RESIDENT_BLOB_OFFSET,
            "KUI_IMAGE_RESIDENT_ADDRESS": layout.RESIDENT_ADDRESS,
            "KUI_IMAGE_RESIDENT_MAX_BYTES": layout.RESIDENT_MAX_BYTES,
            "KUI_IMAGE_CLIENT_ADDRESS": layout.CLIENT_ADDRESS,
            "KUI_IMAGE_CLIENT_MAX_BYTES": layout.CLIENT_MAX_BYTES,
            "KUI_IMAGE_CLIENT_STACK": layout.CLIENT_STACK,
            "KUI_IMAGE_RESIDENT_STACK": layout.RESIDENT_STACK,
        }
        for name, expected in names.items():
            with self.subTest(name=name):
                found = re.search(r"^#define " + name + r"\s+(\w+)\s*$", text, re.M)
                self.assertIsNotNone(found)
                self.assertEqual(int(found.group(1), 0), expected)
        self.assertIn('#define KUI_IMAGE_PACKAGE_MAGIC "KUIIMG01"', text)

    def test_valid_minimum_normal_and_maximum(self):
        for resident_bytes in (4, 16, layout.RESIDENT_MAX_BYTES):
            with self.subTest(resident_bytes=resident_bytes):
                result = inspect_image_probe(self.packaged(self.payload(resident_bytes)))
                self.assertEqual(result["resident_bytes"], resident_bytes)
                self.assertEqual(result["manifest_bytes"], 65536)
                self.assertEqual(result["resident_address"], "0x8ce00000")
                self.assertEqual(result["client_address"], "0x8c010000")
                self.assertEqual(result["build"], "012345abcdef")
                self.assertIn("no retail launch", result["abi"])

    def test_each_relocation_header_byte_is_validated(self):
        pristine = self.payload()
        for offset in range(layout.HEADER_OFFSET,
                            layout.HEADER_OFFSET + layout.HEADER_BYTES):
            with self.subTest(offset=offset):
                data = pristine.copy()
                data[offset] ^= 1
                with self.assertRaisesRegex(ValueError, "relocation header"):
                    inspect_image_probe(self.packaged(data))

    def test_manifest_must_be_blank(self):
        for relative in (0, 1, 511, 512, 1600, 32768, 65535):
            with self.subTest(relative=relative):
                data = self.payload()
                data[layout.MANIFEST_OFFSET + relative] = 1
                with self.assertRaisesRegex(ValueError, "blank card-specific manifest"):
                    inspect_image_probe(self.packaged(data))

    def test_exact_staging_size_and_no_trailing_bytes(self):
        pristine = self.payload()
        with self.assertRaisesRegex(ValueError, "staging size"):
            inspect_image_probe(envelope(pristine, len(pristine) + 4, "012345abcdef"))
        for change in (-4, 4):
            data = pristine[:change] if change < 0 else pristine + bytes(change)
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, "relocation header"):
                inspect_image_probe(self.packaged(data))
        for resident_bytes in (0, layout.RESIDENT_MAX_BYTES + 4):
            with self.subTest(resident_bytes=resident_bytes), self.assertRaisesRegex(ValueError, "staging size"):
                inspect_image_probe(self.packaged(self.payload(resident_bytes)))
        data = self.payload()
        struct.pack_into("<I", data, layout.HEADER_OFFSET + 28, 15)
        with self.assertRaisesRegex(ValueError, "relocation header"):
            inspect_image_probe(self.packaged(data))

    def test_truncated_payloads_and_outer_checksums(self):
        pristine = self.payload()
        for size in (4, 0x100, 0x140, 0x1000, 0x11000, 0x12000):
            with self.subTest(size=size), self.assertRaisesRegex(ValueError, "staging size"):
                inspect_image_probe(self.packaged(pristine[:size]))
        package = self.packaged(pristine)
        for offset in (0, 60, 64, len(package) - 1):
            damaged = bytearray(package); damaged[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                inspect_image_probe(damaged)
        for size in (0, 63, len(package) - 1):
            with self.subTest(size=size), self.assertRaises(ValueError):
                inspect_image_probe(package[:size])

    def test_old_and_new_probe_contracts_cannot_be_confused(self):
        with self.assertRaises(ValueError):
            inspect_probe(self.packaged(self.payload()))
        old = bytearray(0x2010)
        struct.pack_into("<8s14I", old, 0x100, b"KUILDR01", 1, 64, 0x1000,
                         1600, 0x8ce00000, 16, 0x8ce00000, 0x8c010000,
                         0x8cd00000, 0x8cff0000, 0x2000, 0x100000, 0, 0)
        self.assertEqual(inspect_probe(self.packaged(old))["resident_bytes"], 16)
        with self.assertRaises(ValueError):
            inspect_image_probe(self.packaged(old))


if __name__ == "__main__":
    unittest.main()
