#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""One-build repair packaging: actual producer, admission and source archives."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import package_update_repair as repair
import update_package
import test_update_addon as addon_fixture


class RepairTests(unittest.TestCase):
    def setUp(self):
        # Reuse the isolated real-Git, synthetic-runtime admission fixture;
        # this test never invokes an SDK or fabricates a production ELF.
        self.fixture = addon_fixture.AddonTests(methodName="runTest")
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.directory = self.fixture.candidate
        self.output = self.fixture.base / "repair.zip"

    def test_manual_root_only_sources_bindings_and_checksums(self):
        result = repair.package_repair(self.directory, self.output)
        report = json.loads((self.directory / "build.json").read_text())
        with zipfile.ZipFile(self.output) as archive:
            install = [name for name in archive.namelist() if name.startswith("KUI/")]
            self.assertEqual(set(install), set(result["install_files"]))
            self.assertEqual(install, ["KUI/runtime.kui"])
            self.assertFalse(any(name.endswith(".kup") for name in archive.namelist()))
            self.assertFalse(any(name.startswith("KUI/apps/") for name in archive.namelist()))
            runtime = archive.read("KUI/runtime.kui")
            self.assertEqual(hashlib.sha256(runtime).hexdigest(), report["runtime_sha256"])
            delivery = json.loads(archive.read("build.json"))
            self.assertEqual(delivery["installation"], "manual-root-migration")
            self.assertFalse(delivery["updater_installable"])
            source_name = "source/K-UI-" + report["build"] + ".tar.gz"
            with tarfile.open(fileobj=io.BytesIO(archive.read(source_name))) as source:
                self.assertEqual(source.pax_headers["comment"], report["source_commit"])
            for name, expected in report["binding_manifests"].items():
                self.assertEqual(hashlib.sha256(archive.read("evidence/bindings/" + Path(name).name)).hexdigest(), expected["sha256"])
            for line in archive.read("SHA256SUMS").decode().splitlines():
                sha, name = line.split("  ", 1)
                self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(), sha)
            text = archive.read("START-HERE.md").decode()
            self.assertIn("rename KUI/system", text)
            self.assertIn("KUI/apps/system is a DIFFERENT folder", text)
            self.assertIn("If KUI/boot.kui exists", text)
            self.assertIn("Do not open this ZIP in K-UI Updates", text)
            self.assertIn("26-entry", text)
            self.assertIn("Incomplete update cohort", text)
            self.assertIn("Restart K-UI", text)

    def test_existing_output_and_changed_runtime_are_refused(self):
        self.output.write_bytes(b"existing delivery")
        with self.assertRaises(ValueError):
            repair.package_repair(self.directory, self.output)
        self.assertEqual(self.output.read_bytes(), b"existing delivery")
        self.output.unlink()
        runtime = self.directory / "KUI/runtime.kui"
        data = bytearray(runtime.read_bytes())
        data[-1] ^= 1
        runtime.write_bytes(data)
        with self.assertRaises(ValueError):
            repair.package_repair(self.directory, self.output)
        self.assertFalse(self.output.exists())



if __name__ == "__main__":
    unittest.main()
