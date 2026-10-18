#!/usr/bin/env python3
"""Verify the CHD source pin and complete binary-distribution license notices."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CHDDependencies(unittest.TestCase):
    def test_pinned_vendor_tree_and_original_security_patch_hashes(self):
        record = json.loads((ROOT / "dependencies.json").read_text())["libchdr"]
        self.assertEqual(record["commit"], "47d794151b2b64f6aba60304e6480f4f5a2bc24b")
        vendor = ROOT / record["path"]
        actual = {path.relative_to(vendor).as_posix():
                  hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in vendor.rglob("*") if path.is_file()}
        self.assertEqual(actual, record["files_sha256"])
        for name, digest in record["upstream_files_sha256"].items():
            if name not in record["adapted_files"]:
                self.assertEqual(actual[name], digest, name)
        self.assertTrue(record["adaptations"])

    def test_notices_include_full_mit_bsd_and_unlicense_text(self):
        spec = importlib.util.spec_from_file_location("chd_notices", ROOT / "tools/chd_notices.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as temporary:
            module.write_chd_codec_notices(Path(temporary))
            folder = Path(temporary) / "CHD"
            self.assertEqual((folder / "libchdr-LICENSE.txt").read_bytes(),
                             (ROOT / "third_party/libchdr/LICENSE.txt").read_bytes())
            self.assertEqual((folder / "Zstandard-LICENSE.txt").read_bytes(),
                             (ROOT / "third_party/libchdr/deps/zstd-1.5.7/LICENSE").read_bytes())
            text = (folder / "UPSTREAM-NOTICES.txt").read_text()
            for token in ("Aaron Giles", "David Reid", "Martin Raiber", "Meta Platforms",
                          "MIT No Attribution", "Permission is hereby granted",
                          "free and unencumbered", "Redistribution and use"):
                self.assertIn(token, text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
