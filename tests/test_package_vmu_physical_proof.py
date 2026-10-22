#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent SH fixtures exercise physical-proof identity and stale-output gates."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import package_vmu_physical_proof as proof
from check_loader_layout import EH, PH, SH, SYM
from runtime_package import ADDRESS, envelope, flatten_elf

COMMIT = "0123456789ab" + "c" * 28


def fixture(*, mode=1, missing=None, wrong_type=None, local=None, forbidden=None,
            title=proof.TITLE, writable_marker=False):
    payload = bytearray(256)
    payload[96:100] = struct.pack("<I", mode)
    payload[112:112 + len(title)] = title
    labels = [("_start", ADDRESS, 4, 0x12, 1)]
    labels += [(name, ADDRESS + 4 + 4 * index, 4,
                0x11 if name == wrong_type else (0x02 if name == local else 0x12), 1)
               for index, name in enumerate(proof.REQUIRED_FUNCTIONS) if name != missing]
    labels += [(proof.MODE_SYMBOL, ADDRESS + 96, 4,
                0x01 if local == proof.MODE_SYMBOL else 0x11, 2)]
    if forbidden:
        labels += [(forbidden, ADDRESS + 40, 4, 0x12, 1)]
    strings = bytearray(b"\0")
    symbols = bytearray(bytes(SYM.size))
    for name, address, size, info, section in labels:
        name_at = len(strings)
        strings.extend(name.encode("ascii") + b"\0")
        symbols.extend(SYM.pack(name_at, address, size, info, 0, section))
    load_at = 0x100
    symbol_at = load_at + len(payload)
    string_at = symbol_at + len(symbols)
    section_at = (string_at + len(strings) + 3) & ~3
    data = bytearray(section_at + 5 * SH.size)
    data[:EH.size] = EH.pack(b"\x7fELF\x01\x01\x01" + bytes(9), 2, 42, 1,
                            ADDRESS, EH.size, section_at, 0, EH.size,
                            PH.size, 2, SH.size, 5, 0)
    data[EH.size:EH.size + PH.size] = PH.pack(1, load_at, ADDRESS, ADDRESS, 96, 96, 5, 4)
    data[EH.size + PH.size:EH.size + 2 * PH.size] = PH.pack(
        1, load_at + 96, ADDRESS + 96, ADDRESS + 96, 160, 160,
        6 if writable_marker else 4, 4)
    data[load_at:load_at + len(payload)] = payload
    data[symbol_at:symbol_at + len(symbols)] = symbols
    data[string_at:string_at + len(strings)] = strings
    sections = [bytes(SH.size), SH.pack(0, 1, 6, ADDRESS, load_at, 96, 0, 0, 4, 0),
                SH.pack(0, 1, 2, ADDRESS + 96, load_at + 96, 160, 0, 0, 4, 0),
                SH.pack(0, 2, 0, 0, symbol_at, len(symbols), 4, 1, 4, SYM.size),
                SH.pack(0, 3, 0, 0, string_at, len(strings), 0, 0, 1, 0)]
    data[section_at:] = b"".join(sections)
    return bytes(data)


class PhysicalProofPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.guide = self.directory / "guide.md"
        self.guide.write_text("Physical journal proof. Install KUI/tools.kui.\n")
        self.write_fixture()

    def tearDown(self):
        self.temporary.cleanup()

    def write_fixture(self, **kwargs):
        elf = fixture(**kwargs)
        (self.directory / "vmu-harness.elf").write_bytes(elf)
        (self.directory / "prep-config").write_bytes(b"PERSIST_PROOF=1\n")
        (self.directory / "vmu-harness.compile.json").write_text(json.dumps(
            {"commit": COMMIT, "source_dirty": False, "elf_sha256": hashlib.sha256(elf).hexdigest()}))
        (self.directory / "KUI").mkdir(exist_ok=True)
        payload, memory = flatten_elf(elf)
        (self.directory / "KUI/tools.kui").write_bytes(envelope(payload, memory, COMMIT[:12]))

    def inspect(self):
        return proof.inspect_physical_proof(self.directory, COMMIT)

    def test_complete_reproducible_package_and_checksums(self):
        result = proof.package(self.directory, COMMIT, self.guide)
        initial = Path(result["zip"]).read_bytes()
        proof.package(self.directory, COMMIT, self.guide)
        self.assertEqual(initial, Path(result["zip"]).read_bytes())
        with zipfile.ZipFile(result["zip"]) as zipped:
            modules = [name for name in zipped.namelist() if name.endswith(".kui")]
            self.assertEqual(["KUI/tools.kui"], modules)
            for row in zipped.read("SHA256SUMS.txt").decode("ascii").splitlines():
                digest, name = row.split("  ")
                self.assertEqual(digest, hashlib.sha256(zipped.read(name)).hexdigest())
            audit = json.loads(zipped.read(proof.AUDIT))
            self.assertEqual(1, audit["compile_macro"]["KUI_VMU_PERSIST_PROOF"])
            self.assertTrue(audit["source_image_journal_writes_enabled"])
            self.assertFalse(audit["retail_sd_saving_enabled"])
            self.assertFalse(audit["filesystem_metadata_writes_enabled"])
            self.assertEqual(COMMIT + "\n", zipped.read("SOURCE-COMMIT.txt").decode())

    def test_dirty_stale_and_changed_elf_rejected(self):
        provenance = self.directory / "vmu-harness.compile.json"
        original = json.loads(provenance.read_text())
        for key, value in (("source_dirty", True), ("source_dirty", 0),
                           ("commit", "f" * 40), ("elf_sha256", "0" * 64)):
            with self.subTest(key=key, value=value):
                provenance.write_text(json.dumps({**original, key: value}))
                with self.assertRaisesRegex(ValueError, "compile provenance"):
                    self.inspect()
        provenance.write_text("[]")
        with self.assertRaisesRegex(ValueError, "compile provenance"):
            self.inspect()

    def test_mode_marker_must_be_one_readonly_global_object(self):
        for kwargs in ({"mode": 0}, {"local": proof.MODE_SYMBOL}, {"writable_marker": True}):
            with self.subTest(kwargs=kwargs):
                self.write_fixture(**kwargs)
                with self.assertRaises(ValueError):
                    self.inspect()

    def test_every_required_public_api_is_typed_and_global(self):
        for name in proof.REQUIRED_FUNCTIONS:
            for key in ("missing", "wrong_type", "local"):
                with self.subTest(name=name, key=key):
                    self.write_fixture(**{key: name})
                    with self.assertRaises(ValueError):
                        self.inspect()

    def test_format_and_synthetic_harness_compiler_clones_rejected(self):
        for name in proof.FORBIDDEN_FUNCTIONS:
            for suffix in ("", ".constprop.0", ".isra.2"):
                with self.subTest(name=name, suffix=suffix):
                    self.write_fixture(forbidden="_" + name + suffix)
                    with self.assertRaisesRegex(ValueError, "remains linked"):
                        self.inspect()

    def test_valid_envelope_from_another_build_or_elf_rejected(self):
        elf = (self.directory / "vmu-harness.elf").read_bytes()
        payload, memory = flatten_elf(elf)
        module = self.directory / "KUI/tools.kui"
        for other in (envelope(payload, memory, "f" * 12),
                      envelope(bytes([payload[0] ^ 1]) + payload[1:], memory, COMMIT[:12])):
            module.write_bytes(other)
            with self.assertRaisesRegex(ValueError, "source-bound ELF"):
                self.inspect()

    def test_preparation_app_config_and_title_cannot_be_relabelled(self):
        (self.directory / "prep-config").write_bytes(b"PERSIST_PREP=1\n")
        with self.assertRaisesRegex(ValueError, "configuration"):
            self.inspect()
        self.write_fixture(title=b"Virtual VMU - persistence preparation\0")
        with self.assertRaisesRegex(ValueError, "application identity"):
            self.inspect()

    def test_failed_readmission_removes_previous_deliverables(self):
        proof.package(self.directory, COMMIT, self.guide)
        (self.directory / "prep-config").write_bytes(b"PERSIST_PROOF=0\n")
        with self.assertRaises(ValueError):
            proof.package(self.directory, COMMIT, self.guide)
        self.assertFalse((self.directory / proof.ARCHIVE).exists())
        self.assertFalse((self.directory / "package").exists())

    def test_unsafe_output_symlink_does_not_touch_target(self):
        target = self.directory / "preserved"
        target.mkdir()
        (target / "keep").write_text("keep")
        (self.directory / "package").symlink_to(target, target_is_directory=True)
        with self.assertRaises(ValueError):
            proof.package(self.directory, COMMIT, self.guide)
        self.assertEqual("keep", (target / "keep").read_text())


if __name__ == "__main__":
    unittest.main()
