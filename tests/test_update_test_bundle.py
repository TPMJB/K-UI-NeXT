#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Test checked-package test ZIPs without SDK builds or real Git archives."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import update_package as update
import update_test_bundle as bundles
from runtime_package import envelope

COMMIT = "123456abcdef" + "0" * 28
BUILD = COMMIT[:12]


class TestUpdateBundle(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="kui-test-bundle-check-")
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.root = self.base / "KUI"
        self.root.mkdir()
        body = update.CAPABILITY + update.HANDOFF_HEADER + bytes(176) + b"GUI!"
        (self.root / "runtime.kui").write_bytes(envelope(body, len(body), BUILD))
        self.package = self.base / "candidate.kup"
        self.info = self.make_package()
        self.output = self.base / "delivery" / "test.zip"
        source = io.BytesIO()
        with tarfile.open(fileobj=source, mode="w:gz", pax_headers={"comment": COMMIT}) as archive:
            content = b"Synthetic exact-source fixture\n"
            entry = tarfile.TarInfo("K-UI-" + BUILD + "/README.md")
            entry.size = len(content)
            archive.addfile(entry, io.BytesIO(content))
        self.source = source.getvalue()
        patcher = mock.patch.object(bundles, "project_archive", return_value=(self.source, {}))
        self.archive = patcher.start()
        self.addCleanup(patcher.stop)

    def make_package(self, channel="testing"):
        return update.build_package(self.root, self.package, BUILD, channel, inherit_missing=True)

    def bundle(self, **changes):
        return bundles.bundle(changes.get("package", self.package), changes.get("output", self.output),
            changes.get("source_commit", COMMIT), changes.get("label", "Synthetic updater test"))

    def test_contains_one_exact_package_source_and_honest_manifest(self):
        captured = self.package.read_bytes()
        result = self.bundle()
        self.archive.assert_called_once_with(COMMIT)
        self.assertFalse(result["hardware_acceptance"])
        self.assertEqual(result["sha256"], hashlib.sha256(self.output.read_bytes()).hexdigest())
        with zipfile.ZipFile(self.output) as archive:
            names = archive.namelist()
            packages = [name for name in names if name.lower().endswith(".kup")]
            self.assertEqual(packages, [result["package"]])
            self.assertEqual(archive.read(packages[0]), captured)
            self.assertEqual(archive.read("source/K-UI-" + BUILD + ".tar.gz"), self.source)
            self.assertFalse(any(name.startswith("KUI/") for name in names))
            metadata = json.loads(archive.read("build.json"))
            self.assertEqual(metadata["source_commit"], COMMIT)
            self.assertEqual(metadata["package_sha256"], hashlib.sha256(captured).hexdigest())
            self.assertEqual(metadata["replacements"], ["runtime.kui"])
            self.assertEqual(metadata["inherited_root"], list(update.catalog()[1:]))
            self.assertFalse(metadata["hardware_acceptance"])
            checks = {name: digest for digest, name in
                (line.split("  ", 1) for line in archive.read("SHA256SUMS").decode().splitlines())}
            self.assertEqual(set(checks), set(names) - {"SHA256SUMS"})
            for name, digest in checks.items():
                self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(), digest)
            self.assertIn("Source files are not installed", archive.read("ABOUT.txt").decode())

    def test_reports_only_explicit_additional_replacements(self):
        extra = self.root / "apps/games/probe.kui"
        extra.parent.mkdir(parents=True)
        extra.write_bytes(envelope(b"TEST" * 4, 16, BUILD))
        self.make_package()
        result = self.bundle()
        self.assertEqual(result["replacements"], ["runtime.kui", "apps/games/probe.kui"])
        self.assertEqual(result["inherited_root"], list(update.catalog()[2:]))
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(archive.read(result["package"]), self.package.read_bytes())
            self.assertNotIn("apps/games/probe.kui", archive.namelist())

    def test_rejects_source_identity_channel_and_unreadable_labels_before_output(self):
        for commit in ("", BUILD, COMMIT.upper(), "g" * 40, "abcdef123456" + COMMIT[12:]):
            with self.subTest(commit=commit), self.assertRaises(ValueError):
                self.bundle(source_commit=commit)
        for label in ("", "x" * 97, "line\nbreak", "NUL\0", "DEL\x7f", "C1\x85"):
            with self.subTest(label=label), self.assertRaises(ValueError):
                self.bundle(label=label)
        self.make_package("stable")
        with self.assertRaises(ValueError):
            self.bundle()
        self.archive.assert_not_called()
        self.assertFalse(self.output.parent.exists())

    def test_existing_output_and_source_collision_preserve_bytes(self):
        self.output.parent.mkdir()
        self.output.write_bytes(b"Existing delivery")
        original = self.package.read_bytes()
        for output in (self.output, self.package):
            with self.subTest(output=output), self.assertRaises(ValueError):
                self.bundle(output=output)
        self.assertEqual(self.output.read_bytes(), b"Existing delivery")
        self.assertEqual(self.package.read_bytes(), original)
        self.archive.assert_not_called()

    def test_refuses_input_and_output_symlinks_including_ancestors(self):
        linked = self.base / "linked.kup"
        linked.symlink_to(self.package)
        ancestor = self.base / "linked-directory"
        ancestor.symlink_to(self.root, target_is_directory=True)
        (self.root / "inside.kup").write_bytes(self.package.read_bytes())
        for package in (linked, ancestor / "inside.kup"):
            with self.subTest(package=package), self.assertRaises(ValueError):
                self.bundle(package=package)
        outlink = self.base / "dangling.zip"
        outlink.symlink_to(self.base / "missing.zip")
        for output in (outlink, ancestor / "out.zip"):
            with self.subTest(output=output), self.assertRaises(ValueError):
                self.bundle(output=output)
        self.archive.assert_not_called()
        self.assertFalse(self.output.parent.exists())
        self.assertFalse((self.root / "out.zip").exists())
        self.assertFalse((self.base / "missing.zip").exists())

    def test_source_archive_failure_leaves_no_partial_or_output_directory(self):
        self.archive.side_effect = ValueError("Exact source archive could not be verified")
        with self.assertRaisesRegex(ValueError, "Exact source archive"):
            self.bundle()
        self.assertFalse(self.output.exists())
        self.assertFalse(self.output.parent.exists())
        self.assertEqual(update.verify(self.package), self.info)

    def test_corrupt_captured_bytes_are_rejected_even_while_disk_package_is_valid(self):
        original_read = Path.read_bytes
        original = self.package.read_bytes()
        captured = bytearray(original)
        captured[-1] ^= 1

        def corrupted_read(path):
            return bytes(captured) if path == self.package else original_read(path)

        with mock.patch.object(Path, "read_bytes", corrupted_read):
            self.assertEqual(update.verify(self.package), self.info)
            with self.assertRaises(ValueError):
                self.bundle()
        self.archive.assert_called_once_with(COMMIT)
        self.assertEqual(self.package.read_bytes(), original)
        self.assertEqual(update.verify(self.package), self.info)
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.output.parent.iterdir()), [])

    def test_manual_install_zip_is_not_implicitly_converted(self):
        manual = self.base / "legacy-install.zip"
        with zipfile.ZipFile(manual, "w") as archive:
            archive.writestr("KUI/runtime.kui", (self.root / "runtime.kui").read_bytes())
        with self.assertRaises(ValueError):
            self.bundle(package=manual)
        self.archive.assert_not_called()
        self.assertFalse(self.output.parent.exists())


if __name__ == "__main__":
    unittest.main()
