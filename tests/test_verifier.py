# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location("verify_probe", Path(__file__).parents[1] / "tools/verify_probe.py")
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


class VerifierTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = verifier.pattern(0, 100013)
        self.manifest = {"schema": 1, "complete": True,
                         "pattern": "kui-xorshift32-v1", "bytes": len(self.data),
                         "crc32": f"{zlib.crc32(self.data):08x}"}
        self.save()

    def save(self):
        (self.root / "storage.bin").write_bytes(self.data)
        (self.root / "storage.json").write_text(json.dumps(self.manifest))

    def test_complete(self):
        result = verifier.verify(self.root)
        self.assertEqual(result["bytes"], 100013)
        self.assertEqual(len(result["sha256"]), 64)

    def test_corruption_even_with_updated_crc(self):
        self.data = self.data[:65537] + bytes([self.data[65537] ^ 1]) + self.data[65538:]
        self.manifest["crc32"] = f"{zlib.crc32(self.data):08x}"
        self.save()
        with self.assertRaisesRegex(ValueError, "65537"):
            verifier.verify(self.root)

    def test_truncation(self):
        (self.root / "storage.bin").write_bytes(self.data[:-1])
        with self.assertRaisesRegex(ValueError, "length"):
            verifier.verify(self.root)

    def test_incomplete(self):
        self.manifest["complete"] = False
        self.save()
        with self.assertRaises(ValueError):
            verifier.verify(self.root)

    def test_chunk_boundaries(self):
        for offset in (0, 3, 65531, 1048577):
            self.assertEqual(verifier.pattern(offset, 173),
                verifier.pattern(offset, 7) + verifier.pattern(offset + 7, 166))


if __name__ == "__main__":
    unittest.main()
