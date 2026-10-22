#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise clean FatFs setup and pin failures without contacting upstream."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("fetch_deps_under_test", ROOT / "tools/fetch_deps.py")
FETCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FETCH)
PINS = json.loads((ROOT / "dependencies.json").read_text())["fatfs"]
BUNDLE = ROOT / "third_party/fatfs-downloads"
ORIGINAL = {spec["url"].rsplit("/", 1)[-1]: (BUNDLE / spec["url"].rsplit("/", 1)[-1]).read_bytes()
            for spec in PINS}


class FatFsInputs(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="kui-fatfs-inputs-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.deps = self.root / ".deps"
        self.bundle = self.root / "third_party/fatfs-downloads"
        self.bundle.mkdir(parents=True)
        for name, data in ORIGINAL.items():
            (self.bundle / name).write_bytes(data)
        (self.root / "config").mkdir()
        shutil.copyfile(ROOT / "config/ffconf.h", self.root / "config/ffconf.h")
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(FETCH, "ROOT", self.root).start()
        mock.patch.object(FETCH, "DEPS", self.deps).start()
        self.download = mock.patch.object(FETCH, "download", side_effect=AssertionError("Unexpected network fetch")).start()

    def generated_is_correct(self):
        destination = self.deps / "fatfs"
        with zipfile.ZipFile(BUNDLE / "ff16.zip") as archive:
            for name in ("source/ff.h", "source/diskio.h", "source/ffunicode.c", "LICENSE.txt"):
                self.assertEqual((destination / name).read_bytes(), archive.read(name).replace(b"\r\n", b"\n"))
        # SHA-256 of pinned R0.16 ff.c after both exact upstream patches and
        # CRLF normalization; detects silently missing/reordered patch work.
        self.assertEqual(hashlib.sha256((destination / "source/ff.c").read_bytes()).hexdigest(),
                         "3c30280ffe2d0fb19a5241d9ed9f20a2c3911c2861652ebf6ad57f74fb369250")
        self.assertEqual((destination / "source/ffconf.h").read_bytes(), (ROOT / "config/ffconf.h").read_bytes())
        for spec in PINS:
            name = spec["url"].rsplit("/", 1)[-1]
            cached = self.deps / "downloads" / name
            self.assertEqual(cached.read_bytes(), ORIGINAL[name])
            self.assertEqual(hashlib.sha256(cached.read_bytes()).hexdigest(), spec["sha256"])

    def untouched_destination(self):
        sentinel = self.deps / "fatfs/source/ff.c"
        sentinel.parent.mkdir(parents=True, exist_ok=True)
        sentinel.write_bytes(b"previous verified source\n")
        return sentinel

    def test_clean_bundle_build_and_cache_reuse_need_no_network(self):
        self.assertFalse(self.deps.exists())
        FETCH.fetch_fatfs()
        self.generated_is_correct()
        self.download.assert_not_called()
        shutil.rmtree(self.bundle)
        shutil.rmtree(self.deps / "fatfs")
        FETCH.fetch_fatfs()
        self.generated_is_correct()
        self.download.assert_not_called()

    def test_original_bundle_bytes_and_license_match_upstream_pins(self):
        for spec in PINS:
            name = spec["url"].rsplit("/", 1)[-1]
            self.assertEqual(hashlib.sha256(ORIGINAL[name]).hexdigest(), spec["sha256"])
        with zipfile.ZipFile(BUNDLE / "ff16.zip") as archive:
            self.assertEqual((BUNDLE / "LICENSE.txt").read_bytes(), archive.read("LICENSE.txt"))

    def test_any_corrupt_bundle_stops_before_cache_or_extraction(self):
        sentinel = self.untouched_destination()
        for name, original in ORIGINAL.items():
            with self.subTest(file=name):
                (self.bundle / name).write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
                with self.assertRaisesRegex(SystemExit, "Bundled checksum mismatch"):
                    FETCH.fetch_fatfs()
                self.assertEqual(sentinel.read_bytes(), b"previous verified source\n")
                self.assertFalse((self.deps / "downloads").exists())
                self.download.assert_not_called()
                (self.bundle / name).write_bytes(original)

    def test_corrupt_cache_is_not_replaced_by_good_bundle(self):
        sentinel = self.untouched_destination()
        cache = self.deps / "downloads"
        cache.mkdir()
        for name in ORIGINAL:
            with self.subTest(file=name):
                bad = cache / name
                bad.write_bytes(b"untrusted cached bytes")
                with self.assertRaisesRegex(SystemExit, "Cached checksum mismatch"):
                    FETCH.fetch_fatfs()
                self.assertEqual(bad.read_bytes(), b"untrusted cached bytes")
                self.assertEqual(sorted(p.name for p in cache.iterdir()), [name])
                self.assertEqual(sentinel.read_bytes(), b"previous verified source\n")
                self.download.assert_not_called()
                bad.unlink()

    def test_missing_bundle_retains_pinned_online_fallback(self):
        shutil.rmtree(self.bundle)
        self.download.side_effect = lambda url: ORIGINAL[url.rsplit("/", 1)[-1]]
        FETCH.fetch_fatfs()
        self.generated_is_correct()
        self.assertEqual(self.download.call_args_list, [mock.call(spec["url"]) for spec in PINS])

    def test_online_pin_mismatch_does_not_populate_cache(self):
        shutil.rmtree(self.bundle)
        sentinel = self.untouched_destination()
        self.download.side_effect = lambda url: ORIGINAL[url.rsplit("/", 1)[-1]] if url.endswith(".zip") else b"substituted patch"
        with self.assertRaisesRegex(SystemExit, "Download checksum mismatch"):
            FETCH.fetch_fatfs()
        self.assertFalse((self.deps / "downloads").exists())
        self.assertEqual(sentinel.read_bytes(), b"previous verified source\n")


if __name__ == "__main__":
    unittest.main()
