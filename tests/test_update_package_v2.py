#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Current producer -> exact C consumer, with mixed test/release software."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import update_package as update
from make_update import make_update
from runtime_package import envelope


class ProducerV2Tests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="kui-v2-producer-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.stage = self.base / "KUI"
        self.stage.mkdir()
        self.output = self.base / "test.kup"

    def put(self, name, data):
        target = self.stage / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return target

    def kui(self, build="111122223333", payload=bytes(16)):
        return envelope(payload, len(payload), build)

    def gui(self, build="444455556666", engine=True):
        payload = update.CAPABILITY + (update.ENGINE if engine else bytes(16))
        payload += update.HANDOFF_HEADER + bytes(176) + bytes(4)
        return self.kui(build, payload)

    def reader(self, name="apps/games/test-reader.kui", build="111122223333", payload=bytes(16)):
        return self.put(name, self.kui(build, payload))

    def build(self, **kwargs):
        return update.build_package_v2(self.stage, self.output, **kwargs)

    def test_reader_only_and_same_build_configurations_have_content_identity(self):
        reader = self.reader()
        first = self.build()
        self.assertEqual(first["format"], 2)
        self.assertEqual([e["path"] for e in first["members"]], ["apps/games/test-reader.kui"])
        self.assertEqual(first["members"][0]["build"], "111122223333")
        self.assertNotEqual(first["build"], "111122223333")
        self.assertEqual(first["consumer"]["decoder"], "current-source")
        self.assertEqual(len(first["consumer"]["source_sha256"]), 64)
        again = self.build()
        self.assertEqual(again["build"], first["build"])
        reader.write_bytes(self.kui("111122223333", bytes([7]) * 16))
        changed = self.build()
        self.assertNotEqual(changed["build"], first["build"])

    def test_full_release_contains_every_staged_file_and_own_builds(self):
        gui = self.gui()
        self.put("runtime.kui", gui)
        self.reader(build="aaaabbbbcccc")
        self.reader("apps/disc/other.kui", "ddddeeeeffff")
        self.put("apps/games/brand-new-helper.bin", b"\0raw\xffhelper")
        self.put("apps/games/probe.dat", b"probe data")
        self.put("apps/music/menu.ogg", b"OggSbytes")
        self.put("apps/music/click.wav", b"RIFFbytes")
        self.put("assets/theme/preview.png", b"raw asset")
        self.put("tosec.db", b"dbbytes")
        report = self.build(release=True, channel="stable")
        self.assertTrue(report["full"])
        self.assertEqual(report["flags"], update.FULL | update.REQUIRES_ENGINE3)
        self.assertTrue(report["consumer"]["full"])
        self.assertEqual(self.output.read_bytes()[45], update.FULL | update.REQUIRES_ENGINE3)
        expected = set(update.inventory_v2(self.stage))
        self.assertEqual({e["path"] for e in report["members"]}, expected)
        self.assertTrue(all(e["policy"] in (update.SELF_KUI, update.RAW_SOFTWARE) for e in report["members"]))
        self.assertEqual(report["members"][0]["path"], "runtime.kui")
        self.assertEqual(report["members"][0]["build"], "444455556666")
        data = self.output.read_bytes()
        offset = 128 + 160 * len(report["members"])
        for entry in report["members"]:
            self.assertEqual(data[offset:offset + entry["bytes"]], (self.stage / entry["path"]).read_bytes())
            offset += entry["bytes"]
        self.assertEqual(offset, len(data))

    def test_empty_raw_asset_is_a_real_file(self):
        self.put("assets/empty.txt", b"")
        report = self.build()
        self.assertEqual(report["members"][0]["bytes"], 0)
        self.assertEqual(report["members"][0]["policy"], update.RAW_SOFTWARE)
        self.assertEqual(report["members"][0]["sha256"], hashlib.sha256(b"").hexdigest())

    def test_package_identity_is_not_a_gui_build_restriction(self):
        self.reader()
        self.build()
        data = bytearray(self.output.read_bytes())
        # These IDs name withdrawn GUI source builds, not every independently
        # identified artifact that happens to use the same twelve hex digits.
        data[32:44] = b"28cadccf0e71"
        struct.pack_into("<I", data, 124, zlib.crc32(data[:124]))
        self.output.write_bytes(data)
        self.assertEqual(update.verify(self.output)["format"], 2)
        update.current_consumer_check(self.output)

    def test_keep_and_remove_have_no_payload(self):
        self.reader()
        report = self.build(keep=["apps/disc/retained.kui"], remove=["apps/games/absent-helper.bin"])
        directives = {e["path"]: e for e in report["members"] if e["policy"] in (2, 5)}
        self.assertEqual(directives["apps/disc/retained.kui"]["policy"], 2)
        self.assertEqual(directives["apps/games/absent-helper.bin"]["policy"], 5)
        self.assertTrue(all(not e["bytes"] and e["sha256"] == "00" * 32 for e in directives.values()))

    def test_old_gui_is_refused_before_existing_output_changes(self):
        self.put("runtime.kui", self.gui(engine=False))
        self.output.write_bytes(b"retained output")
        with self.assertRaisesRegex(ValueError, "current updater engine"):
            self.build()
        self.assertEqual(self.output.read_bytes(), b"retained output")

    def test_release_requires_gui_but_test_does_not(self):
        self.reader()
        with self.assertRaisesRegex(ValueError, "full software release"):
            self.build(release=True)
        self.assertFalse(self.output.exists())
        self.build()

    def test_staged_new_role_is_automatically_included(self):
        self.reader("apps/music/new-role.kui")
        report = self.build()
        self.assertEqual(report["members"][0]["path"], "apps/music/new-role.kui")

    def test_software_release_can_exceed_the_old_64_role_inventory(self):
        for index in range(80):
            self.put(f"assets/test-{index:03d}.dat", bytes((index,)))
        report = self.build()
        self.assertEqual(len(report["members"]), 80)
        self.assertEqual(report["consumer"]["members"], 80)

    def test_full_flag_and_channel_are_part_of_package_identity(self):
        self.put("runtime.kui", self.gui())
        self.reader()
        partial = self.build(channel="testing")
        full = self.build(full=True, channel="testing")
        stable = self.build(full=True, channel="stable")
        self.assertEqual(len({partial["build"], full["build"], stable["build"]}), 3)
        self.assertFalse(partial["full"])
        self.assertTrue(full["full"])
        self.assertEqual(full["mode"], "full")

    def test_full_package_cannot_depend_on_prior_software(self):
        self.put("runtime.kui", self.gui())
        with self.assertRaisesRegex(ValueError, "cannot keep"):
            self.build(full=True, keep=["apps/games/old.kui"])

    def test_invalid_full_flags_do_not_pass_host_or_consumer(self):
        self.put("runtime.kui", self.gui())
        self.build()
        data = bytearray(self.output.read_bytes())
        data[45] = 4
        struct.pack_into("<I", data, 124, zlib.crc32(data[:124]))
        self.output.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "Invalid v2 update header"):
            update.verify(self.output)
        with self.assertRaises(ValueError):
            update.current_consumer_check(self.output)

    def test_private_and_unknown_files_are_not_silently_omitted(self):
        for name in ("vmu/cards/save.vmd", "apps/system/settings.bin", "saves/card.bin", "updates/old.kup",
                     "apps/games/unknown.dat", "assets/name./file.png", "assets/a/b/c/d/e.png"):
            with self.subTest(name=name):
                self.put(name, b"data")
                with self.assertRaises(ValueError):
                    self.build()
                shutil.rmtree(self.stage)
                self.stage.mkdir()

    def test_symlink_stage_members_are_refused(self):
        reader = self.reader()
        real = self.base / "external.kui"
        reader.rename(real)
        reader.symlink_to(real)
        with self.assertRaisesRegex(ValueError, "Symlink"):
            self.build()

    def test_duplicate_directives_and_stage_conflicts_are_refused(self):
        self.reader()
        for kwargs in ({"remove": ["apps/games/test-reader.kui"]},
                       {"remove": ["assets/x", "assets/x"]},
                       {"remove": ["assets/x"], "keep": ["assets/x"]}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                self.build(**kwargs)

    def test_console_unsafe_paths_are_refused(self):
        for path in ("../runtime.kui", "apps/games/../runtime.kui", "assets/a//b", "assets/Upper.png",
                     "assets/file.", "apps/games/x..kui", "0:/KUI/runtime.kui"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.build(remove=[path])

    def test_current_c_consumer_rejects_corruption_even_if_python_check_is_bypassed(self):
        self.reader()
        self.build()
        data = bytearray(self.output.read_bytes())
        data[-1] ^= 1
        self.output.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "current Main consumer|Current Main consumer"):
            update.current_consumer_check(self.output)

    def test_consumer_source_change_requires_new_compiler_fingerprint(self):
        self.reader()
        self.build()
        # Simulate source changing while the actual checker is compiling.
        # A stale checker must neither be published nor retained for retry.
        with mock.patch.object(update, "consumer_fingerprint", side_effect=["0" * 64, "1" * 64]):
            with self.assertRaisesRegex(ValueError, "source changed during consumer compilation"):
                update.current_consumer_check(self.output)
        self.assertIsNone(update._CONSUMER_CACHE)
        checked = update.current_consumer_check(self.output)
        self.assertEqual(checked["source_sha256"], update.consumer_fingerprint())

    def test_producer_does_not_publish_when_actual_consumer_refuses(self):
        self.reader()
        self.output.write_bytes(b"previous")
        with mock.patch.object(update, "current_consumer_check", side_effect=ValueError("consumer refusal")):
            with self.assertRaisesRegex(ValueError, "consumer refusal"):
                self.build()
        self.assertEqual(self.output.read_bytes(), b"previous")

    def test_zip_output_contains_only_one_checked_kup(self):
        self.reader()
        output = self.base / "test.zip"
        report = make_update(self.stage, output)
        with zipfile.ZipFile(output) as archive:
            self.assertEqual(archive.namelist(), ["test.kup"])
            captured = self.base / "captured.kup"
            captured.write_bytes(archive.read("test.kup"))
        self.assertEqual(update.verify(captured)["build"], report["build"])
        update.current_consumer_check(captured)

    def test_old_manual_zip_software_is_repacked_with_every_file(self):
        old = self.base / "manual.zip"
        with zipfile.ZipFile(old, "w") as archive:
            archive.writestr("KUI/runtime.kui", self.gui())
            archive.writestr("KUI/apps/games/test.kui", self.kui())
            archive.writestr("KUI/apps/games/test-helper.bin", b"helper bytes")
            archive.writestr("KUI/updates/stale.kup", b"ignored derived delivery")
            archive.writestr("source/unused.tar.gz", b"unrelated source archive")
        report = make_update(old, self.output)
        self.assertEqual({e["path"] for e in report["members"]},
                         {"runtime.kui", "apps/games/test.kui", "apps/games/test-helper.bin"})
        self.assertEqual(report["replaced_embedded_update_files"], ["KUI/updates/stale.kup"])

    def test_zip_with_only_one_v2_package_is_checked_and_copied(self):
        self.reader()
        first = self.build()
        source = self.base / "input.zip"
        with zipfile.ZipFile(source, "w") as archive:
            archive.write(self.output, "test.kup")
        target = self.base / "copy.kup"
        copied = make_update(source, target)
        self.assertEqual(target.read_bytes(), self.output.read_bytes())
        self.assertEqual(copied["build"], first["build"])

    def test_partial_embedded_package_is_not_silently_relabelled_full(self):
        self.put("runtime.kui", self.gui())
        self.reader()
        self.build()
        source = self.base / "partial.zip"
        with zipfile.ZipFile(source, "w") as archive:
            archive.write(self.output, "partial.kup")
        target = self.base / "full.kup"
        with self.assertRaisesRegex(ValueError, "rebuilt from its staged software"):
            make_update(source, target, full=True)
        self.assertFalse(target.exists())

    def test_zip_traversal_and_private_software_are_refused(self):
        for name in ("../KUI/runtime.kui", "KUI/apps/system/settings.bin"):
            source = self.base / "bad.zip"
            with zipfile.ZipFile(source, "w") as archive:
                archive.writestr(name, self.gui())
            with self.subTest(name=name), self.assertRaises(ValueError):
                make_update(source, self.output)

    def test_ordinary_build_cli_uses_current_v2_producer(self):
        self.reader()
        result = subprocess.run([sys.executable, str(ROOT / "tools/update_package.py"), "build",
                                 str(self.stage), str(self.output)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["format"], 2)


if __name__ == "__main__":
    unittest.main()
