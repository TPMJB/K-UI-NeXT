# SPDX-License-Identifier: GPL-3.0-only
"""Host CLI integration. Set KUI_NATIVE_STARTUP / KUI_NATIVE_EVOLUTION2 for real images."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from vmu_native_cache_seed import DETECTOR_EPOCH, record_metadata
SOURCES = (
    "tools/vmu_native_resolution_cache_tool.c",
    "src/core/vmu_native_program.c",
    "src/core/vmu_native_resolution_cache.c",
    "src/core/vmu_native_discovery.c",
    "src/core/vmu_native_layout.c",
    "src/core/vmu_native_heap.c",
    "src/core/vmu_native_exit.c",
    "src/core/vmu_native_patch.c",
    "src/core/hash.c",
    "src/core/data.c",
)
SOURCE_FIELDS = {"source_sha256", "source_bytes", "program_bytes", "discarded_suffix_bytes"}
EVOLUTION2_SOURCE_SHA256 = "16b45fcc84992a60d5a64bf64bf22b699b27306b95f177ba3d9730ca380069ae"
EVOLUTION2_SOURCE_BYTES = 4057056
EVOLUTION2_PROGRAM_BYTES = 0x3D67E0


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
        evolution2 = os.environ.get("KUI_NATIVE_EVOLUTION2")
        cls.evolution2 = Path(evolution2).resolve() if evolution2 else None

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

    def evolution2_source(self):
        if self.evolution2 is None:
            self.skipTest("Set KUI_NATIVE_EVOLUTION2 for original Evolution 2 source coverage")
        source = self.evolution2.read_bytes()
        self.assertEqual(len(source), EVOLUTION2_SOURCE_BYTES)
        self.assertEqual(hashlib.sha256(source).hexdigest(), EVOLUTION2_SOURCE_SHA256)
        return source

    def assert_source_identity(self, metadata, source, program_bytes):
        if not source[-0x8000:].startswith(b"SEGA SEGAKATANA SEGA ENTERPRISES"):
            self.assertEqual(program_bytes, len(source))
        self.assertEqual(metadata["source_sha256"], hashlib.sha256(source).hexdigest())
        self.assertEqual(metadata["source_bytes"], len(source))
        self.assertEqual(metadata["program_bytes"], program_bytes)
        self.assertEqual(metadata["discarded_suffix_bytes"], len(source) - program_bytes)
        self.assertEqual(metadata["image_bytes"], program_bytes)
        self.assertEqual(metadata["image_sha256"], hashlib.sha256(source[:program_bytes]).hexdigest())

    def assert_inspected_identity(self, cache, metadata):
        inspected = json.loads(self.invoke("inspect", cache).stdout)
        self.assertTrue(SOURCE_FIELDS.isdisjoint(inspected))
        self.assertEqual(inspected, {key: value for key, value in metadata.items() if key not in SOURCE_FIELDS})

    def test_usage_and_load_or_pin_parsing(self):
        self.invoke(code=2)
        self.invoke("unknown", code=2)
        self.invoke("inspect", code=2)
        self.invoke("build", "image", "cache", "not-hex", code=2)
        self.invoke("build", "image", "cache", "100000000", code=2)
        self.invoke("replay", "image", "cache", "abc", code=2)
        self.invoke("replay", "image", "cache", "z" * 64, code=2)

    def test_detector_epoch_is_current_and_shared_by_c_and_python(self):
        header = (ROOT / "include/kui/vmu_native_resolution_cache.h").read_text()
        epoch = re.search(r"^#define KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH (\d+)u$",
                          header, re.MULTILINE)
        self.assertIsNotNone(epoch)
        self.assertEqual(DETECTOR_EPOCH, 6)
        self.assertEqual(int(epoch.group(1)), DETECTOR_EPOCH)

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
        self.assert_source_identity(metadata, self.actual.read_bytes(), metadata["image_bytes"])
        self.assert_inspected_identity(cache, metadata)
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
        self.assertEqual(hashlib.sha256(self.actual.read_bytes()).hexdigest(), metadata["source_sha256"])

    def test_actual_old_epochs_resealed_record_is_refused_without_deleting_it(self):
        cache, metadata = self.build_actual()
        self.assertEqual(metadata["detector_epoch"], DETECTOR_EPOCH)
        for epoch in range(1, DETECTOR_EPOCH):
            wire = bytearray(cache.read_bytes())
            struct.pack_into("<I", wire, 16, epoch)
            wire[992:] = hashlib.sha256(wire[:992]).digest()
            stale = self.work / f"old epoch{epoch}.nvr"
            stale.write_bytes(wire)
            self.invoke("inspect", stale, code=1)
            self.invoke("verify", self.actual, stale, code=1)
            self.invoke("replay", self.actual, stale, metadata["record_digest"], code=1)
            self.invoke("replay", self.actual, stale, wire[992:].hex(), code=1)
            with self.assertRaisesRegex(ValueError, "version/epoch/schema"):
                record_metadata(wire, wire[992:])
            self.assertEqual(stale.read_bytes(), wire)

    def test_terminal_dispatch_schema_is_typed_and_matches_c_codec(self):
        cache, metadata = self.build_actual()
        original = cache.read_bytes()
        for family, count, fatal, table, accepted in (
                (5, 3, 0x8c010100, 0x8c010200, True),
                (5, 2, 0x8c010100, 0x8c010200, False),
                (5, 3, 0, 0x8c010200, False),
                (5, 3, 0x8c010100, 0, False),
                (4, 2, 0x8c010100, 0x8c010200, False),
                (6, 3, 0x8c010100, 0x8c010200, False)):
            with self.subTest(family=family, count=count, fatal=fatal, table=table):
                wire = bytearray(original)
                for offset, value in ((708, family), (720, count), (732, fatal), (744, table)):
                    struct.pack_into("<I", wire, offset, value)
                wire[992:] = hashlib.sha256(wire[:992]).digest()
                changed = self.work / "typed terminal.nvr"
                changed.write_bytes(wire)
                self.invoke("inspect", changed, code=0 if accepted else 1)
                if accepted:
                    self.assertEqual(record_metadata(wire)["detector_epoch"], DETECTOR_EPOCH)
                    # Schema integrity alone cannot confer a fresh image proof.
                    self.invoke("verify", self.actual, changed, code=1)
                    self.invoke("replay", self.actual, changed, metadata["record_digest"], code=1)
                else:
                    with self.assertRaisesRegex(ValueError, "exit schema"):
                        record_metadata(wire)

    def test_crt_bootstrap_schema_is_typed_and_matches_c_codec(self):
        cache, metadata = self.build_actual()
        original = cache.read_bytes()
        for family,count,fourth,accepted in (
                (6,4,0x8c010300,True),
                (6,3,0x8c010300,False),
                (6,5,0x8c010300,False),
                (6,4,0,False),
                (1,3,0x8c010300,False),
                (1,4,0x8c010300,False),
                (5,3,0x8c010300,False),
                (7,4,0x8c010300,False)):
            with self.subTest(family=family,count=count,fourth=fourth):
                wire = bytearray(original)
                for offset,value in ((708,family),(720,count),(736,fourth)):
                    struct.pack_into('<I',wire,offset,value)
                wire[992:] = hashlib.sha256(wire[:992]).digest()
                changed = self.work / 'typed CRT terminal.nvr'
                changed.write_bytes(wire)
                self.invoke('inspect',changed,code=0 if accepted else 1)
                if accepted:
                    self.assertEqual(record_metadata(wire)['detector_epoch'],DETECTOR_EPOCH)
                    self.invoke('verify',self.actual,changed,code=1)
                    self.invoke('replay',self.actual,changed,metadata['record_digest'],code=1)
                else:
                    with self.assertRaisesRegex(ValueError,'exit schema'):
                        record_metadata(wire)

    def test_evolution2_raw_source_and_program_prefix_share_cache_identity(self):
        source = self.evolution2_source()
        raw_cache = self.work / "source.nvr"
        raw = json.loads(self.invoke("build", self.evolution2, raw_cache).stdout)
        self.assert_source_identity(raw, source, EVOLUTION2_PROGRAM_BYTES)
        self.assertNotEqual(raw["source_sha256"], raw["image_sha256"])
        self.assert_inspected_identity(raw_cache, raw)
        for command in ("verify", "replay"):
            args = (raw["record_digest"],) if command == "replay" else ()
            self.assertEqual(raw, json.loads(self.invoke(command, self.evolution2, raw_cache, *args).stdout))

        prefix = source[:EVOLUTION2_PROGRAM_BYTES]
        prefix_image, prefix_cache = self.work / "program.bin", self.work / "program.nvr"
        prefix_image.write_bytes(prefix)
        program = json.loads(self.invoke("build", prefix_image, prefix_cache).stdout)
        self.assert_source_identity(program, prefix, len(prefix))
        self.assertEqual(raw_cache.read_bytes(), prefix_cache.read_bytes())
        self.assertEqual(program, json.loads(self.invoke("verify", prefix_image, raw_cache).stdout))
        self.assertEqual(program, json.loads(self.invoke("replay", prefix_image, raw_cache,
                                                       raw["record_digest"]).stdout))
        self.assertEqual(hashlib.sha256(self.evolution2.read_bytes()).hexdigest(), EVOLUTION2_SOURCE_SHA256)

    def test_evolution2_footer_contradictions_refused_and_arbitrary_suffix_retained(self):
        source = self.evolution2_source()
        cache = self.work / "source.nvr"
        metadata = json.loads(self.invoke("build", self.evolution2, cache).stdout)
        original_record = cache.read_bytes()
        changed = []
        for footer_offset in (0, 0x60, 0x7FFF):
            data = bytearray(source)
            data[EVOLUTION2_PROGRAM_BYTES + footer_offset] ^= 1
            changed.append((f"changed footer {footer_offset:x}", data))
        changed.append(("duplicate IP suffix", source + source[EVOLUTION2_PROGRAM_BYTES:]))
        for index, (name, data) in enumerate(changed):
            with self.subTest(source=name):
                image, output = self.work / f"changed{index}.bin", self.work / f"changed{index}.nvr"
                image.write_bytes(data)
                self.invoke("build", image, output, code=1)
                self.assertFalse(output.exists())
                self.invoke("verify", image, cache, code=1)
                self.invoke("replay", image, cache, metadata["record_digest"], code=1)
                self.assertEqual(cache.read_bytes(), original_record)

        # Arbitrary data is part of the executable. Its full identity must
        # remain in the new record even where a known IP would be reclaimable.
        data = source[:EVOLUTION2_PROGRAM_BYTES] + bytes(0x8000)
        image, output = self.work / "non-IP.bin", self.work / "non-IP.nvr"
        image.write_bytes(data)
        retained = json.loads(self.invoke("build", image, output).stdout)
        self.assert_source_identity(retained, data, len(data))
        self.assertNotEqual(output.read_bytes(), original_record)
        self.assertEqual(retained, json.loads(self.invoke("verify", image, output).stdout))
        self.assertEqual(retained, json.loads(self.invoke("replay", image, output,
                                                        retained["record_digest"]).stdout))
        self.invoke("verify", image, cache, code=1)
        self.invoke("replay", image, cache, metadata["record_digest"], code=1)
        self.assertEqual(cache.read_bytes(), original_record)

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
        struct.pack_into("<I", wire, 16, metadata["detector_epoch"] + 1)  # A stale proof, even with a new local hash.
        wire[992:] = hashlib.sha256(wire[:992]).digest()
        cache.write_bytes(wire)
        self.invoke("inspect", cache, code=1)


if __name__ == "__main__":
    unittest.main()
