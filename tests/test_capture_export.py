#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent host decoders inspect the actual console writer's containers."""
import ctypes.util
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("capture_export_importer", ROOT / "tools/game_image_import.py")
importer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(importer)


class CaptureExportOracleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binary = ROOT / "build/test-capture-export"
        if not binary.exists():
            subprocess.run(["make", "build/test-capture-export", "SANITIZERS="], cwd=ROOT, check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        cls.temporary = tempfile.TemporaryDirectory(prefix="kui-export-oracle-")
        cls.base = Path(cls.temporary.name)
        cls.directory = cls.base / "original"
        cls.directory.mkdir()
        result = subprocess.run([str(binary), str(cls.directory)], text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, check=True)
        if "checks passed" not in result.stdout:
            raise AssertionError(result.stdout)
        cls.expected = (cls.directory / "expected.iso").read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def inspect(self, suffix, decoder):
        data = (self.directory / ("test" + suffix)).read_bytes()
        magic, header, logical, block, version, shift, reserved = struct.unpack_from("<4sIQIBB2s", data)
        self.assertEqual((magic, header, logical, block, version, shift, reserved),
                         (b"CISO" if suffix == ".cso" else b"ZISO", 24, len(self.expected), 2048, 1, 0, b"\0\0"))
        count = logical // block
        indices = struct.unpack_from(f"<{count + 1}I", data, 24)
        self.assertEqual(indices[-1], len(data))
        restored = bytearray()
        methods = set()
        for first, following in zip(indices, indices[1:]):
            raw = bool(first & 0x80000000)
            payload = data[first & 0x7fffffff:following & 0x7fffffff]
            value = payload if raw else decoder(payload)
            self.assertEqual(len(value), 2048)
            restored.extend(value)
            methods.add(raw)
        self.assertEqual(methods, {False, True}, "Both compression and raw fallback must be exercised")
        self.assertEqual(bytes(restored), self.expected)

    def test_cso_decodes_with_python_zlib_and_computer_importer(self):
        self.inspect(".cso", lambda data: zlib.decompress(data, -15))
        target = self.base / "cso-imported"
        report = importer.import_image(self.directory / "test.cso", target)
        self.assertEqual((target / "disc.iso").read_bytes(), self.expected)
        self.assertEqual(report["image"]["blocks"], 16)

    def test_swat_zso_decodes_with_independent_system_lzo_and_importer(self):
        library = os.environ.get("KUI_LZO2_LIBRARY") or ctypes.util.find_library("lzo2")
        if not library:
            if os.environ.get("KUI_REQUIRE_CODEC_ORACLES") == "1":
                self.fail("liblzo2 is required for the independent ZSO oracle")
            self.skipTest("Independent liblzo2 unavailable; core safe-LZO readback is tested")
        self.inspect(".zso", lambda data: importer.decode_lzo(data, 2048, library=library)[0])
        target = self.base / "zso-imported"
        report = importer.import_image(self.directory / "test.zso", target, zso_codec="lzo", lzo_library=library)
        self.assertEqual((target / "disc.iso").read_bytes(), self.expected)
        self.assertEqual(report["image"]["zso_codec"], "lzo")


if __name__ == "__main__":
    unittest.main()
