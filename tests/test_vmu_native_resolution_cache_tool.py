# SPDX-License-Identifier: GPL-3.0-only
"""Host CLI integration. Set KUI_NATIVE_STARTUP to run actual-image cases."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCES = (
    "tools/vmu_native_resolution_cache_tool.c",
    "src/core/vmu_native_resolution_cache.c",
    "src/core/vmu_native_discovery.c",
    "src/core/vmu_native_layout.c",
    "src/core/vmu_native_heap.c",
    "src/core/vmu_native_exit.c",
    "src/core/vmu_native_patch.c",
    "src/core/hash.c",
    "src/core/data.c",
)


class CacheToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiled = tempfile.TemporaryDirectory(prefix="kui-native-cache-tool-")
        cls.tool = Path(cls.compiled.name) / "cache-tool"
        subprocess.run(
            [os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-Wpedantic", "-Iinclude", *SOURCES, "-o", str(cls.tool)],
            cwd=ROOT, check=True, capture_output=True, text=True,
        )
        actual = os.environ.get("KUI_NATIVE_STARTUP")
        cls.actual = Path(actual).resolve() if actual else None

    @classmethod
    def tearDownClass(cls):
        cls.compiled.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="kui cache CLI ")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)

    def invoke(self, *args, code=0):
        result = subprocess.run([str(self.tool), *map(str, args)], cwd=self.work,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, code, result.stderr)
        return result

    def build_actual(self):
        if self.actual is None:
            self.skipTest("Set KUI_NATIVE_STARTUP for actual-image CLI coverage")
        cache = self.work / "game record.nvr"
        result = json.loads(self.invoke("build", self.actual, cache).stdout)
        self.assertEqual(len(cache.read_bytes()), 1024)
        return cache, result

    def test_usage_and_load_or_pin_parsing(self):
        self.invoke(code=2)
        self.invoke("unknown", code=2)
        self.invoke("inspect", code=2)
        self.invoke("build", "image", "cache", "not-hex", code=2)
        self.invoke("build", "image", "cache", "100000000", code=2)
        self.invoke("replay", "image", "cache", "abc", code=2)
        self.invoke("replay", "image", "cache", "z" * 64, code=2)

    def test_unsupported_or_oversized_image_creates_nothing(self):
        image, cache = self.work / "image.bin", self.work / "cache.nvr"
        image.write_bytes(bytes(1024))
        self.invoke("build", image, cache, code=1)
        self.assertFalse(cache.exists())
        with image.open("wb") as handle:
            handle.truncate(0xC00001)
        self.invoke("build", image, cache, code=1)
        self.assertFalse(cache.exists())

    def test_inspect_refuses_lengths_and_random_bytes(self):
        record = self.work / "invalid.nvr"
        for size in (0, 1, 128, 1023, 1024, 1025):
            record.write_bytes(bytes(size))
            self.invoke("inspect", record, code=1)

    def test_actual_build_inspect_verify_and_trusted_replay(self):
        cache, metadata = self.build_actual()
        wire = cache.read_bytes()
        self.assertEqual(metadata["record_digest"], hashlib.sha256(wire[:992]).hexdigest())
        self.assertEqual(bytes.fromhex(metadata["record_digest"]), wire[992:])
        self.assertEqual(metadata["image_sha256"], hashlib.sha256(self.actual.read_bytes()).hexdigest())
        self.assertEqual(metadata, json.loads(self.invoke("inspect", cache).stdout))
        self.assertEqual(metadata, json.loads(self.invoke("verify", self.actual, cache).stdout))
        self.assertEqual(metadata, json.loads(self.invoke("replay", self.actual, cache,
                                                        metadata["record_digest"]).stdout))
        wrong = bytes.fromhex(metadata["record_digest"])
        wrong = bytes([wrong[0] ^ 1]) + wrong[1:]
        self.invoke("replay", self.actual, cache, wrong.hex(), code=1)

    def test_actual_existing_output_and_source_stay_unchanged(self):
        cache, metadata = self.build_actual()
        before = cache.read_bytes()
        self.invoke("build", self.actual, cache, code=1)
        self.assertEqual(cache.read_bytes(), before)
        self.assertEqual(hashlib.sha256(self.actual.read_bytes()).hexdigest(), metadata["image_sha256"])

    def test_actual_stale_image_misses(self):
        cache, metadata = self.build_actual()
        image = self.work / "changed image.bin"
        data = bytearray(self.actual.read_bytes())
        data[-1] ^= 1
        image.write_bytes(data)
        self.invoke("replay", image, cache, metadata["record_digest"], code=1)
        self.invoke("verify", image, cache, code=1)

    def test_actual_resealed_metadata_needs_the_independent_pin(self):
        cache, metadata = self.build_actual()
        wire = bytearray(cache.read_bytes())
        submit = struct.unpack_from("<I", wire, 88)[0]
        struct.pack_into("<I", wire, 88, submit + 4)
        wire[992:] = hashlib.sha256(wire[:992]).digest()
        cache.write_bytes(wire)
        # Integrity inspection has no authority to certify target addresses.
        inspected = json.loads(self.invoke("inspect", cache).stdout)
        self.assertNotEqual(inspected["record_digest"], metadata["record_digest"])
        self.invoke("replay", self.actual, cache, metadata["record_digest"], code=1)
        self.invoke("verify", self.actual, cache, code=1)
        struct.pack_into("<I", wire, 16, 2)  # Stale detector epoch, even with a new local hash.
        wire[992:] = hashlib.sha256(wire[:992]).digest()
        cache.write_bytes(wire)
        self.invoke("inspect", cache, code=1)


if __name__ == "__main__":
    unittest.main()
