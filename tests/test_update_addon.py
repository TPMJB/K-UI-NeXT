#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""GUI-only delivery admission and native-source guards; no SDK compilation."""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import update_addon as addon
import update_package as update
import update_source
from runtime_package import envelope


class AddonTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="kui-addon-test-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.repo = self.base / "repo"
        self.repo.mkdir()
        self.git("init", "-q")
        self.git("config", "user.email", "fixture@example.invalid")
        self.git("config", "user.name", "Synthetic fixture")
        for name in ("src/loader/worker.c", "Makefile.vmu-fixture", "src/core/vmu_fixture.c",
                     "src/core/disc_fixture.c", "src/core/games_library.c", "src/apps/games.c",
                     "src/core/retail_fixture.c", "include/kui/native_fixture.h", "include/kui/disc_fixture.h",
                     "LICENSE", "THIRD_PARTY.md", "dependencies.json"):
            file = self.repo / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("{}\n" if name == "dependencies.json" else "Synthetic source fixture\n")
        self.anchor = self.commit("Synthetic native anchor")
        (self.repo / "README.md").write_text("GUI A\n")
        self.a = self.commit("Synthetic GUI A")
        (self.repo / "README.md").write_text("GUI B\n")
        self.b = self.commit("Synthetic GUI B")
        for name, value in (("ROOT", self.repo), ("SOURCE_BASELINE", self.anchor)):
            patch = mock.patch.object(addon, name, value)
            patch.start()
            self.addCleanup(patch.stop)
        patch = mock.patch.object(update_source, "ROOT", self.repo)
        patch.start()
        self.addCleanup(patch.stop)
        self.baseline = self.fixture("baseline", self.a)
        self.candidate = self.fixture("candidate", self.b)
        self.output = self.base / "addon.zip"

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.repo, stderr=subprocess.PIPE).decode().strip()

    def commit(self, message):
        self.git("add", ".")
        self.git("commit", "-qm", message)
        return self.git("rev-parse", "HEAD")

    def fixture(self, name, commit):
        directory = self.base / name
        (directory / "KUI").mkdir(parents=True)
        body = update.CAPABILITY + update.HANDOFF_HEADER + bytes(176) + bytes(4)
        runtime = envelope(body, len(body), commit[:12])
        (directory / "KUI/runtime.kui").write_bytes(runtime)
        payload = directory / addon.payload_name(commit[:12])
        package = update.build_package(directory / "KUI", payload, commit[:12], "testing", inherit_missing=True)
        elf_sha = hashlib.sha256(b"synthetic compile witness" + commit.encode()).hexdigest()
        receipt = {"commit": commit, "source_dirty": False, "elf_sha256": elf_sha}
        report = {"kind": "offline-updater-addon", "source_commit": commit, "build": commit[:12],
            "source_dirty": False, "hardware_acceptance": False, "channel": "testing",
            "replacements": ["runtime.kui"], "inherited": list(update.catalog()[1:]),
            "payload": payload.name, "payload_sha256": addon.digest(payload.read_bytes()),
            "runtime_sha256": addon.digest(runtime), "elf_sha256": elf_sha, "compile": receipt,
            "native_source_guard": addon.check_sources(commit), "package": package}
        (directory / "gui.compile.json").write_text(json.dumps(receipt))
        (directory / "build.json").write_text(json.dumps(report))
        return directory

    def alter_report(self, **changes):
        path = self.candidate / "build.json"
        report = json.loads(path.read_text())
        report.update(changes)
        path.write_text(json.dumps(report))

    def test_admits_gui_only_package_and_fixed_delivery_inventory(self):
        _, baseline, _ = addon.admitted_addon(self.baseline)
        _, candidate, _ = addon.admitted_addon(self.candidate)
        result = addon.package_addon(self.baseline, self.candidate, self.output)
        self.assertFalse(result["hardware_acceptance"])
        with zipfile.ZipFile(self.output) as archive:
            install = [name for name in archive.namelist() if name.startswith("KUI/")]
            self.assertEqual(sorted(install), ["KUI/runtime.kui", "KUI/updates/" + candidate["payload"]])
            self.assertEqual(archive.read("KUI/runtime.kui"), (self.baseline / "KUI/runtime.kui").read_bytes())
            self.assertNotIn("complete_cohort", json.loads(archive.read("build.json")))
            self.assertIn("withdrawn", archive.read("START-HERE.md").decode())
            self.assertIn("not a request", archive.read("START-HERE.md").decode())
            for label, report in (("baseline", baseline), ("candidate", candidate)):
                data = archive.read("source/K-UI-" + label + "-" + report["build"] + ".tar.gz")
                with tarfile.open(fileobj=io.BytesIO(data)) as source:
                    self.assertEqual(source.pax_headers["comment"], report["source_commit"])
                    self.assertEqual(source.extractfile("K-UI-" + report["build"] + "/README.md").read(),
                                     ("GUI A\n" if label == "baseline" else "GUI B\n").encode())

    def test_refuses_presence_changing_staged_reader_or_empty_apps_directory(self):
        apps = self.candidate / "KUI/apps"
        apps.mkdir()
        with self.assertRaises(ValueError):
            addon.package_addon(self.baseline, self.candidate, self.output)
        (apps / "reader.kui").write_bytes(b"reader must never ship")
        with self.assertRaises(ValueError):
            addon.admitted_addon(self.candidate)
        self.assertFalse(self.output.exists())

    def test_refuses_extra_replacement_even_in_resealed_valid_package(self):
        source = self.base / "extra"
        (source / "apps/games").mkdir(parents=True)
        (source / "runtime.kui").write_bytes((self.candidate / "KUI/runtime.kui").read_bytes())
        (source / "apps/games/probe.kui").write_bytes(envelope(bytes(20), 20, self.b[:12]))
        payload = self.candidate / addon.payload_name(self.b[:12])
        package = update.build_package(source, payload, self.b[:12], "testing", inherit_missing=True)
        self.alter_report(package=package, payload_sha256=addon.digest(payload.read_bytes()))
        with self.assertRaises(ValueError):
            addon.admitted_addon(self.candidate)

    def test_refuses_mismatched_runtime_package_hash_and_claimed_scope(self):
        original = (self.candidate / "build.json").read_text()
        for changes in ({"runtime_sha256": "00" * 32}, {"payload_sha256": "00" * 32},
                        {"replacements": ["runtime.kui", "apps/games/probe.kui"]},
                        {"source_dirty": True}, {"complete_cohort": True},
                        {"payload": "../candidate.kup"}, {"native_source_guard": {}}):
            with self.subTest(changes=changes):
                (self.candidate / "build.json").write_text(original)
                self.alter_report(**changes)
                with self.assertRaises(ValueError):
                    addon.admitted_addon(self.candidate)

    def test_refuses_gui_symlink_and_compile_receipt_substitution(self):
        runtime = self.candidate / "KUI/runtime.kui"
        copy = self.base / "runtime-copy.kui"
        runtime.rename(copy)
        runtime.symlink_to(copy)
        with self.assertRaises(ValueError):
            addon.admitted_addon(self.candidate)
        runtime.unlink()
        copy.rename(runtime)
        receipt = self.candidate / "gui.compile.json"
        value = json.loads(receipt.read_text())
        value["source_dirty"] = True
        receipt.write_text(json.dumps(value))
        with self.assertRaises(ValueError):
            addon.admitted_addon(self.candidate)

    def test_refuses_symlinked_admission_evidence(self):
        for name in ("build.json", "gui.compile.json"):
            original = self.candidate / name
            external = self.base / ("external-" + name)
            original.rename(external)
            original.symlink_to(external)
            with self.subTest(name=name), self.assertRaises(ValueError):
                addon.admitted_addon(self.candidate)
            original.unlink()
            external.rename(original)
    def test_native_guard_refuses_added_changed_and_deleted_protected_sources(self):
        for name, action in (("src/loader/worker.c", "change"), ("Makefile.native-new", "add"),
                             ("src/core/vmu_fixture.c", "delete"), ("include/kui/native_new.h", "add")):
            with self.subTest(name=name, action=action):
                self.git("reset", "--hard", self.b)
                path = self.repo / name
                if action == "delete":
                    path.unlink()
                else:
                    path.write_text("Changed protected source\n")
                changed = self.commit("Synthetic guard refusal")
                with self.assertRaises(ValueError):
                    addon.check_sources(changed)

    def test_native_guard_runs_before_any_sdk_make(self):
        (self.repo / "Makefile.native-new").write_text("Must refuse before SDK\n")
        self.commit("Synthetic incompatible native recipe")
        real_run = subprocess.run
        def no_sdk(command, *args, **kwargs):
            self.assertNotEqual(command[0], "make", "SDK compilation started before source admission")
            return real_run(command, *args, **kwargs)
        with mock.patch.object(addon.subprocess, "run", side_effect=no_sdk):
            with self.assertRaises(ValueError):
                addon.build(self.base / "must-not-build")

    def test_dirty_source_refuses_before_any_sdk_make(self):
        (self.repo / "README.md").write_text("Uncommitted GUI\n")
        real_run = subprocess.run
        def no_sdk(command, *args, **kwargs):
            self.assertNotEqual(command[0], "make", "SDK compilation started with dirty source")
            return real_run(command, *args, **kwargs)
        with mock.patch.object(addon.subprocess, "run", side_effect=no_sdk):
            with self.assertRaises(ValueError):
                addon.build(self.base / "must-not-build")

    def test_refuses_same_build_and_existing_output(self):
        with self.assertRaises(ValueError):
            addon.package_addon(self.baseline, self.baseline, self.output)
        self.output.write_bytes(b"existing delivery")
        with self.assertRaises(ValueError):
            addon.package_addon(self.baseline, self.candidate, self.output)
        self.assertEqual(self.output.read_bytes(), b"existing delivery")


if __name__ == "__main__":
    unittest.main()
