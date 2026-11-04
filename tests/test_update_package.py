#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the release-side package producer with adversarial envelopes."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import re
from unittest import mock
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import update_package as update
from runtime_package import envelope

BUILD = "123456abcdef"


class UpdatePackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="kui-package-test-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / "KUI"
        self.paths = update.catalog()
        for index, name in enumerate(self.paths):
            data = (update.CAPABILITY if index == 0 else bytes(16)) + struct.pack("<I", index)
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(envelope(data, len(data), BUILD))
        self.output = self.base / "update.kup"

    def package(self, channel="stable"):
        return update.build_package(self.root, self.output, BUILD, channel)

    def test_catalog_covers_every_declared_launcher_module(self):
        declared = set()
        for name in ("games_retail.h", "disc_launch.h", "games_probe.h", "games_image_probe.h"):
            header = (update.ROOT / "include/kui" / name).read_text()
            declared.update(re.findall(r'"0:/KUI/([^"\n]+\.kui)"', header))
        self.assertTrue(declared)
        self.assertFalse(declared - set(self.paths), "A launcher module is absent from the update cohort")

    def corrupted(self, mutate, manifest=False):
        self.package()
        data = bytearray(self.output.read_bytes())
        mutate(data)
        if manifest:
            size = len(self.paths) * update.ENTRY_BYTES
            data[64:96] = hashlib.sha256(data[128:128 + size]).digest()
        struct.pack_into("<I", data, 124, zlib.crc32(data[:124]))
        self.output.write_bytes(data)
        with self.assertRaises(ValueError):
            update.verify(self.output)

    def test_complete_deterministic_roundtrip_and_user_data_exclusion(self):
        user = self.root / "VMU/private.vmd"
        user.parent.mkdir()
        user.write_bytes(b"PRIVATE SAVE MUST NOT SHIP")
        info = self.package()
        self.assertEqual(info, update.verify(self.output))
        self.assertEqual([entry["path"] for entry in info["members"]], list(self.paths))
        first = self.output.read_bytes()
        self.package()
        self.assertEqual(first, self.output.read_bytes())
        self.assertNotIn(user.read_bytes(), first)
        self.assertEqual(info["file_bytes"], len(first))

    def test_testing_channel_and_invalid_arguments(self):
        self.assertEqual(self.package("testing")["channel"], "testing")
        for build, channel in (("not-a-build", "stable"), (BUILD, "nightly"), (BUILD.upper(), "stable")):
            with self.subTest(build=build, channel=channel), self.assertRaises(ValueError):
                update.build_package(self.root, self.output, build, channel)

    def test_missing_mixed_legacy_and_duplicate_capability(self):
        member = self.root / self.paths[1]
        original = member.read_bytes()
        member.unlink()
        with self.assertRaises(ValueError):
            self.package()
        member.write_bytes(envelope(bytes(20), 20, "abcdef123456"))
        with self.assertRaises(ValueError):
            self.package()
        member.write_bytes(original)
        for payload in (bytes(20), update.CAPABILITY * 2):
            (self.root / "runtime.kui").write_bytes(envelope(payload, len(payload), BUILD))
            with self.assertRaises(ValueError):
                self.package()

    def test_symlink_and_output_source_collision(self):
        member = self.root / self.paths[1]
        original = member.read_bytes()
        target = self.base / "external.kui"
        target.write_bytes(original)
        member.unlink()
        member.symlink_to(target)
        with self.assertRaises(ValueError):
            self.package()
        member.unlink()
        member.write_bytes(original)
        with self.assertRaises(ValueError):
            update.build_package(self.root, self.root / "runtime.kui", BUILD)

    def test_changed_source_never_publishes_partial_output(self):
        self.output.write_bytes(b"previous valid download")
        original = update.read_member
        calls = 0

        def changed(root, name):
            nonlocal calls
            calls += 1
            result = original(root, name)
            return envelope(bytes(20), 20, BUILD) if calls > len(self.paths) and name == self.paths[1] else result

        with mock.patch.object(update, "read_member", changed), self.assertRaises(ValueError):
            self.package()
        self.assertEqual(self.output.read_bytes(), b"previous valid download")
        self.assertEqual(list(self.base.glob(".kui-update-*")), [])

    def test_header_and_manifest_rejections(self):
        mutations = (
            lambda b: b.__setitem__(0, 0),
            lambda b: struct.pack_into("<I", b, 8, 2),
            lambda b: struct.pack_into("<I", b, 16, 65),
            lambda b: struct.pack_into("<I", b, 20, 0xFFFFFFFF),
            lambda b: struct.pack_into("<Q", b, 24, 0xFFFFFFFFFFFFFFFF),
            lambda b: b.__setitem__(44, 2),
            lambda b: b.__setitem__(45, 1),
            lambda b: b.__setitem__(96, 1),
            lambda b: b.__setitem__(64, b[64] ^ 1),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                self.corrupted(mutate)
        for bad in (b"../runtime.kui", b"VMU/save.vmd", b"apps/system/settings-a.bin"):
            with self.subTest(path=bad):
                self.corrupted(lambda b: b.__setitem__(slice(128, 208), bad + bytes(80 - len(bad))), manifest=True)
        self.corrupted(lambda b: struct.pack_into("<Q", b, 208, 0xFFFFFFFFFFFFFFFF), manifest=True)
        self.corrupted(lambda b: b.__setitem__(248, 1), manifest=True)
        self.corrupted(lambda b: b.__setitem__(216, b[216] ^ 1), manifest=True)

    def test_truncation_trailing_bytes_and_member_corruption(self):
        self.package()
        data = self.output.read_bytes()
        for size in (0, 127, 128, 128 + len(self.paths) * 128 - 1, len(data) - 1):
            with self.subTest(size=size):
                self.output.write_bytes(data[:size])
                with self.assertRaises(ValueError):
                    update.verify(self.output)
        self.output.write_bytes(data + b"unexpected")
        with self.assertRaises(ValueError):
            update.verify(self.output)
        corrupted = bytearray(data)
        corrupted[-1] ^= 1
        self.output.write_bytes(corrupted)
        with self.assertRaises(ValueError):
            update.verify(self.output)


if __name__ == "__main__":
    unittest.main()
