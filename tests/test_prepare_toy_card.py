#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic offline Toy Commander preparation and journal admission tests."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

from test_vmu_image_subset_tool import directory_offset, fixture, oracle

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from vmu_persist_profile import decode_profile, template_profile  # noqa: E402

SPEC = importlib.util.spec_from_file_location("prepare_toy_card", ROOT / "tools/prepare-toy-card.py")
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)
HOST = Path(os.environ.get("KUI_VMU_SUBSET_HOST", ROOT / "build/vmu-subset-tool/vmu-image-subset")).resolve()


def map_crc(profile):
    """Independent serialization of the production redo-store map binding."""
    image, journal = (profile["files"][role]["useful"] for role in (0, 1))
    wire = bytes.fromhex(profile["image_id"])
    wire += struct.pack("<QII", profile["card_sectors"], len(image), len(journal))
    for extent in image + journal:
        wire += struct.pack("<QI", extent["lba"], extent["blocks"])
    return zlib.crc32(wire)


def committed_journal(image, profile, block=199):
    payload = image[block * 512:(block + 1) * 512]
    marker = bytearray(512)
    marker[:8] = b"KUIVMUJ1"
    struct.pack_into("<II", marker, 8, 1, 512)
    marker[16:32] = bytes.fromhex(profile["image_id"])
    struct.pack_into("<QIIIIQ", marker, 32, profile["card_sectors"],
                     map_crc(profile), block, zlib.crc32(payload), 1, 7)
    struct.pack_into("<I", marker, 508, zlib.crc32(marker[:508]))
    return payload + bytes(marker)


def toy_fixture():
    source, records = fixture()
    source = bytearray(source)
    for slot, name in ((0, b"TOY_COMM.___"), (16, b"TOY_COMM.AAA"), (15, b"TOY_COMM.P04")):
        offset = directory_offset(slot)
        source[offset + 4:offset + 16] = name
        records[slot] = (name, *records[slot][1:])
    return bytes(source), records


class JournalTests(unittest.TestCase):
    def setUp(self):
        self.profile = decode_profile(template_profile())
        self.identity = bytes.fromhex(self.profile["image_id"])
        self.image, _ = toy_fixture()

    def state(self, journal, image=None, identity=None, profile=None):
        return TOOL.journal_state(self.image if image is None else image, journal,
                                  self.identity if identity is None else identity,
                                  self.profile if profile is None else profile)

    def test_zero_marker_allows_stale_payload(self):
        self.assertEqual(self.state(bytes(1024)), "clear")
        self.assertEqual(self.state(b"\xa7" * 512 + bytes(512)), "clear")

    def test_committed_record_already_applied(self):
        journal = committed_journal(self.image, self.profile)
        self.assertEqual(self.state(journal), "committed_already_applied")

    def test_pending_recovery_is_refused_without_replay(self):
        journal = committed_journal(self.image, self.profile)
        changed = bytearray(self.image)
        changed[199 * 512 + 17] ^= 1
        before = bytes(changed)
        with self.assertRaisesRegex(ValueError, "(?i)(pending|recover)"):
            self.state(journal, image=changed)
        self.assertEqual(bytes(changed), before)

    def test_map_crc_matches_independent_wire_and_binds_useful_maps(self):
        self.assertEqual(TOOL.mapping_crc(self.profile), map_crc(self.profile))
        changed = copy.deepcopy(self.profile)
        changed["files"][0]["useful"] = [
            dict(logical_block=0, lba=9000, blocks=128),
            dict(logical_block=128, lba=10000, blocks=128)]
        self.assertEqual(TOOL.mapping_crc(changed), map_crc(changed))
        self.assertNotEqual(map_crc(changed), map_crc(self.profile))
        with self.assertRaises(ValueError):
            self.state(committed_journal(self.image, self.profile), profile=changed)

    def test_malformed_committed_records_are_refused(self):
        original = committed_journal(self.image, self.profile)
        mutations = {
            "magic": (0, b"X"),
            "version": (8, struct.pack("<I", 2)),
            "sector-size": (12, struct.pack("<I", 1024)),
            "identity": (16, b"\x00"),
            "capacity": (32, struct.pack("<Q", self.profile["card_sectors"] + 1)),
            "map-crc": (40, struct.pack("<I", map_crc(self.profile) ^ 1)),
            "block-range": (44, struct.pack("<I", 256)),
            "payload-crc": (48, struct.pack("<I", 0)),
            "commit-flag": (52, struct.pack("<I", 0)),
            "reserved-start": (64, b"\x01"),
            "reserved-end": (507, b"\x01"),
        }
        for name, (offset, value) in mutations.items():
            with self.subTest(name=name):
                bad = bytearray(original)
                bad[512 + offset:512 + offset + len(value)] = value
                struct.pack_into("<I", bad, 1020, zlib.crc32(bad[512:1020]))
                with self.assertRaises(ValueError):
                    self.state(bytes(bad))

    def test_bad_payload_header_checksum_and_lengths_are_refused(self):
        original = committed_journal(self.image, self.profile)
        for offset in (0, 1023):
            with self.subTest(offset=offset):
                bad = bytearray(original)
                bad[offset] ^= 1
                with self.assertRaises(ValueError):
                    self.state(bytes(bad))
        for length in (0, 512, 1023, 1025):
            with self.subTest(length=length):
                with self.assertRaises(ValueError):
                    self.state((original + b"x")[:length])

    def test_pair_identity_must_match_profile_even_with_clear_journal(self):
        for identity in (bytes(16), self.identity[:-1], self.identity + b"x"):
            with self.subTest(identity=identity):
                with self.assertRaises(ValueError):
                    self.state(bytes(1024), identity=identity)


class PrepareToyCardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not HOST.is_file():
            raise RuntimeError("Build the host adapter with make -f Makefile.vmu-subset-tool first.")

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="kui-prepare-toy-test-")
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.pair = self.base / "p0001"
        self.pair.mkdir()
        self.output = self.base / "fresh output"
        self.image, self.records = toy_fixture()
        self.profile = decode_profile(template_profile())
        self.original = {
            "card.vmd": self.image,
            "card.journal": bytes(1024),
            "identity.bin": bytes.fromhex(self.profile["image_id"]),
            "retail-persistent-profile-0001.bin": template_profile(),
        }
        for name, data in self.original.items():
            (self.pair / name).write_bytes(data)

    def unchanged(self):
        self.assertEqual({path.name: path.read_bytes() for path in self.pair.iterdir()}, self.original)

    def prepare(self):
        return TOOL.prepare(self.pair, self.output, host=HOST)

    def test_prepares_all_toy_names_and_preserves_source(self):
        self.prepare()
        image = self.output / "KUI/vmu-import/card.vmd"
        self.assertEqual(image.read_bytes(), oracle(self.image, self.records, [0, 15, 16]))
        report = json.loads((self.output / "report.json").read_text())
        self.assertEqual(report["output_summary"],
                         {"files": 3, "game_files": 0, "used_blocks": 6, "free_blocks": 194})
        self.assertEqual({item["name_hex"] for item in report["selection"]},
                         {self.records[slot][0].hex() for slot in (0, 15, 16)})
        self.unchanged()

    def test_already_applied_journal_is_accepted(self):
        journal = committed_journal(self.image, self.profile)
        self.original["card.journal"] = journal
        (self.pair / "card.journal").write_bytes(journal)
        self.prepare()
        self.assertEqual((self.output / "KUI/vmu-import/card.vmd").read_bytes(),
                         oracle(self.image, self.records, [0, 15, 16]))
        self.unchanged()

    def test_pending_recovery_leaves_no_output_or_source_changes(self):
        applied = bytearray(self.image)
        applied[199 * 512] ^= 1
        journal = committed_journal(bytes(applied), self.profile)
        self.original["card.journal"] = journal
        (self.pair / "card.journal").write_bytes(journal)
        with self.assertRaisesRegex(ValueError, "(?i)(pending|recover)"):
            self.prepare()
        self.assertFalse(self.output.exists())
        self.unchanged()

    def test_existing_output_is_refused(self):
        self.output.mkdir()
        sentinel = self.output / "keep.txt"
        sentinel.write_bytes(b"keep existing output")
        with self.assertRaises((ValueError, FileExistsError)):
            self.prepare()
        self.assertEqual(list(self.output.iterdir()), [sentinel])
        self.assertEqual(sentinel.read_bytes(), b"keep existing output")
        self.unchanged()

    def test_missing_options_or_profile_save_is_refused(self):
        for slots in ((0,), (15, 16)):
            with self.subTest(slots=slots):
                changed = bytearray(self.image)
                for slot in slots:
                    offset = directory_offset(slot)
                    changed[offset + 4:offset + 16] = b"OTHER_SAVE__"
                (self.pair / "card.vmd").write_bytes(changed)
                with self.assertRaises(ValueError):
                    self.prepare()
                self.assertFalse(self.output.exists())

    def test_pair_identity_and_profile_presence_are_required(self):
        identity = self.pair / "identity.bin"
        identity.write_bytes(bytes(16))
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.output.exists())
        identity.write_bytes(self.original["identity.bin"])
        profile = self.pair / "retail-persistent-profile-0001.bin"
        profile.unlink()
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.output.exists())
        profile.write_bytes(self.original[profile.name])
        (self.pair / "retail-persistent-profile-0002.bin").write_bytes(self.original[profile.name])
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
