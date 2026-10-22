#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independently cross-check every P2 byte against C and reject stale ownership."""
import copy
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from vmu_persist_profile import (decode_profile, encode_profile, validate_ownership,
    same_ownership, collect_snapshot, verify_snapshot, MARKER_SHA, template_profile)
from vmu_sd_profile import decode_profile as decode_readonly


def sector(role, block):
    data = bytes((block * 17 + i * 3 + role * 31) & 255 for i in range(512))
    if role == 1 and block == 1:
        return bytes(512)
    if role == 2:
        return bytes(range(17, 33)) + data[16:]
    return data


def fixture(fs=2, spc=256):
    g = dict(fs_type=fs, sector_bytes=512, sectors_per_cluster=spc,
             volume_serial=0x12345678, fat_count=2 if fs == 1 else 1,
             active_fat=0, fat_first_lba=4096, fat_sectors=1024, data_first_lba=8192,
             cluster_count=3000 if spc > 256 else 100000, root_cluster=2,
             fat_mirrored=int(fs == 1), bitmap_first_cluster=3 if fs == 2 else 0,
             upcase_first_cluster=4 if fs == 2 else 0,
             bitmap_bytes=0, upcase_bytes=128 if fs == 2 else 0)
    if fs == 2:
        g["bitmap_bytes"] = (g["cluster_count"] + 7) // 8
    p = dict(transport=1, high_capacity=1, flags=1, card_sectors=249999360,
             partition_first=2048, partition_end=249999360,
             cid=bytes(range(1, 17)).hex(), image_id=bytes(range(17, 33)).hex(),
             pair_path="/KUI/vmu-test/p0001", geometry=g, files=[])
    for role, (size, sectors, cluster) in enumerate(zip((131072, 1024, 16), (256, 2, 1), (10, 1000, 2000))):
        lba = 8192 + (cluster - 2) * spc
        p["files"].append(dict(file_bytes=size, valid_bytes=size, first_cluster=cluster,
            allocation_flags=0 if fs == 1 else 3, directory_first_cluster=5,
            entry_offset=role * 96, entry_lba=8192 + 3 * spc,
            entry_count=2 if fs == 1 else 3, entry_kind=fs,
            useful=[dict(logical_block=0, lba=lba, blocks=sectors)],
            allocated=[dict(logical_block=0, lba=lba, blocks=((sectors + spc - 1) // spc) * spc)]))
    return collect_snapshot(p, sector)


def seal(data):
    data = bytearray(data);struct.pack_into("<I", data, 16, 0)
    struct.pack_into("<I", data, 16, zlib.crc32(data));return bytes(data)


class PersistProfileToolTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temp.name)
        cls.binary = cls.directory / "profile-c-test"
        subprocess.run([os.environ.get("HOST_CC", "cc"), "-std=c11", "-O0", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-Iinclude",
            "src/core/vmu_persist_profile.c", "src/core/hash.c", "src/core/retail_image.c",
            "tests/test_vmu_persist_profile.c", "-o", str(cls.binary)], cwd=ROOT, check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check_c(self, data, *, accepted=True):
        path = self.directory / "wire.bin";path.write_bytes(data)
        result = subprocess.run([str(self.binary), "--check", str(path)], capture_output=True)
        self.assertEqual(result.returncode == 0, accepted, result.stderr.decode())

    def test_complete_independent_golden_matches_every_c_byte(self):
        path = self.directory / "golden.bin"
        subprocess.run([str(self.binary), "--emit", str(path)], check=True)
        expected = encode_profile(fixture())
        self.assertEqual(path.read_bytes(), expected)
        p = decode_profile(expected)
        self.assertEqual(encode_profile(p), expected)
        self.assertEqual(p["marker_sha256"], MARKER_SHA)
        self.assertEqual(p["block_crc"][42], zlib.crc32(sector(0, 42)))
        self.assertEqual(p["base_sha256"], hashlib.sha256(b"".join(sector(0, i) for i in range(256))).hexdigest())
        self.assertEqual(p["identity_sha256"], hashlib.sha256(bytes(range(17, 33))).hexdigest())
        self.check_c(expected)

    def test_public_build_template_is_canonical_and_admitted_by_independent_c(self):
        wire = template_profile()
        self.assertEqual(len(wire), 4096)
        self.assertEqual(encode_profile(decode_profile(wire)), wire)
        self.check_c(wire)

    def test_all_wire_bytes_and_short_lengths_are_admitted_strictly(self):
        data = encode_profile(fixture())
        for offset in range(4096):
            changed = bytearray(data);changed[offset] ^= 1
            with self.assertRaises(ValueError):
                decode_profile(changed)
        for count in range(4096):
            with self.assertRaises(ValueError):
                decode_profile(data[:count])
        with self.assertRaises(ValueError):
            decode_profile(data + b"\0")
        self.check_c(data[:-1], accepted=False)
        self.check_c(data + b"\0", accepted=False)

    def test_reserved_and_unused_fields_rejected_even_with_recomputed_wire_crc(self):
        data = encode_profile(fixture())
        offsets = list(range(188, 192)) + list(range(211, 288)) + list(range(348, 352))
        offsets += list(range(376, 384)) + list(range(416, 512)) + list(range(2888, 4096))
        for role in range(3):
            base = 512 + 448 * role
            offsets += list(range(base + 56, base + 64))
            offsets += [base + start + index * 12 + field
                        for start in (64, 256) for index in range(1, 16) for field in range(12)]
        for offset in offsets:
            changed = bytearray(data);changed[offset] = 1
            with self.assertRaises(ValueError, msg=str(offset)):
                decode_profile(seal(changed))
        for offset in (188, 348, 376, 416, 4095, 512 + 56, 960 + 76, 1408 + 268):
            changed = bytearray(data);changed[offset] = 1
            self.check_c(seal(changed), accepted=False)

    def test_geometry_identity_and_file_semantics_are_cross_checked_by_c(self):
        mutations = [(20, "I", 2), (24, "I", 2), (28, "I", 0),
            (32, "Q", (1 << 32) + 1), (48, "Q", 2048), (184, "I", 18),
            (288, "I", 3), (292, "I", 4096), (296, "I", 3), (304, "I", 3),
            (308, "I", 1), (312, "Q", 2048), (320, "Q", 1), (328, "Q", 4096),
            (336, "I", 0xffffffff), (340, "I", 1), (344, "I", 1),
            (352, "I", 2), (360, "Q", 1), (368, "Q", 131074),
            (512, "Q", 131071), (512 + 8, "I", 1), (512 + 12, "I", 2),
            (512 + 20, "I", 1), (512 + 32, "I", 2), (512 + 36, "I", 1),
            (512 + 40, "I", 17), (960 + 44, "I", 3), (1408 + 44, "I", 2),
            (512 + 48, "Q", 131071), (512 + 64, "I", 1),
            (512 + 68, "I", 10241), (512 + 256 + 8, "I", 255), (2880, "Q", 1)]
        data = encode_profile(fixture())
        for at, fmt, value in mutations:
            changed = bytearray(data);struct.pack_into("<" + fmt, changed, at, value);changed = seal(changed)
            with self.subTest(at=at), self.assertRaises(ValueError):
                decode_profile(changed)
            self.check_c(changed, accepted=False)

    def test_full_slack_overlap_and_useful_prefix_are_ownership_errors(self):
        p = fixture()
        # Their useful sectors remain disjoint while the full128KiB journal
        # cluster intersects the base's allocation: ownership still refuses.
        j = p["files"][1]
        j["first_cluster"] = p["files"][0]["first_cluster"]
        j["allocated"][0]["lba"] = p["files"][0]["allocated"][0]["lba"]
        j["useful"][0]["lba"] = j["allocated"][0]["lba"] + 128
        with self.assertRaises(ValueError):
            validate_ownership(p)
        p = fixture();p["files"][2]["allocated"][0]["blocks"] = 1
        with self.assertRaises(ValueError):
            validate_ownership(p)
        p = fixture();p["files"][0]["allocated"][0]["lba"] += 1
        with self.assertRaises(ValueError):
            validate_ownership(p)

    def test_fragmented_maps_are_canonical_coalesced_and_exact(self):
        p = fixture(spc=1)
        f = p["files"][0]
        f["allocated"] = [dict(logical_block=i * 16, lba=8200 + i * 32, blocks=16) for i in range(16)]
        f["useful"] = copy.deepcopy(f["allocated"])
        self.check_c(encode_profile(p))
        for mutation in (lambda q: q["files"][0]["allocated"][1].update(lba=8216),
                         lambda q: q["files"][0]["useful"][1].update(lba=8200),
                         lambda q: q["files"][0]["allocated"][1].update(logical_block=0)):
            changed = copy.deepcopy(p);mutation(changed)
            with self.assertRaises(ValueError):
                encode_profile(changed)

    def test_fat32_exfat_cluster_sizes_and_addressing_boundaries(self):
        for fs, spc in ((1, 1), (1, 128), (2, 1), (2, 256), (2, 65536)):
            p = fixture(fs, spc);self.check_c(encode_profile(p))
            self.assertTrue(same_ownership(p, decode_profile(encode_profile(p))))
        p = fixture();p["high_capacity"] = 0
        with self.assertRaises(ValueError):
            validate_ownership(p)
        p = fixture();p["card_sectors"] = 1 << 32;p["partition_end"] = 1 << 32
        self.check_c(encode_profile(p))

    def test_ownership_equality_ignores_only_content_provenance(self):
        p = fixture();changed = copy.deepcopy(p)
        changed.update(base_sha256="00" * 32, marker_sha256="00" * 32,
                       block_crc=[0] * 256, journal_sequence=99)
        self.assertTrue(same_ownership(p, changed))
        for mutation in (lambda q: q["files"][0].update(entry_offset=32),
                         lambda q: q["geometry"].update(volume_serial=123),
                         lambda q: q.update(pair_path="/KUI/vmu-test/p0002")):
            changed = copy.deepcopy(p);mutation(changed)
            self.assertFalse(same_ownership(p, changed))

    def test_snapshot_verification_requires_clean_marker_and_owned_identity(self):
        p = fixture();verify_snapshot(p, sector)
        original = copy.deepcopy(p)
        for target in ((0, 42), (1, 0), (1, 1), (2, 0)):
            def corrupt(role, block):
                data = bytearray(sector(role, block))
                if (role, block) == target:
                    data[0] ^= 1
                return bytes(data)
            with self.assertRaises(ValueError):
                verify_snapshot(p, corrupt)
            self.assertEqual(p, original)
        # Identity-file padding after its16 data bytes is not file content.
        verify_snapshot(p, lambda r, b: sector(r, b) if r != 2 else sector(r, b)[:16] + bytes(496))

    def test_r1_and_p2_are_separate_noninterchangeable_formats(self):
        wire = encode_profile(fixture())
        with self.assertRaises(ValueError):
            decode_readonly(wire)
        with self.assertRaises(ValueError):
            decode_profile(b"KUIVMUR1" + bytes(1528))


if __name__ == "__main__":
    unittest.main()
