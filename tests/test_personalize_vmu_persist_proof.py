#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independently construct SH mode4 images and prove P2-only derivation."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_loader_layout import inspect_elf
from package_vmu_maple_observe import (inspect_worker, inspect_sd_profile, inspect_mode4_source,
                                      write_test_archive)
from personalize_vmu_persist_proof import personalize, validate_personalized
from runtime_package import ADDRESS, envelope, flatten_elf
from vmu_persist_profile import decode_profile, encode_profile, template_profile
from test_personalize_vmu_sd_proof import elf_fixture, COMMIT, STAGE, digest
from test_vmu_maple_observe_package import worker


def owner_profile():
    p = decode_profile(template_profile())
    p["cid"] = bytes(range(1, 17)).hex()
    p["image_id"] = bytes(range(17, 33)).hex()
    p["identity_sha256"] = hashlib.sha256(bytes(range(17, 33))).hexdigest()
    return encode_profile(p)


def build_fixture(base):
    base.mkdir()
    wire = template_profile()
    worker_elf = bytes(worker(4))
    worker_bin = inspect_elf(worker_elf, 0x8cfd0000, 0x8cfe0000, private_p2=True,
        entry_symbol="_kui_vmu_maple_observer_init", entry_at_base=False)["payload"]
    resident_bin = b"\x09\x00" * 64
    resident_elf = elf_fixture(0x8c004000, resident_bin, [])
    stage = b"\x09\x00" * 32 + wire
    labels = [("__vmu_sd_profile_blob_start", 64), ("__vmu_sd_profile_blob_end", len(stage))]
    for transport in ("scif", "sci", "ide", "scia"):
        prefix = "__retail_resident_" + transport + "_blob_"
        labels.append((prefix + "start", len(stage)))
        stage += resident_bin
        labels.append((prefix + "end", len(stage)))
    labels.append(("__vmu_observe_worker_blob_start", len(stage)))
    stage += worker_bin
    labels.append(("__vmu_observe_worker_blob_end", len(stage)))
    stage_elf = elf_fixture(STAGE, stage, labels)
    entry = b"\x09\x00" * 32 + bytes(0x2000 - 64) + stage
    entry_elf = elf_fixture(ADDRESS, entry,
        [("__retail_stage_blob_start", 0x2000), ("__retail_stage_blob_end", len(entry))])
    payload, memory = flatten_elf(entry_elf)
    module = envelope(payload, memory, COMMIT[:12])
    files = {"stage.elf": stage_elf, "stage.bin": stage, "entry.elf": entry_elf,
        "entry.bin": entry, "vmu-sd-profile.bin": wire,
        "retail-maple-observe.kui": module,
        "entry.compile.json": json.dumps(dict(commit=COMMIT, source_dirty=False,
            elf_sha256=digest(entry_elf))).encode(),
        "worker.elf": worker_elf, "worker.bin": worker_bin,
        "resident-sci.elf": resident_elf, "resident-sci.bin": resident_bin,
        "worker/native.su": b"unit.c:1:1:kui_vmu_durable_transaction\t64\tstatic\n",
        "resident/lto/native.ci": b"preserved native compiler report\n"}
    for name, contents in files.items():
        target = base / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(contents)
    audit = inspect_worker(base)
    audit.update(source_commit=COMMIT, module_sha256=digest(module), sd_profile_sha256=digest(wire))
    audit["sd_profile"] = inspect_sd_profile(base,
        inspect_elf(stage_elf, STAGE, 0x8cfe0000), 4)
    files["observer-artifact-audit.json"] = json.dumps(audit).encode()
    (base / "observer-artifact-audit.json").write_bytes(files["observer-artifact-audit.json"])
    return files


class PersistentPersonalizationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.base, self.output = self.root / "base", self.root / "personalized"
        for name in ("check_hook", "check_completion_hook"):
            guard = patch("package_vmu_maple_observe." + name, return_value={})
            guard.start()
            self.addCleanup(guard.stop)
        self.files = build_fixture(self.base)
        self.profile = self.root / "owner.bin"
        self.profile.write_bytes(owner_profile())

    def run_personalize(self):
        return personalize(self.base, self.profile, self.output, expected_commit=COMMIT)

    def test_canonical_public_template_is_stable_distinct_and_non_owner(self):
        wire = template_profile()
        self.assertEqual(len(wire), 4096)
        self.assertEqual(digest(wire), "147eb84a549e91b8121f363980f601bd2d80212c880f24ffeaa4fb180b6e9860")
        self.assertEqual(decode_profile(wire)["cid"], b"KUI-P2-TEMPLATE!".hex())
        self.profile.write_bytes(wire)
        with self.assertRaisesRegex(ValueError, "not an owner"):
            self.run_personalize()

    def test_only_exact_p2_and_crc_spans_change_with_independent_provenance(self):
        report = self.run_personalize()
        self.assertEqual(report["proof_mode"], 4)
        self.assertEqual(report["profile_sha256"], digest(owner_profile()))
        self.assertEqual(validate_personalized(self.output), report)
        self.assertFalse((self.output / "entry.compile.json").exists())
        self.assertEqual((self.output / report["base_package_audit"]["name"]).read_bytes(),
                         self.files["observer-artifact-audit.json"])
        for name, before in self.files.items():
            self.assertEqual((self.base / name).read_bytes(), before)
        for name, row in report["artifacts"].items():
            before, after = self.files[name], (self.output / name).read_bytes()
            self.assertEqual(len(before), len(after))
            changed = {n for n, (a, b) in enumerate(zip(before, after)) if a != b}
            actual = {n for span in row["changed_ranges"]
                      for n in range(span["offset"], span["offset"] + span["bytes"])}
            self.assertEqual(changed, actual)
            self.assertEqual(row["personalized_sha256"], digest(after))
            self.assertTrue(all(span["bytes"] == 4096 for span in row["allowed_ranges"]
                                if span["bytes"] != 4))
        stage = inspect_elf((self.output / "stage.elf").read_bytes(), STAGE, 0x8cfe0000)
        self.assertEqual(inspect_sd_profile(self.output, stage, 4)["wire_sha256"], digest(owner_profile()))
        entry = inspect_elf((self.output / "entry.elf").read_bytes(), ADDRESS, 0x8c112000)
        result = inspect_mode4_source(self.output, entry,
                                     (self.output / "retail-maple-observe.kui").read_bytes())
        self.assertEqual(result["source_commit"], COMMIT)

    def test_instruction_edit_and_widened_json_permission_are_rejected(self):
        report = self.run_personalize()
        module = self.output / "retail-maple-observe.kui"
        contents = bytearray(module.read_bytes())
        contents[64] ^= 1
        module.write_bytes(contents)
        changed = json.loads(json.dumps(report))
        changed["artifacts"]["retail-maple-observe.kui"]["personalized_sha256"] = digest(contents)
        changed["artifacts"]["retail-maple-observe.kui"]["allowed_ranges"] = [{"offset": 0, "bytes": len(contents)}]
        (self.output / "personalization-audit.json").write_text(json.dumps(changed))
        with self.assertRaises(ValueError):
            validate_personalized(self.output)

    def test_base_provenance_and_exact_mode_cannot_be_relabelled(self):
        audit = self.base / "observer-artifact-audit.json"
        record = json.loads(audit.read_bytes())
        record["source_commit"] = "f" * 40
        audit.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, "package audit"):
            self.run_personalize()
        audit.write_bytes(self.files["observer-artifact-audit.json"])
        for mode in (2, 3, True, "4"):
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                personalize(self.base, self.profile, self.output,
                            expected_commit=COMMIT, expected_mode=mode)

    def test_generic_source_provenance_is_clean_and_bound_to_actual_entry(self):
        entry = inspect_elf(self.files["entry.elf"], ADDRESS, 0x8c112000)
        module = self.files["retail-maple-observe.kui"]
        self.assertEqual(inspect_mode4_source(self.base, entry, module)["source_commit"], COMMIT)
        record = json.loads(self.files["entry.compile.json"])
        record["source_dirty"] = True
        (self.base / "entry.compile.json").write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, "clean source"):
            inspect_mode4_source(self.base, entry, module)

    def test_personalized_archive_binds_mode_module_and_preserved_native_records(self):
        report = self.run_personalize()
        module = (self.output / "retail-maple-observe.kui").read_bytes()
        target = write_test_archive(self.output, module, b"{}", persistent_proof=True)
        with zipfile.ZipFile(target) as archive:
            self.assertEqual(archive.read("KUI/apps/games/retail-boot.kui"), module)
            self.assertEqual(json.loads(archive.read("personalization-audit.json")), report)
            self.assertEqual(archive.read(report["base_compile_provenance"]["name"]),
                             self.files["entry.compile.json"])
            self.assertEqual(archive.read(report["base_package_audit"]["name"]),
                             self.files["observer-artifact-audit.json"])
        with self.assertRaises(ValueError):
            write_test_archive(self.output, module, b"{}", volatile_proof=True)
        with self.assertRaises(ValueError):
            write_test_archive(self.output, module[:64] + bytes([module[64] ^ 1]) + module[65:],
                               b"{}", persistent_proof=True)

    def test_embedded_worker_and_resident_binding_is_independent_of_audit_hashes(self):
        path = self.base / "resident-sci.bin"
        path.write_bytes(bytes(128))
        with self.assertRaisesRegex(ValueError, "resident binary"):
            self.run_personalize()

    def test_reserved_profile_bytes_and_wrong_base_template_fail(self):
        wire = bytearray(owner_profile())
        wire[-1] = 1
        self.profile.write_bytes(wire)
        with self.assertRaises(ValueError):
            self.run_personalize()
        self.profile.write_bytes(owner_profile())
        (self.base / "vmu-sd-profile.bin").write_bytes(owner_profile())
        with self.assertRaisesRegex(ValueError, "deterministic"):
            self.run_personalize()


if __name__ == "__main__":
    unittest.main()
