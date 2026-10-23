#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent typed ELF mutations for the temporary native installer stack."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_loader_layout import EH, PH, SH, SYM
from check_vmu_native_stage_stack import HELPER, STACK_SYMBOL, STACK_BYTES, STAGE, typed_stack
from check_vmu_native_worker_gateway import read_elf
from test_vmu_native_cache_seed_artifact import change_symbol


def fixture():
    strings = b"\0"
    rows = [bytes(SYM.size)]
    labels = (("_start", STAGE, 4, 0x12, 1),
              (HELPER, STAGE + 4, 4, 0x12, 1),
              ("__retail_stage_binary_end", STAGE + 64, 0, 0x10, 1),
              ("__retail_stage_bss_begin", STAGE + 256, 0, 0x10, 2),
              ("other_typed_bss", STAGE + 256, 16, 0x11, 2),
              (STACK_SYMBOL, STAGE + 512, STACK_BYTES, 0x11, 2),
              ("__retail_stage_bss_end", STAGE + 512 + STACK_BYTES + 32, 0, 0x10, 2))
    for name, value, size, info, owner in labels:
        at = len(strings); strings += name.encode() + b"\0"
        rows.append(SYM.pack(at, value, size, info, 0, owner))
    symbols = b"".join(rows)
    raw = bytearray(bytes(0x100) + b"\x09\0" * 32)
    strings_at = len(raw); raw.extend(strings); raw.extend(bytes(-len(raw) % 4))
    symbols_at = len(raw); raw.extend(symbols); raw.extend(bytes(-len(raw) % 4))
    sections_at = len(raw)
    bss_bytes = 512 + STACK_BYTES + 32 - 256
    sections = (bytes(SH.size), SH.pack(0, 1, 6, STAGE, 0x100, 64, 0, 0, 4, 0),
                SH.pack(0, 8, 3, STAGE + 256, 0, bss_bytes, 0, 0, 32, 0),
                SH.pack(0, 2, 0, 0, symbols_at, len(symbols), 4, 1, 4, SYM.size),
                SH.pack(0, 3, 0, 0, strings_at, len(strings), 0, 0, 1, 0))
    raw.extend(b"".join(sections))
    raw[:EH.size] = EH.pack(b"\x7fELF\x01\x01\x01" + bytes(9), 2, 42, 1, STAGE,
        EH.size, sections_at, 0, EH.size, PH.size, 1, SH.size, len(sections), 0)
    raw[EH.size:EH.size + PH.size] = PH.pack(1, 0x100, STAGE, STAGE, 64, 512 + STACK_BYTES + 32, 7, 32)
    return bytes(raw)


def section_change(data, index, **changes):
    raw = bytearray(data); header = EH.unpack_from(raw)
    at = header[6] + index * SH.size; row = list(SH.unpack_from(raw, at))
    for name, value in changes.items():
        row[{"kind": 1, "flags": 2, "address": 3, "offset": 4, "bytes": 5, "alignment": 8}[name]] = value
    SH.pack_into(raw, at, *row); return bytes(raw)


def load_change(data, **changes):
    raw = bytearray(data); row = list(PH.unpack_from(raw, EH.size))
    for name, value in changes.items():
        row[{"kind": 0, "virtual": 2, "physical": 3, "file_bytes": 4, "memory_bytes": 5, "flags": 6}[name]] = value
    PH.pack_into(raw, EH.size, *row); return bytes(raw)


class StageStackArtifactTests(unittest.TestCase):
    def parsed(self, data):
        with tempfile.TemporaryDirectory(prefix="native-stack-elf-") as name:
            path = Path(name) / "fixture.elf"; path.write_bytes(data)
            return read_elf(path)

    def audit(self, data=None):
        return typed_stack(self.parsed(fixture() if data is None else data))

    def test_exact_typed_zero_filled_stack(self):
        address, size, top, first, end = self.audit()
        self.assertEqual((address, size, top), (STAGE + 512, 65536, STAGE + 512 + 65536 - 32))
        self.assertLessEqual(first, address); self.assertGreaterEqual(end, address + size)

    def test_type_size_alignment_and_bss_owner(self):
        for fields in ({"info": 0x10}, {"info": 0x12}, {"size": 0}, {"size": 65532},
                       {"size": 65540}, {"value": STAGE + 514}, {"owner": 1}):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                self.audit(change_symbol(fixture(), STACK_SYMBOL, **fields))

    def test_section_must_be_writable_zero_fill_with_alignment(self):
        for fields in ({"kind": 1}, {"flags": 2}, {"flags": 7}, {"alignment": 4},
                       {"bytes": 512}, {"address": STAGE + 1024}):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                self.audit(section_change(fixture(), 2, **fields))

    def test_complete_linked_bss_must_protect_stack(self):
        for name, value in (("__retail_stage_bss_end", STAGE + STACK_BYTES),
                            ("__retail_stage_bss_end", 0x8CFE0004),
                            ("__retail_stage_bss_begin", STAGE + 1024),
                            ("__retail_stage_binary_end", STAGE + 1024)):
            with self.subTest(symbol=name, value=value), self.assertRaises(ValueError):
                self.audit(change_symbol(fixture(), name, value=value))

    def test_load_ownership_and_zero_fill_are_independent(self):
        for fields in ({"physical": STAGE + 32}, {"flags": 5}, {"memory_bytes": 1024},
                       {"file_bytes": 1024}, {"kind": 4}):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                self.audit(load_change(fixture(), **fields))

    def test_other_typed_objects_cannot_alias_stack(self):
        for alias in (0, 0x80000000, 0xA0000000):
            changed = change_symbol(fixture(), "other_typed_bss", value=alias | 0x0CE00220)
            with self.subTest(alias=alias), self.assertRaisesRegex(ValueError, "overlaps another"):
                self.audit(changed)

    def test_helper_required_for_new_stack(self):
        changed = change_symbol(fixture(), HELPER, info=0x10)
        with self.assertRaisesRegex(ValueError, "exact live allocation helper"):
            self.audit(changed)

    def test_legacy_absence_remains_disabled_but_partial_marker_refuses(self):
        parsed = self.parsed(fixture())
        del parsed[4][STACK_SYMBOL]
        with self.assertRaisesRegex(ValueError, "lacks its typed installer stack"):
            typed_stack(parsed)
        del parsed[4][HELPER]
        self.assertIsNone(typed_stack(parsed))


if __name__ == "__main__":
    unittest.main()
