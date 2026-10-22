#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent SH fixtures reject stale and wrong-application preparation packages."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import package_vmu_persist_prep as prep
from check_loader_layout import EH, PH, SH, SYM
from runtime_package import ADDRESS, envelope, flatten_elf

COMMIT = "0123456789ab" + "c" * 28


def fixture(*, mode=1, missing=None, wrong_type=None, forbidden=None):
    payload = bytearray(256)
    payload[64:68] = struct.pack("<I", mode)
    title = b"Virtual VMU - persistence preparation\0"
    payload[80:80 + len(title)] = title
    labels = [("_start", ADDRESS, 4, 0x12, 1)]
    labels += [(name, ADDRESS + 4 + 4 * index, 4,
                0x11 if name == wrong_type else 0x12, 1)
               for index, name in enumerate(prep.REQUIRED_FUNCTIONS) if name != missing]
    labels += [(prep.MODE_SYMBOL, ADDRESS + 64, 4, 0x11, 2)]
    if forbidden:
        labels += [(forbidden, ADDRESS + 24, 4, 0x12, 1)]
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
    data[EH.size:EH.size + PH.size] = PH.pack(1, load_at, ADDRESS, ADDRESS, 64, 64, 5, 4)
    data[EH.size + PH.size:EH.size + 2 * PH.size] = PH.pack(
        1, load_at + 64, ADDRESS + 64, ADDRESS + 64, 192, 192, 4, 4)
    data[load_at:load_at + len(payload)] = payload
    data[symbol_at:symbol_at + len(symbols)] = symbols
    data[string_at:string_at + len(strings)] = strings
    sections = [bytes(SH.size), SH.pack(0, 1, 6, ADDRESS, load_at, 64, 0, 0, 4, 0),
                SH.pack(0, 1, 2, ADDRESS + 64, load_at + 64, 192, 0, 0, 4, 0),
                SH.pack(0, 2, 0, 0, symbol_at, len(symbols), 4, 1, 4, SYM.size),
                SH.pack(0, 3, 0, 0, string_at, len(strings), 0, 0, 1, 0)]
    data[section_at:] = b"".join(sections)
    return bytes(data)


class PreparationPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.guide = self.directory / "guide.md"
        self.guide.write_text("Preparation only. Install KUI/tools.kui.\n")
        self.write_fixture()

    def tearDown(self):
        self.temporary.cleanup()

    def write_fixture(self, **kwargs):
        elf = fixture(**kwargs)
        (self.directory / "vmu-harness.elf").write_bytes(elf)
        (self.directory / "prep-config").write_bytes(b"PERSIST_PREP=1\n")
        (self.directory / "vmu-harness.compile.json").write_text(json.dumps(
            {"commit": COMMIT, "source_dirty": False, "elf_sha256": hashlib.sha256(elf).hexdigest()}))
        (self.directory / "KUI").mkdir(exist_ok=True)
        payload, memory = flatten_elf(elf)
        (self.directory / "KUI/tools.kui").write_bytes(envelope(payload, memory, COMMIT[:12]))

    def inspect(self):
        return prep.inspect_preparation(self.directory, COMMIT)

    def test_complete_reproducible_package_and_checksums(self):
        result = prep.package(self.directory, COMMIT, self.guide)
        initial = Path(result["zip"]).read_bytes()
        prep.package(self.directory, COMMIT, self.guide)
        self.assertEqual(initial, Path(result["zip"]).read_bytes())
        with zipfile.ZipFile(result["zip"]) as zipped:
            names = set(zipped.namelist())
            self.assertIn("KUI/tools.kui", names)
            self.assertFalse(any("retail" in name or "boot.kui" in name for name in names))
            for row in zipped.read("SHA256SUMS.txt").decode("ascii").splitlines():
                digest, name = row.split("  ")
                self.assertEqual(digest, hashlib.sha256(zipped.read(name)).hexdigest())
            audit = json.loads(zipped.read(prep.AUDIT))
            self.assertEqual(1, audit["compile_macro"]["KUI_VMU_PERSIST_PREP"])
            self.assertFalse(audit["source_image_journal_writes_enabled"])
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

    def test_wrong_macro_and_missing_public_functions_rejected(self):
        for kwargs in ({"mode": 0}, {"missing": prep.REQUIRED_FUNCTIONS[0]},
                       {"wrong_type": prep.REQUIRED_FUNCTIONS[1]}):
            with self.subTest(kwargs=kwargs):
                self.write_fixture(**kwargs)
                with self.assertRaises(ValueError):
                    self.inspect()

    def test_legacy_writers_and_compiler_clones_rejected(self):
        for name in prep.FORBIDDEN_FUNCTIONS:
            for suffix in ("", ".constprop.0", ".isra.2"):
                with self.subTest(name=name, suffix=suffix):
                    self.write_fixture(forbidden="_" + name + suffix)
                    with self.assertRaisesRegex(ValueError, "remains linked"):
                        self.inspect()

    def test_module_self_consistent_but_wrong_build_or_payload_rejected(self):
        elf = (self.directory / "vmu-harness.elf").read_bytes()
        payload, memory = flatten_elf(elf)
        module = self.directory / "KUI/tools.kui"
        for other in (envelope(payload, memory, "f" * 12),
                      envelope(bytes([payload[0] ^ 1]) + payload[1:], memory, COMMIT[:12])):
            module.write_bytes(other)
            with self.assertRaisesRegex(ValueError, "source-bound ELF"):
                self.inspect()

    def test_wrong_config_rejected_even_with_right_marker(self):
        (self.directory / "prep-config").write_bytes(b"PERSIST_PREP=0\n")
        with self.assertRaisesRegex(ValueError, "configuration"):
            self.inspect()

    def test_failed_readmission_removes_previous_deliverables(self):
        prep.package(self.directory, COMMIT, self.guide)
        (self.directory / "prep-config").write_bytes(b"PERSIST_PREP=0\n")
        with self.assertRaisesRegex(ValueError, "configuration"):
            prep.package(self.directory, COMMIT, self.guide)
        self.assertFalse((self.directory / prep.ARCHIVE).exists())
        self.assertFalse((self.directory / "package").exists())


if __name__ == "__main__":
    unittest.main()
