# SPDX-License-Identifier: GPL-3.0-only
"""Native release adapters retain clean source identity and linked parity proof."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import package
from retail_package import HEADER_BYTES, HEADER_OFFSET, STAGE_BLOB_OFFSET, relocation_header
from runtime_package import ADDRESS, envelope
from test_image_loader_layout import executable


class NativeMusicReleasePackaging(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.games = self.root / "dist/sd/KUI/apps/games"
        self.games.mkdir(parents=True)
        self.commit = "0123456789abcdef" * 2 + "01234567"
        payload = bytearray(STAGE_BLOB_OFFSET + 32)
        payload[HEADER_OFFSET:HEADER_OFFSET + HEADER_BYTES] = relocation_header(32)
        payload[-32:-19] = self.commit[:12].encode() + b"\0"
        self.payload = bytes(payload)
        for profile, (_, package_name) in package.NATIVE_CDDA_PROFILES.items():
            for mode in ("compat", "reference"):
                directory = self.root / "build" / ("native-" + mode + "-" + profile)
                directory.mkdir(parents=True)
                elf = directory / "entry.elf"
                elf.write_bytes(executable(ADDRESS, payload, len(payload), {}))
                self.provenance(elf)
                (directory / package_name).write_bytes(envelope(payload, len(payload), self.commit[:12]))
        root_patch = patch("package.ROOT", self.root)
        root_patch.start()
        self.addCleanup(root_patch.stop)
        audit_patch = patch("check_native_cdda_compat.check_directory", side_effect=self.audit)
        self.auditor = audit_patch.start()
        self.addCleanup(audit_patch.stop)

    def provenance(self, elf, **changes):
        record = {"commit": self.commit, "source_dirty": False,
                  "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest()}
        record.update(changes)
        elf.with_suffix(".compile.json").write_text(json.dumps(record))

    def audit(self, directory, profile, reference):
        self.assertEqual(reference, self.root / "build" / ("native-reference-" + profile))
        return {"profile": profile, "experimental": True, "console_tested": False,
                "package": {"build": self.commit[:12]},
                "unchanged_reference_playback_sha256": {"worker": "fixture", "resident-sci": "fixture"}}

    def build(self):
        return package.write_native_cdda_modules(self.games, self.root / "dist", self.commit)

    def test_all_three_fresh_adapters_install_with_reference_provenance_and_experimental_proof(self):
        records = self.build()
        self.assertEqual(set(records), {"toy-raw", "toy-cooked", "mdk2"})
        self.assertEqual(self.auditor.call_count, 3)
        for profile, record in records.items():
            name, package_name = package.NATIVE_CDDA_PROFILES[profile]
            original = self.root / "build" / ("native-compat-" + profile) / package_name
            self.assertEqual((self.games / name).read_bytes(), original.read_bytes())
            self.assertEqual(record["sha256"], hashlib.sha256(original.read_bytes()).hexdigest())
            self.assertEqual(record["compile"]["commit"], self.commit)
            self.assertEqual(record["reference_compile"]["commit"], self.commit)
            self.assertTrue(record["experimental"])
            self.assertFalse(record["new_binary_hardware_tested"])
            evidence = self.root / "dist" / ("native-cdda-" + profile + "-build")
            self.assertEqual(json.loads((evidence / "linked-audit.json").read_text()), record["linked_audit"])
            self.assertTrue((evidence / "reference/entry.compile.json").is_file())
        self.assertFalse((self.games / "retail-toy-pilot.kui").exists())
        self.assertFalse((self.games / "retail-mdk2-cdda.kui").exists())

    def test_dirty_foreign_or_modified_current_and_reference_builds_are_refused(self):
        for mode in ("compat", "reference"):
            elf = self.root / "build" / ("native-" + mode + "-toy-raw") / "entry.elf"
            for changes in ({"source_dirty": True}, {"commit": "f" * 40}, {"elf_sha256": "0" * 64}):
                with self.subTest(mode=mode, changes=changes):
                    self.provenance(elf, **changes)
                    with self.assertRaisesRegex(ValueError, "provenance"):
                        self.build()
            self.provenance(elf)

    def test_stale_package_and_failed_or_wrong_linked_audit_are_refused(self):
        path = self.root / "build/native-compat-toy-raw/retail-toy-pilot.kui"
        original = path.read_bytes()
        path.write_bytes(envelope(self.payload, len(self.payload), "ffffffffffff"))
        with self.assertRaisesRegex(ValueError, "mismatch"):
            self.build()
        path.write_bytes(original)
        self.auditor.side_effect = ValueError("reader parity failed")
        with self.assertRaisesRegex(ValueError, "reader parity failed"):
            self.build()
        for key, value in (("profile", "mdk2"), ("experimental", False), ("console_tested", True)):
            with self.subTest(field=key):
                audit = copy.deepcopy(self.audit(None, "toy-raw", self.root / "build/native-reference-toy-raw"))
                audit[key] = value
                self.auditor.side_effect = None
                self.auditor.return_value = audit
                with self.assertRaisesRegex(ValueError, "linked audit differs"):
                    self.build()


if __name__ == "__main__":
    unittest.main()
