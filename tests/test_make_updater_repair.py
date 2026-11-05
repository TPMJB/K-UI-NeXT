#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Current bootstrap publication cannot leave an old physical boot path active."""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import make_updater_repair as repair
from runtime_package import envelope
import update_package as update


def gui(engine=update.ENGINE_CURRENT):
    payload = update.CAPABILITY + b"KUIENG" + str(engine).encode() + b"!" + struct.pack("<II", engine, 0xffffffff - engine)
    payload += update.HANDOFF_HEADER + bytes(176) + bytes(4)
    return envelope(payload, len(payload), "123456abcdef")


class CurrentBootstrapTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="kui-bootstrap-test-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.stage = self.base / "KUI"
        self.stage.mkdir()
        self.runtime = gui()
        self.put("runtime.kui", self.runtime)
        self.output = self.base / "repair.zip"
        self.kup = self.base / "repair.kup"

    def put(self, name, data):
        path = self.stage / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def test_one_gui_stage_boots_both_paths_and_root_records_decode_in_current_c(self):
        report = repair.build_repair(self.stage, self.output)
        self.assertEqual(report["engine"], update.ENGINE_CURRENT)
        self.assertEqual(report["software_files"], 1)
        self.assertEqual(report["gui_build"], "123456abcdef")
        self.assertEqual(report["kup_sha256"], hashlib.sha256(self.kup.read_bytes()).hexdigest())
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.read("KUI/runtime.kui"), self.runtime)
            self.assertEqual(archive.read("KUI/boot.kui"), self.runtime)
            self.assertEqual(archive.read("KUI/updates/test/repair.kup"), self.kup.read_bytes())
            self.assertEqual([name for name in archive.namelist() if name.endswith(".kup")], ["KUI/updates/test/repair.kup"])
            for index in (0, 1):
                wire = archive.read(f"KUI/system/active{index}.bin")
                self.assertEqual(wire[:8], b"KUIACT1\0")
                self.assertEqual(struct.unpack_from("<Q", wire, 16)[0], index + 1)
                self.assertEqual(wire[24:124], bytes(100))
                self.assertEqual(struct.unpack_from("<I", wire, 124)[0], zlib.crc32(wire[:124]))
            evidence = json.loads(archive.read("updater-repair.json"))
            self.assertEqual(evidence["activation_validation"], {"decoder": "current-source", "root_selected": True, "sequences": [1, 2]})
            self.assertEqual(evidence["package"]["flags"], update.REQUIRES_ENGINE3)
            self.assertEqual(evidence["package"]["consumer"]["decoder"], "current-source")
            self.assertFalse(evidence["console_tested"])
            self.assertRegex(evidence["updater_source"]["sha256"]["src/core/update.c"], r"^[0-9a-f]{64}$")

    def test_seven_files_keep_every_reader_and_helper_exact(self):
        software = {"runtime.kui": self.runtime}
        for index in range(4):
            software[f"apps/games/reader-{index}.kui"] = envelope(bytes((index,)) * 16, 16, f"{index + 1:012x}")
        software["apps/games/one-helper.bin"] = b"\0original helper one\xff"
        software["apps/games/two-helper.bin"] = b"original helper two"
        for name, data in software.items():
            self.put(name, data)
        repair.build_repair(self.stage, self.output, kup_output=self.kup)
        report = update.verify(self.kup)
        self.assertEqual({entry["path"] for entry in report["members"]}, set(software))
        offset = 128 + len(report["members"]) * 160
        payload = self.kup.read_bytes()
        with zipfile.ZipFile(self.output) as archive:
            for entry in report["members"]:
                expected = software[entry["path"]]
                self.assertEqual(archive.read("KUI/" + entry["path"]), expected)
                self.assertEqual(payload[offset:offset + entry["bytes"]], expected)
                offset += entry["bytes"]
        self.assertEqual(offset, len(payload))

    def test_old_gui_private_mutable_and_control_inputs_publish_nothing(self):
        self.put("runtime.kui", gui(2))
        with self.assertRaisesRegex(ValueError, "current updater engine"):
            repair.build_repair(self.stage, self.output)
        self.assertFalse(self.output.exists() or self.kup.exists())
        self.put("runtime.kui", self.runtime)
        for name in ("boot.kui", "system/active0.bin", "prefs/settings.bin", "apps/games/selection-a.bin",
                     "apps/games/launch-settings-a.bin", "apps/games/smash-vc2-vmu-session.bin"):
            with self.subTest(path=name):
                self.put(name, b"do not freeze mutable state")
                with self.assertRaises(ValueError):
                    repair.build_repair(self.stage, self.output)
                self.assertFalse(self.output.exists() or self.kup.exists())
                (self.stage / name).unlink()
                for parent in (self.stage / name).parents:
                    if parent == self.stage:
                        break
                    if not any(parent.iterdir()):
                        parent.rmdir()

    def test_missing_gui_and_symlink_stage_are_rejected(self):
        (self.stage / "runtime.kui").unlink()
        self.put("apps/games/test-helper.bin", b"helper")
        with self.assertRaisesRegex(ValueError, "runtime.kui"):
            repair.build_repair(self.stage, self.output)
        (self.stage / "runtime.kui").symlink_to(self.base / "elsewhere")
        with self.assertRaises(ValueError):
            repair.build_repair(self.stage, self.output)
        self.assertFalse(self.output.exists() or self.kup.exists())

    def test_existing_or_inconsistent_outputs_are_never_overwritten(self):
        for existing in (self.output, self.kup):
            with self.subTest(path=existing.name):
                existing.write_bytes(b"retained existing output")
                with self.assertRaisesRegex(ValueError, "existing output"):
                    repair.build_repair(self.stage, self.output)
                self.assertEqual(existing.read_bytes(), b"retained existing output")
                self.assertFalse((self.kup if existing == self.output else self.output).exists())
                existing.unlink()
        self.output.symlink_to(self.base / "absent.zip")
        with self.assertRaises(ValueError):
            repair.build_repair(self.stage, self.output)
        self.assertFalse(self.kup.exists())

    def test_publication_error_rolls_back_only_new_outputs(self):
        real_link = os.link
        count = 0

        def fail_second(*args, **kwargs):
            nonlocal count
            count += 1
            if count == 2:
                raise OSError("simulated second publication failure")
            return real_link(*args, **kwargs)

        with mock.patch.object(repair.os, "link", side_effect=fail_second), self.assertRaisesRegex(OSError, "second publication"):
            repair.build_repair(self.stage, self.output)
        self.assertFalse(self.output.exists() or self.kup.exists())
        self.assertEqual(list(self.base.glob(".kui-repair-*")), [])

    def test_racing_existing_second_output_is_preserved(self):
        real_link = os.link
        count = 0

        def race_second(source, destination, **kwargs):
            nonlocal count
            count += 1
            if count == 2:
                Path(destination).write_bytes(b"new unrelated output")
            return real_link(source, destination, **kwargs)

        with mock.patch.object(repair.os, "link", side_effect=race_second), self.assertRaises(FileExistsError):
            repair.build_repair(self.stage, self.output)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.kup.read_bytes(), b"new unrelated output")
        self.assertEqual(list(self.base.glob(".kui-repair-*")), [])

    def test_current_activation_decoder_rejects_corruption_and_bank_selection(self):
        paths = [self.base / "active0.bin", self.base / "active1.bin"]
        for index, path in enumerate(paths):
            path.write_bytes(repair.root_activation(index + 1))
        self.assertTrue(repair.check_controls(paths, self.base)["root_selected"])
        for checksum_valid in (False, True):
            wire = bytearray(repair.root_activation(1))
            wire[24:36] = b"123456abcdef"
            if checksum_valid:
                wire[48:80] = bytes([1]) * 32
                struct.pack_into("<I", wire, 124, zlib.crc32(wire[:124]))
            paths[0].write_bytes(wire)
            with self.assertRaisesRegex(ValueError, "activation decoder rejected"):
                repair.check_controls(paths, self.base)


if __name__ == "__main__":
    unittest.main()
