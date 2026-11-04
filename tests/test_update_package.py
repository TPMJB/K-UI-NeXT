#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the release-side package producer with adversarial envelopes."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
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
            data = (update.CAPABILITY + update.HANDOFF_HEADER + bytes(176) if index == 0 else bytes(16)) + struct.pack("<I", index)
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(envelope(data, len(data), BUILD))
        self.output = self.base / "update.kup"

    def package(self, channel="stable"):
        return update.build_package(self.root, self.output, BUILD, channel)

    def gui_only(self):
        shutil.rmtree(self.root / "apps")

    def inherited_package(self):
        return update.build_package(self.root, self.output, BUILD, "testing", inherit_missing=True)

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

    def test_withdrawn_gui_builds_cannot_be_produced_or_verified(self):
        self.package()
        original = self.output.read_bytes()
        payload = update.CAPABILITY + update.HANDOFF_HEADER + bytes(176)
        for build in ("28cadccf0e71", "33b04057db86"):
            with self.subTest(build=build):
                with self.assertRaisesRegex(ValueError, "withdrawn"):
                    update.build_package(self.root, self.base / "withdrawn.kup", build, inherit_missing=True)
                with self.assertRaisesRegex(ValueError, "withdrawn"):
                    update.member_info("runtime.kui", envelope(payload, len(payload), build), build)
                wire = bytearray(original)
                wire[32:44] = build.encode()
                struct.pack_into("<I", wire, 124, zlib.crc32(wire[:124]))
                self.output.write_bytes(wire)
                with self.assertRaisesRegex(ValueError, "withdrawn"):
                    update.verify(self.output)
        self.assertFalse((self.base / "withdrawn.kup").exists())

    def test_only_explicit_exact_legacy_pins_accept_an_older_build(self):
        path = "apps/games/cdda-toy-raw.kui"
        data = envelope(bytes(20), 20, "abcdef123456")
        pin = {"path": path, "build": "abcdef123456", "bytes": len(data),
               "sha256": hashlib.sha256(data).hexdigest()}
        with mock.patch.object(update, "legacy_catalog", return_value={path: pin}):
            self.assertEqual(update.member_info(path, data, BUILD)["policy"], 1)
            with self.assertRaises(ValueError):
                update.member_info("apps/games/probe.kui", data, BUILD)
            for key, value in (("build", BUILD), ("bytes", len(data) + 4), ("sha256", "00" * 32)):
                changed = {**pin, key: value}
                with self.subTest(key=key), mock.patch.object(update, "legacy_catalog", return_value={path: changed}):
                    with self.assertRaises(ValueError):
                        update.member_info(path, data, BUILD)
        self.assertEqual(len(update.legacy_catalog()), 3)

    def test_ram_handoff_must_be_unique_and_empty_on_disk(self):
        for handoff in (b"", update.HANDOFF_HEADER + b"\1" + bytes(175),
                        (update.HANDOFF_HEADER + bytes(176)) * 2,
                        update.HANDOFF_HEADER + bytes(172)):
            payload = update.CAPABILITY + handoff
            (self.root / "runtime.kui").write_bytes(envelope(payload, len(payload), BUILD))
            with self.subTest(bytes=len(handoff)), self.assertRaises(ValueError):
                self.package()

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

    def test_gui_only_inheritance_is_explicit_and_has_no_module_payload(self):
        self.gui_only()
        with self.assertRaises(ValueError):
            self.package()
        with mock.patch.object(update, "read_member", wraps=update.read_member) as reads:
            info = self.inherited_package()
        self.assertEqual([call.args[1] for call in reads.call_args_list], ["runtime.kui"] * 2)
        self.assertEqual(info, update.verify(self.output))
        self.assertEqual([entry["path"] for entry in info["members"]], list(self.paths))
        self.assertEqual(info["members"][0]["policy"], 0)
        self.assertEqual(info["members"][1:], [
            {"path": path, "bytes": 0, "sha256": "00" * 32, "policy": 2} for path in self.paths[1:]])
        runtime = (self.root / "runtime.kui").read_bytes()
        wire = self.output.read_bytes()
        self.assertEqual(info["payload_bytes"], len(runtime))
        self.assertEqual(wire[128 + 128 * len(self.paths):], runtime)
        self.assertEqual(info["file_bytes"], len(wire))
        first = wire
        self.inherited_package()
        self.assertEqual(self.output.read_bytes(), first)

    def test_sparse_package_keeps_valid_provided_modules_and_exact_legacy_policy(self):
        current = self.paths[1]
        current_data = (self.root / current).read_bytes()
        self.gui_only()
        (self.root / current).parent.mkdir(parents=True)
        (self.root / current).write_bytes(current_data)
        legacy = "apps/games/cdda-toy-raw.kui"
        older = envelope(bytes(20), 20, "abcdef123456")
        (self.root / legacy).write_bytes(older)
        pin = {"path": legacy, "build": "abcdef123456", "bytes": len(older),
               "sha256": hashlib.sha256(older).hexdigest()}
        with mock.patch.object(update, "legacy_catalog", return_value={legacy: pin}):
            info = self.inherited_package()
            self.assertEqual(info, update.verify(self.output))
        rows = {row["path"]: row for row in info["members"]}
        self.assertEqual(rows[current]["policy"], 0)
        self.assertEqual(rows[current]["sha256"], hashlib.sha256(current_data).hexdigest())
        self.assertEqual(rows[legacy]["policy"], 1)
        self.assertEqual(rows[legacy]["sha256"], pin["sha256"])
        self.assertEqual(sum(row["policy"] == 2 for row in rows.values()), len(self.paths) - 3)
        self.assertEqual(info["payload_bytes"], (self.root / "runtime.kui").stat().st_size + len(current_data) + len(older))

    def test_inheritance_never_replaces_missing_or_invalid_gui(self):
        self.gui_only()
        gui = self.root / "runtime.kui"
        gui.unlink()
        with self.assertRaises(ValueError):
            self.inherited_package()
        gui.write_bytes(envelope(bytes(20), 20, BUILD))
        with self.assertRaises(ValueError):
            self.inherited_package()

    def test_inheritance_refuses_existing_invalid_files_directories_and_symlinks(self):
        self.gui_only()
        member = self.root / self.paths[1]
        member.parent.mkdir(parents=True)
        self.output.write_bytes(b"previous download")
        for data in (b"", bytes(64), b"bad envelope" * 10, bytes(update.MAX_MEMBER + 1),
                     envelope(bytes(20), 20, "abcdef123456")):
            with self.subTest(bytes=len(data)):
                member.write_bytes(data)
                with self.assertRaises(ValueError):
                    self.inherited_package()
                member.unlink()
        member.mkdir()
        with self.assertRaises(ValueError):
            self.inherited_package()
        member.rmdir()
        external = self.base / "external.kui"
        for present in (False, True):
            if present:
                external.write_bytes(envelope(bytes(20), 20, BUILD))
            member.symlink_to(external)
            with self.subTest(symlink_target_exists=present), self.assertRaises(ValueError):
                self.inherited_package()
            member.unlink()
        self.assertEqual(self.output.read_bytes(), b"previous download")
        self.assertEqual(list(self.base.glob(".kui-update-*")), [])

    def test_inheritance_refuses_non_directory_and_symlink_ancestors(self):
        self.gui_only()
        apps = self.root / "apps"
        apps.write_bytes(b"not a directory")
        with self.assertRaises(ValueError):
            self.inherited_package()
        apps.unlink()
        target = self.base / "external-apps"
        target.mkdir()
        apps.symlink_to(target, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.inherited_package()
        self.assertEqual(list(target.iterdir()), [])

    def test_appearing_inherited_source_never_publishes_partial_output(self):
        self.gui_only()
        self.output.write_bytes(b"previous download")
        original = update.read_member
        calls = 0

        def changed(root, name):
            nonlocal calls
            calls += 1
            result = original(root, name)
            if calls == 2:
                member = root / self.paths[1]
                member.parent.mkdir(parents=True)
                member.write_bytes(envelope(bytes(20), 20, BUILD))
            return result

        with mock.patch.object(update, "read_member", changed), self.assertRaises(ValueError):
            self.inherited_package()
        self.assertEqual(self.output.read_bytes(), b"previous download")
        self.assertEqual(list(self.base.glob(".kui-update-*")), [])

    def test_malformed_inheritance_records_are_rejected(self):
        self.gui_only()
        self.inherited_package()
        original = self.output.read_bytes()
        entry = 128 + 128
        payload = 128 + 128 * len(self.paths)
        for fault in ("runtime", "size", "hash", "empty_hash", "reserved", "unknown_policy", "path", "policy0", "policy1", "extra_payload"):
            with self.subTest(fault=fault):
                wire = bytearray(original)
                if fault == "runtime":
                    struct.pack_into("<Q", wire, 128 + 80, 0)
                    wire[128 + 88:128 + 120] = bytes(32)
                    wire[128 + 120] = 2
                    del wire[payload:]
                elif fault == "size":
                    struct.pack_into("<Q", wire, entry + 80, 4)
                    wire.extend(bytes(4))
                elif fault == "hash":
                    wire[entry + 88] = 1
                elif fault == "empty_hash":
                    wire[entry + 88:entry + 120] = hashlib.sha256(b"").digest()
                elif fault == "reserved":
                    wire[entry + 121] = 1
                elif fault == "unknown_policy":
                    wire[entry + 120] = 3
                elif fault == "path":
                    name = b"../runtime.kui"
                    wire[entry:entry + 80] = name + bytes(80 - len(name))
                elif fault in ("policy0", "policy1"):
                    wire[entry + 120] = int(fault[-1])
                else:
                    wire.extend(bytes(4))
                struct.pack_into("<Q", wire, 24, len(wire) - payload)
                wire[64:96] = hashlib.sha256(wire[128:payload]).digest()
                struct.pack_into("<I", wire, 124, zlib.crc32(wire[:124]))
                self.output.write_bytes(wire)
                with self.assertRaises(ValueError):
                    update.verify(self.output)

    def test_cli_requires_explicit_inheritance_opt_in(self):
        self.gui_only()
        command = [sys.executable, str(update.ROOT / "tools/update_package.py"), "build", str(self.root),
                   str(self.output), "--build-id", BUILD]
        strict = subprocess.run(command, text=True, capture_output=True)
        self.assertNotEqual(strict.returncode, 0)
        self.assertFalse(self.output.exists())
        inherited = subprocess.run([*command, "--inherit-missing"], text=True, capture_output=True)
        self.assertEqual(inherited.returncode, 0, inherited.stderr)
        info = json.loads(inherited.stdout)
        self.assertEqual(info, update.verify(self.output))
        self.assertEqual(sum(row["policy"] == 2 for row in info["members"]), len(self.paths) - 1)

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
