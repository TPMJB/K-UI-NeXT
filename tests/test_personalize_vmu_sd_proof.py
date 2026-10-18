#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise profile-only patching with independently constructed SH ELF files."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_loader_layout import EH, PH, SH, SYM, inspect_elf
from personalize_vmu_sd_proof import MODULE, personalize, validate_personalized
from runtime_package import ADDRESS, envelope, flatten_elf, verify
from vmu_sd_profile import template_profile

COMMIT = "0123456789abcdef0123456789abcdef01234567"
STAGE = 0x8CE00000


def digest(data):
    return hashlib.sha256(data).hexdigest()


def elf_fixture(base, payload, labels, *, readonly_flags=2):
    """One PT_LOAD, distinct code/RO sections, and an ordinary static symtab."""
    strings = b"\0_start\0"
    symbol_rows = [(1, base, 4, 0x12, 0, 1)]
    for label, offset in labels:
        at = len(strings)
        strings += label.encode() + b"\0"
        symbol_rows.append((at, base + offset, 0, 0x10, 0, 2))
    symbols = bytes(SYM.size) + b"".join(SYM.pack(*row) for row in symbol_rows)
    body = bytearray(bytes(0x100) + payload)
    names_offset = len(body)
    body.extend(strings)
    body.extend(bytes(-len(body) % 4))
    symbols_offset = len(body)
    body.extend(symbols)
    body.extend(bytes(-len(body) % 4))
    sections_offset = len(body)
    sections = [bytes(SH.size),
                SH.pack(0, 1, 6, base, 0x100, 64, 0, 0, 4, 0),
                SH.pack(0, 1, readonly_flags, base + 64, 0x140, len(payload) - 64, 0, 0, 4, 0),
                SH.pack(0, 2, 0, 0, symbols_offset, len(symbols), 4, 1, 4, SYM.size),
                SH.pack(0, 3, 0, 0, names_offset, len(strings), 0, 0, 1, 0)]
    body.extend(b"".join(sections))
    body[:EH.size] = EH.pack(b"\x7fELF\x01\x01\x01" + bytes(9), 2, 42, 1, base,
        EH.size, sections_offset, 0, EH.size, PH.size, 1, SH.size, len(sections), 0)
    body[EH.size:EH.size + PH.size] = PH.pack(1, 0x100, base, base, len(payload), len(payload), 7, 4)
    return bytes(body)


def owner_profile():
    data = bytearray(template_profile())
    data[56:72] = bytes(range(1, 17))
    data[72:88] = bytes(range(17, 33))
    data[88:120] = bytes(range(33, 65))
    struct.pack_into("<I", data, 16, 0)
    struct.pack_into("<I", data, 16, zlib.crc32(data))
    return bytes(data)


def build_fixture(base, *, entry_bin=True, profile_flags=2):
    base.mkdir()
    wire = template_profile()
    stage = b"\x09\x00" * 32 + wire + b"\x77\x88\x99\xaa" * 64
    stage_elf = elf_fixture(STAGE, stage,
        [("__vmu_sd_profile_blob_start", 64), ("__vmu_sd_profile_blob_end", 1600)],
        readonly_flags=profile_flags)
    entry = b"\x09\x00" * 32 + bytes(0x2000 - 64) + stage
    entry_elf = elf_fixture(ADDRESS, entry,
        [("__retail_stage_blob_start", 0x2000),
         ("__retail_stage_blob_end", len(entry))])
    payload, memory = flatten_elf(entry_elf)
    files = {"stage.elf": stage_elf, "stage.bin": stage,
             "entry.elf": entry_elf, "vmu-sd-profile.bin": wire,
             MODULE: envelope(payload, memory, COMMIT[:12]),
             "entry.compile.json": (json.dumps(dict(commit=COMMIT, source_dirty=False,
                elf_sha256=digest(entry_elf)), indent=2) + "\n").encode(),
             "worker.bin": struct.pack("<16I", 0x564D4F31, 5, 64, *([0] * 11), 2, 0),
             "worker.elf": b"independent-worker-proof-fixture",
             "worker/frame.su": b"worker.c:1:1:function\t32\tstatic\n",
             "resident/lto/graph.ci": b"retained final compiler graph\n",
             "resident-sci.map": b"retained link map\n",
             "observer-artifact-audit.json": b'{"template_audit":true}\n',
             "old.zip": b"do not copy old test package",
             "stage.dis": b"do not copy old disassembly"}
    if entry_bin:
        files["entry.bin"] = entry
    for name, data in files.items():
        path = base / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    return files


class PersonalizationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.base = self.root / "base"
        self.output = self.root / "personalized"
        self.files = build_fixture(self.base)
        self.profile = self.root / "owner.bin"
        self.profile.write_bytes(owner_profile())

    def run_personalize(self):
        return personalize(self.base, self.profile, self.output, expected_commit=COMMIT)

    def test_profile_only_round_trip_and_unmodified_base(self):
        report = self.run_personalize()
        self.assertEqual(report["source_commit"], COMMIT)
        self.assertEqual(report["profile_address"], "0x8ce00040")
        self.assertEqual(report["profile_sha256"], digest(owner_profile()))
        self.assertEqual(set(report["artifacts"]),
            {"stage.elf", "stage.bin", "entry.elf", "entry.bin", "vmu-sd-profile.bin", MODULE})
        self.assertFalse((self.output / "entry.compile.json").exists())
        for name, original in self.files.items():
            self.assertEqual((self.base / name).read_bytes(), original)
        for name in ("stage.elf", "entry.elf", MODULE, "vmu-sd-profile.bin", "entry.compile.json"):
            self.assertEqual((self.output / "base-provenance" / name).read_bytes(), self.files[name])
        self.assertEqual((self.output / "base-provenance/observer-artifact-audit.json").read_bytes(),
                         self.files["observer-artifact-audit.json"])
        for name in ("old.zip", "stage.dis", "observer-artifact-audit.json"):
            self.assertFalse((self.output / name).exists())
        stage = inspect_elf((self.output / "stage.elf").read_bytes(), STAGE, 0x8CFE0000)
        self.assertEqual(stage["payload"], (self.output / "stage.bin").read_bytes())
        self.assertEqual(stage["payload"][64:1600], owner_profile())
        payload, memory = flatten_elf((self.output / "entry.elf").read_bytes())
        module = (self.output / MODULE).read_bytes()
        self.assertEqual(module, envelope(payload, memory, COMMIT[:12]))
        self.assertEqual(verify(module)["build"], COMMIT[:12])
        self.assertEqual(payload[0x2040:0x2640], owner_profile())
        self.assertEqual((self.output / "entry.bin").read_bytes(), payload)
        for name, row in report["artifacts"].items():
            before, after = self.files[name], (self.output / name).read_bytes()
            self.assertEqual(row["base_sha256"], digest(before))
            self.assertEqual(row["personalized_sha256"], digest(after))
            changed = {i for i, (left, right) in enumerate(zip(before, after)) if left != right}
            declared = {i for span in row["changed_ranges"]
                        for i in range(span["offset"], span["offset"] + span["bytes"])}
            allowed = {i for span in row["allowed_ranges"]
                       for i in range(span["offset"], span["offset"] + span["bytes"])}
            self.assertEqual(changed, declared)
            self.assertLessEqual(changed, allowed)
        for name, expected in report["unchanged_artifacts"].items():
            self.assertEqual(digest((self.output / name).read_bytes()), expected)
        self.assertNotIn("observer-artifact-audit.json", report["unchanged_artifacts"])
        self.assertEqual(json.loads((self.output / "personalization-audit.json").read_text()), report)
        self.assertEqual(validate_personalized(self.output), report)

    def test_without_optional_entry_binary(self):
        (self.base / "entry.bin").unlink()
        self.assertNotIn("entry.bin", self.run_personalize()["artifacts"])

    def test_refuses_invalid_or_template_owner_profile(self):
        for data in (template_profile(), owner_profile()[:-1], bytes(1536)):
            self.profile.write_bytes(data)
            with self.assertRaises(ValueError):
                self.run_personalize()
            self.assertFalse(self.output.exists())

    def test_refuses_dirty_wrong_or_stale_compile_provenance(self):
        record = json.loads(self.files["entry.compile.json"])
        for field, value in (("source_dirty", True), ("source_dirty", 0),
                             ("elf_sha256", "0" * 64), ("commit", "f" * 40)):
            changed = dict(record, **{field: value})
            (self.base / "entry.compile.json").write_text(json.dumps(changed))
            with self.assertRaises(ValueError):
                self.run_personalize()
            self.assertFalse(self.output.exists())

    def test_refuses_mismatched_stage_runtime_entry_and_profile(self):
        for name in ("stage.bin", MODULE, "entry.bin", "vmu-sd-profile.bin", "entry.elf"):
            before = self.files[name]
            changed = bytearray(before)
            changed[-1] ^= 1
            (self.base / name).write_bytes(changed)
            with self.assertRaises(ValueError, msg=name):
                self.run_personalize()
            self.assertFalse(self.output.exists())
            (self.base / name).write_bytes(before)

    def test_refuses_executable_or_writable_profile_section(self):
        for flags in (3, 6, 7):
            other = self.root / ("flags" + str(flags))
            build_fixture(other, profile_flags=flags)
            with self.assertRaises(ValueError):
                personalize(other, self.profile, self.output, expected_commit=COMMIT)
            self.assertFalse(self.output.exists())

    def test_refuses_wrong_worker_mode_existing_output_and_symlinks(self):
        wrong = bytearray(self.files["worker.bin"])
        struct.pack_into("<I", wrong, 56, 1)
        (self.base / "worker.bin").write_bytes(wrong)
        with self.assertRaises(ValueError):
            self.run_personalize()
        (self.base / "worker.bin").write_bytes(self.files["worker.bin"])
        (self.base / "linked").symlink_to(self.profile)
        with self.assertRaises(ValueError):
            self.run_personalize()
        (self.base / "linked").unlink()
        self.output.mkdir()
        with self.assertRaises(ValueError):
            self.run_personalize()

    def test_refuses_second_personalization_and_input_nested_output(self):
        self.run_personalize()
        with self.assertRaises(ValueError):
            personalize(self.output, self.profile, self.root / "twice", expected_commit=COMMIT)
        with self.assertRaises(ValueError):
            personalize(self.base, self.profile, self.base / "nested", expected_commit=COMMIT)

    def test_validator_rederives_report_instead_of_trusting_claims(self):
        original = self.run_personalize()
        audit = self.output / "personalization-audit.json"
        mutations = [lambda r: r.update(format=True),
                     lambda r: r.update(source_commit="f" * 40),
                     lambda r: r.update(profile_address="0x8ce00044"),
                     lambda r: r["artifacts"]["stage.elf"].update(
                         allowed_ranges=[dict(offset=0, bytes=1000000)]),
                     lambda r: r["artifacts"]["entry.elf"].update(changed_ranges=[]),
                     lambda r: r["base_compile_provenance"].update(name="../../entry.compile.json")]
        for mutation in mutations:
            changed = json.loads(json.dumps(original))
            mutation(changed)
            audit.write_text(json.dumps(changed))
            with self.assertRaises(ValueError):
                validate_personalized(self.output)
        audit.write_text(json.dumps(original))
        self.assertEqual(validate_personalized(self.output), original)

    def test_validator_refuses_instruction_change_even_with_updated_hash_claim(self):
        report = self.run_personalize()
        artifact = self.output / "stage.elf"
        changed = bytearray(artifact.read_bytes())
        changed[0x100] ^= 1  # Executable .text, outside the profile section.
        artifact.write_bytes(changed)
        report["artifacts"]["stage.elf"]["personalized_sha256"] = digest(changed)
        report["artifacts"]["stage.elf"]["changed_ranges"].append(dict(offset=0x100, bytes=1))
        report["artifacts"]["stage.elf"]["allowed_ranges"].append(dict(offset=0x100, bytes=1))
        (self.output / "personalization-audit.json").write_text(json.dumps(report))
        with self.assertRaises(ValueError):
            validate_personalized(self.output)

    def test_validator_refuses_proof_asset_change_even_with_updated_hash_claim(self):
        report = self.run_personalize()
        name = "resident/lto/graph.ci"
        changed = b"forged stack/ownership proof\n"
        (self.output / name).write_bytes(changed)
        report["unchanged_artifacts"][name] = digest(changed)
        (self.output / "personalization-audit.json").write_text(json.dumps(report))
        with self.assertRaises(ValueError):
            validate_personalized(self.output)

    def test_validator_refuses_base_or_current_bytes_tampering(self):
        self.run_personalize()
        for name in (MODULE, "stage.bin", "entry.elf", "vmu-sd-profile.bin",
                     "base-provenance/entry.elf", "base-provenance/stage.elf",
                     "base-provenance/entry.compile.json"):
            path = self.output / name
            before = path.read_bytes()
            changed = bytearray(before)
            changed[-1] ^= 1
            path.write_bytes(changed)
            with self.assertRaises((ValueError, UnicodeError), msg=name):
                validate_personalized(self.output)
            path.write_bytes(before)

    def test_validator_allows_regenerated_reports_but_refuses_false_compile_record(self):
        report = self.run_personalize()
        for name in ("observer-artifact-audit.json", "stage.dis", "new-test.zip"):
            (self.output / name).write_bytes(b"regenerated package evidence")
        self.assertEqual(validate_personalized(self.output), report)
        (self.output / "entry.compile.json").write_bytes(self.files["entry.compile.json"])
        with self.assertRaises(ValueError):
            validate_personalized(self.output)


if __name__ == "__main__":
    unittest.main()
