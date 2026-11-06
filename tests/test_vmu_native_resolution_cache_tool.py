# SPDX-License-Identifier: GPL-3.0-only
"""Host CLI integration; optional KUI_NATIVE_* variables supply original images."""
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
        time_stalkers = os.environ.get("KUI_NATIVE_TIME_STALKERS")
        cls.time_stalkers = Path(time_stalkers).resolve() if time_stalkers else None

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
        self.assertEqual(DETECTOR_EPOCH, 8)
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

    def test_current_fixtures_inspect_and_resealed_old_epochs_remain_refused(self):
        # All nine fixtures were regenerated by the C builder from exact
        # original sources. Codec acceptance does not upgrade any old proof.
        for slug in ('ps2','skies','sa2','grandia2','evolution1','rayman2','pso','evolution2','lodoss'):
            with self.subTest(slug=slug):
                fixture=ROOT/'tests/fixtures/native-cache'/f'{slug}.nvr'
                original=fixture.read_bytes()
                self.assertEqual(struct.unpack_from('<I',original,16)[0],DETECTOR_EPOCH)
                c=json.loads(self.invoke('inspect',fixture).stdout)
                self.assertEqual(record_metadata(original)['record_digest'],c['record_digest'])
                for epoch in (6,7):
                    wire=bytearray(original);struct.pack_into('<I',wire,16,epoch)
                    # A valid local integrity digest cannot freshen an old epoch.
                    wire[992:]=hashlib.sha256(wire[:992]).digest()
                    stale=self.work/f'{slug} epoch{epoch}.nvr';stale.write_bytes(wire)
                    self.invoke('inspect',stale,code=1)
                    with self.assertRaisesRegex(ValueError,'version/epoch/schema'):
                        record_metadata(wire,wire[992:])
                    self.assertEqual(stale.read_bytes(),wire)
                self.assertEqual(fixture.read_bytes(),original)

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

    def test_direct_allocator_and_early_exit_schema_match_c_codec(self):
        # Schema-only authored metadata, not a certificate for a real image.
        original = bytearray(1024)
        original[:8] = b'KUINVR01'
        load, image_bytes = 0x8c010000, 4096
        image_digest = hashlib.sha256(b'direct allocator codec fixture').digest()
        original[32:64] = original[136:168] = image_digest
        initializer, sentinel = load+0x100, load+0x1000
        fields = {8:1, 12:1024, 16:DETECTOR_EPOCH, 20:1, 24:image_bytes, 28:load,
                  64:1, 68:104, 72:2, 76:image_bytes, 80:load, 84:7, 132:12,
                  168:1, 172:440, 192:1, 404:12, 608:1, 612:72, 616:3, 620:2,
                  624:image_bytes, 628:load, 632:initializer, 636:load+0x40,
                  640:initializer, 644:initializer+0xae, 648:initializer+0x160,
                  652:0, 656:0, 660:sentinel, 664:sentinel+0x20,
                  668:sentinel+0x24, 672:sentinel+0x28, 676:sentinel+0x2c,
                  700:2, 704:52, 708:7, 712:load, 716:image_bytes,
                  720:2, 724:load+0x400, 728:load+0x408,
                  732:0, 736:0, 744:0}
        for offset, value in fields.items():
            struct.pack_into('<I', original, offset, value)
        # These schema-only mutations intentionally have no fresh image proof.
        for offset, value, accepted in (
                (652,0,True), (652,sentinel-4,False), (656,load+0x80,False),
                (620,1,False), (644,initializer+0xe2,False),
                (648,initializer+0x1b0,False), (664,sentinel+0x24,False),
                (636,load+0x41,False), (640,initializer+4,False),
                (728,load+0x40c,False), (732,load+0x500,False)):
            with self.subTest(offset=offset,value=value):
                wire = bytearray(original)
                struct.pack_into('<I',wire,offset,value)
                wire[992:] = hashlib.sha256(wire[:992]).digest()
                changed = self.work/'direct schema.nvr'
                changed.write_bytes(wire)
                self.invoke('inspect',changed,code=0 if accepted else 1)
                if accepted:
                    self.assertEqual(record_metadata(wire)['detector_epoch'],DETECTOR_EPOCH)
                else:
                    with self.assertRaises(ValueError):record_metadata(wire)

        # The new early/compact family keeps the same wire size, but binds
        # both the eight-byte pair and its independently proved loader thunk.
        early_compact = bytearray(original)
        for offset,value in ((708,8),(720,3),(732,load+0x500),(744,load+0x54c)):
            struct.pack_into('<I',early_compact,offset,value)
        for offset,value,accepted in ((708,8,True),(708,7,False),(708,9,False),
                (720,2,False),(720,4,False),(724,0,False),(728,load+0x40c,False),
                (732,0,False),(736,load+0x600,False),(744,0,False),(744,load+0x54e,False)):
            with self.subTest(early_compact_offset=offset,value=value):
                wire=bytearray(early_compact);struct.pack_into('<I',wire,offset,value)
                wire[992:]=hashlib.sha256(wire[:992]).digest()
                changed=self.work/'early compact schema.nvr';changed.write_bytes(wire)
                self.invoke('inspect',changed,code=0 if accepted else 1)
                if accepted:
                    self.assertEqual(record_metadata(wire)['detector_epoch'],DETECTOR_EPOCH)
                else:
                    with self.assertRaises(ValueError):record_metadata(wire)

        # Firmware checks retain a third terminal entry with no executable
        # thunk. Only the early variant requires the adjacent eight-byte head.
        for family in (9,10):
            firmware=bytearray(original)
            for offset,value in ((708,family),(720,3),(728,load+(0x408 if family==10 else 0x500)),
                                 (732,load+0x600),(736,0),(744,0)):
                struct.pack_into('<I',firmware,offset,value)
            cases=((708,family,True),(708,0,False),(708,11,False),(708,0xffffffff,False),
                   (700,1,False),(704,48,False),(712,load+4,False),(716,image_bytes-4,False),
                   (720,2,False),(720,4,False),(732,0,False),(736,load+0x700,False),
                   (744,load+0x64c,False),(724,0,family==9),(728,load+0x40a,family==9))
            for offset,value,accepted in cases:
                with self.subTest(firmware_family=family,offset=offset,value=value):
                    wire=bytearray(firmware);struct.pack_into('<I',wire,offset,value)
                    wire[992:]=hashlib.sha256(wire[:992]).digest()
                    changed=self.work/f'firmware{family} schema.nvr';changed.write_bytes(wire)
                    result=self.invoke('inspect',changed,code=0 if accepted else 1)
                    if accepted:
                        python=record_metadata(wire);c=json.loads(result.stdout)
                        self.assertEqual(python['record_digest'],c['record_digest'])
                        self.assertEqual(python['detector_epoch'],c['detector_epoch'])
                        self.assertEqual(c['exit_family'],family)
                    else:
                        with self.assertRaises(ValueError):record_metadata(wire)
            # Schema validity remains distinct from original source proof.
            # Relabeling a current record cannot authorize a loader thunk.
            malformed=bytearray(firmware)
            struct.pack_into('<I',malformed,744,load+0x64c)
            malformed[992:]=hashlib.sha256(malformed[:992]).digest()
            with self.assertRaisesRegex(ValueError,'exit schema'):record_metadata(malformed)

        # The separately tagged P2-lock early driver retains requirements7
        # and the wire ABI, but cannot lack the public initializer/call order.
        early_p2=bytearray(original)
        for offset,value in ((72,3),(680,load+0x80-12),(684,load+0x80),(688,load+0x84),(184,load+0x80+26)):
            struct.pack_into('<I',early_p2,offset,value)
        for offset,value,accepted in ((72,3,True),(72,4,False),(84,15,False),
                (636,0,False),(680,0,False),(684,load-2,False),(684,load+image_bytes-28,False),
                (684,load+0x82,False),(688,load+0x82,False),(184,load+0x80+24,False)):
            with self.subTest(early_p2_offset=offset,value=value):
                wire=bytearray(early_p2);struct.pack_into('<I',wire,offset,value)
                wire[992:]=hashlib.sha256(wire[:992]).digest()
                changed=self.work/'early P2 schema.nvr';changed.write_bytes(wire)
                self.invoke('inspect',changed,code=0 if accepted else 1)
                if accepted:
                    self.assertEqual(record_metadata(wire)['detector_epoch'],DETECTOR_EPOCH)
                else:
                    with self.assertRaises(ValueError):record_metadata(wire)
        # A structurally valid older fixed-plan record must not acquire the
        # new tag without its required immutable initializer slot.
        for driver,accepted in ((2,True),(3,False)):
            wire=bytearray(early_p2)
            for offset,value in ((72,driver),(20,2),(616,1),(620,1),
                                 (636,0),(640,0),(696,0x100)):
                struct.pack_into('<I',wire,offset,value)
            wire[992:]=hashlib.sha256(wire[:992]).digest()
            changed=self.work/'early P2 fixed-plan refusal.nvr';changed.write_bytes(wire)
            self.invoke('inspect',changed,code=0 if accepted else 1)
            if accepted:record_metadata(wire)
            else:
                with self.assertRaises(ValueError):record_metadata(wire)

        # Other C proof kinds retain their own canonical shapes. A shared
        # codec must never relabel the direct record as either arena kind.
        for arena in (False,True):
            wire = bytearray(original)
            values = {20:3 if arena else 2, 616:2 if arena else 1, 620:0 if arena else 1,
                      636:load+0x40 if arena else 0, 640:0,
                      684:load+0x600, 688:load+0x604, 696:0x100}
            if arena:
                values.update({offset:0 for offset in (632,644,648,652,656,660,664,668,672,676)})
            for offset,value in values.items():struct.pack_into('<I',wire,offset,value)
            wire[992:] = hashlib.sha256(wire[:992]).digest()
            changed = self.work/f'kind {3 if arena else 2}.nvr'
            changed.write_bytes(wire)
            self.invoke('inspect',changed)
            self.assertEqual(record_metadata(wire)['detector_epoch'],DETECTOR_EPOCH)
            struct.pack_into('<I',wire,20,1)
            wire[992:] = hashlib.sha256(wire[:992]).digest()
            changed.write_bytes(wire)
            self.invoke('inspect',changed,code=1)
            with self.assertRaisesRegex(ValueError,'allocator schema'):record_metadata(wire)

    def test_time_stalkers_direct_allocator_fresh_build_verify_and_replay(self):
        if self.time_stalkers is None:
            self.skipTest('Set KUI_NATIVE_TIME_STALKERS for original direct-allocator proof coverage')
        cache = self.work/'time stalkers.nvr'
        metadata = json.loads(self.invoke('build',self.time_stalkers,cache).stdout)
        wire = cache.read_bytes()
        self.assertEqual(tuple(struct.unpack_from('<I',wire,offset)[0] for offset in
                               (20,72,616,620,652,656,708,720)),(1,2,3,2,0,0,7,2))
        self.assertEqual(record_metadata(wire)['record_digest'],metadata['record_digest'])
        self.assert_source_identity(metadata,self.time_stalkers.read_bytes(),metadata['image_bytes'])
        self.assertEqual(metadata,json.loads(self.invoke('verify',self.time_stalkers,cache).stdout))
        self.assertEqual(metadata,json.loads(self.invoke('replay',self.time_stalkers,cache,
                                                        metadata['record_digest']).stdout))
        # Local integrity may not restore absent dispatch roles to this family.
        forged = bytearray(wire)
        struct.pack_into('<I',forged,652,struct.unpack_from('<I',wire,660)[0]-4)
        forged[992:] = hashlib.sha256(forged[:992]).digest()
        invalid = self.work/'invented dispatch.nvr'
        invalid.write_bytes(forged)
        self.invoke('inspect',invalid,code=1)
        self.invoke('verify',self.time_stalkers,invalid,code=1)
        self.invoke('replay',self.time_stalkers,invalid,forged[992:].hex(),code=1)

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
