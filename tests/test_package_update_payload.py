#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise automatic delivery of exact test/release software to the updater."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from package_update_payload import add_update_payload, publish_update_payload, write_release_update
from runtime_package import envelope
import update_package as update
from publish_release import verify_update_asset
from package import write_release_assets


def module(build="012345abcdef", gui=False, engine=True):
    payload = b"\x09\0\x09\0"
    if gui:
        payload += update.CAPABILITY + update.HANDOFF_HEADER + bytes(176)
        if engine:
            payload += b"KUIENG2!" + struct.pack("<II", 2, 0xfffffffd)
    return envelope(payload, len(payload), build)


class PackageUpdatePayloadTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="kui-auto-payload-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_test_payload_includes_every_staged_reader_and_helper_exactly(self):
        files = {"KUI/apps/games/new-reader.kui": module(),
                 "KUI/apps/games/vmu-helper.bin": b"exact helper bytes\0\xff",
                 "build/reader.elf": b"source evidence", "README.md": b"instructions"}
        original = dict(files)
        report = add_update_payload(files, "reader-test.kup")
        self.assertEqual(report["format"], 2)
        self.assertEqual(report["mode"], "test")
        self.assertRegex(report["consumer"]["source_sha256"], r"^[0-9a-f]{64}$")
        members = {row["path"]: row for row in report["members"]}
        self.assertEqual(set(members), {"apps/games/new-reader.kui", "apps/games/vmu-helper.bin"})
        self.assertEqual(members["apps/games/new-reader.kui"]["policy"], update.SELF_KUI)
        self.assertEqual(members["apps/games/vmu-helper.bin"]["policy"], update.RAW_SOFTWARE)
        self.assertNotIn("runtime.kui", members)
        offset = 128 + 160 * len(members)
        self.assertEqual(files["reader-test.kup"][offset:],
                         b"".join(original["KUI/" + row["path"]] for row in report["members"]))
        for name, data in original.items():
            self.assertEqual(files[name], data)
        output = self.root / "reader-test.kup"
        published = publish_update_payload(files["reader-test.kup"], output)
        self.assertEqual(output.read_bytes(), files["reader-test.kup"])
        self.assertEqual(published["sha256"], hashlib.sha256(output.read_bytes()).hexdigest())
        self.assertEqual(published["bytes"], output.stat().st_size)

    def test_old_gui_fails_before_delivery_members_change(self):
        files = {"KUI/runtime.kui": module(gui=True, engine=False), "README.md": b"old GUI"}
        original = dict(files)
        with self.assertRaisesRegex(ValueError, "current updater engine"):
            add_update_payload(files, "test.kup")
        self.assertEqual(files, original)

    def test_unknown_or_private_kui_files_are_never_silently_inherited(self):
        for path in ("apps/games/unknown.data", "preferences.ini", "saves/slot.bin", "system/active"):
            with self.subTest(path=path):
                files = {"KUI/apps/disc/reader.kui": module(), "KUI/" + path: b"unexpected"}
                original = dict(files)
                with self.assertRaisesRegex(ValueError, "Non-software"):
                    add_update_payload(files, "test.kup")
                self.assertEqual(files, original)

    def test_ambiguous_package_and_traversal_are_refused(self):
        for extra in ({"old.kup": b"old package"}, {"KUI/../outside": b"unsafe"}):
            files = {"KUI/apps/disc/reader.kui": module(), **extra}
            with self.assertRaises(ValueError):
                add_update_payload(files, "test.kup")
            self.assertNotIn("test.kup", files)

    def test_release_covers_current_official_software_data(self):
        stage = self.root / "KUI"
        files = {"runtime.kui": module(gui=True), "apps/games/reader.kui": module("111111abcdef"),
                 "apps/games/probe.dat": b"loader probe data", "apps/music/menu.ogg": b"menu music",
                 "redump.db": b"reference database", "tosec.db": b"reference database two"}
        for name, data in files.items():
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        report = write_release_update(stage, self.root / "release.kup")
        self.assertEqual(report["mode"], "release")
        self.assertEqual(report["channel"], "stable")
        self.assertEqual({row["path"] for row in report["members"]}, set(files))
        self.assertEqual(next(row["build"] for row in report["members"]
                              if row["path"] == "apps/games/reader.kui"), "111111abcdef")
        # A public release's small update must contain precisely the software
        # in its larger install ZIP, including immutable data and mixed builds.
        installation = self.root / "release.zip"
        with zipfile.ZipFile(installation, "w") as archive:
            for name, data in files.items():
                archive.writestr("KUI/" + name, data)
            archive.writestr("README.md", b"not installed software")
        verify_update_asset(self.root / "release.kup", installation)
        # Matching members in an overlay still leave omitted old root software
        # visible, so a complete release must also carry the FULL semantics.
        update.build_package_v2(stage, self.root / "partial.kup", "stable")
        with self.assertRaisesRegex(SystemExit, "exact replacements"):
            verify_update_asset(self.root / "partial.kup", installation)
        with zipfile.ZipFile(installation, "w") as archive:
            for name, data in files.items():
                archive.writestr("KUI/" + name, data + b"changed" if name == "redump.db" else data)
        with self.assertRaisesRegex(SystemExit, "software differs"):
            verify_update_asset(self.root / "release.kup", installation)

    def test_release_asset_delivery_contains_standalone_kup_and_checksum(self):
        bundle = self.root / "release"
        bundle.mkdir()
        (bundle / "README.md").write_text("installation evidence")
        source = self.root / "source"
        source.mkdir()
        (source / "HEAD.tar.gz").write_bytes(b"source evidence")
        (self.root / "LICENSES").mkdir()
        for name in ("build.json", "LICENSE", "THIRD_PARTY.md"):
            (self.root / name).write_text("evidence")
        files = {"KUI/apps/games/reader.kui": module()}
        add_update_payload(files, "reader.kup")
        payload = self.root / "reader.kup"
        publish_update_payload(files["reader.kup"], payload)
        write_release_assets(self.root, bundle, source,
                             {"version": "1.7", "artifact_prefix": "release-test"}, payload)
        assets = self.root / "release-assets"
        self.assertEqual((assets / "release-test-update.kup").read_bytes(), files["reader.kup"])
        sums = dict(line.split("  ", 1)[::-1] for line in (assets / "SHA256SUMS.txt").read_text().splitlines())
        self.assertEqual(sums["release-test-update.kup"], hashlib.sha256(files["reader.kup"]).hexdigest())


if __name__ == "__main__":
    unittest.main()
