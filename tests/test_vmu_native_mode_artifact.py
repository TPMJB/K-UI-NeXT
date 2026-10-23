#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent ELF, template and embedding mutations for native mode selection."""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_loader_layout import EH, PH, SH, inspect_elf
from check_vmu_native_mode import audit, template_mode, generic_mode, START, END, HELPER, DECODER, STAGE, MODE_BYTES
from runtime_package import ADDRESS, envelope
from test_personalize_vmu_sd_proof import elf_fixture
from test_vmu_native_cache_seed_artifact import change_symbol, hide_symbol

MODE_AT = 512
LABELS = [("__native_worker_blob_begin", 64), ("__native_worker_blob_end", 192),
          ("__native_worker_reloc_begin", 192), ("__native_worker_reloc_end", 256),
          ("__retail_trampoline_start", 256), ("__retail_trampoline_end", 384),
          (START, MODE_AT), (END, MODE_AT + MODE_BYTES),
          ("__native_resolution_cache_seed_start", 640), ("__native_resolution_cache_seed_end", 1760),
          ("__vmu_sd_profile_blob_start", 2048), ("__vmu_sd_profile_blob_end", 6144),
          (HELPER, 4), (DECODER, 8)]


def stage_fixture(*, duplicate=False, flags=2):
    payload = bytearray(b"\x09\x00" * 3200)
    payload[MODE_AT:MODE_AT + MODE_BYTES] = template_mode()
    if duplicate:
        payload.extend(template_mode())
    raw = elf_fixture(STAGE, bytes(payload), LABELS, readonly_flags=flags)
    raw = change_symbol(raw, START, size=MODE_BYTES, info=0x11)
    for name in (HELPER, DECODER):
        raw = change_symbol(raw, name, size=4, info=0x12, owner=1)
    return raw


def wrap(stage, *, extra=b""):
    payload = inspect_elf(stage, STAGE, 0x8CFE0000)["payload"]
    outer = b"\x09\x00" * 32 + bytes(0x2000 - 64) + payload + extra
    entry = elf_fixture(ADDRESS, outer,
        [("__retail_stage_blob_start", 0x2000), ("__retail_stage_blob_end", 0x2000 + len(payload))])
    return entry, envelope(outer, len(outer), "0123456789ab")


class ModeArtifactTests(unittest.TestCase):
    def audit(self, stage=None, *, entry=None, module=None, cached=True):
        stage = stage_fixture() if stage is None else stage
        if entry is None:
            entry, default = wrap(stage)
            module = default if module is None else module
        return audit(stage, entry, module, cached_code=cached)

    def test_exact_typed_unique_normal_mode(self):
        report = self.audit()
        self.assertTrue(report["enabled"])
        self.assertEqual(report["module_offset"], 64 + 0x2000 + MODE_AT)
        self.assertEqual(report["bytes"], 32)
        self.assertEqual(report["modes"], {"normal": 0, "control": 1, "profile": 2, "lease-only": 3, "hooks-only": 4, "raw-chain": 5, "evolution-trace": 6})

    def test_every_generic_byte_and_invalid_lengths(self):
        wire = template_mode()
        self.assertEqual(wire, b"KUINVM01" + struct.pack("<3I", 1, 32, 0) + bytes(12))
        for i in range(len(wire)):
            changed = bytearray(wire); changed[i] ^= 1
            with self.subTest(offset=i), self.assertRaises(ValueError):
                generic_mode(bytes(changed))
        for changed in (wire[:-1], wire + b"\0", b""):
            with self.assertRaises(ValueError): generic_mode(changed)

    def test_generic_cannot_enable_control_or_profile(self):
        for mode in (1, 2, 3, 4, 5, 6, 0xFFFFFFFF):
            raw = bytearray(stage_fixture()); struct.pack_into("<I", raw, 0x100 + MODE_AT + 16, mode)
            with self.subTest(mode=mode), self.assertRaisesRegex(ValueError, "normal template"):
                self.audit(bytes(raw))

    def test_typed_object_and_functions_are_required(self):
        for changes in ({"info": 0x10}, {"info": 0x12}, {"size": 0}, {"size": 28},
                        {"size": 36}, {"value": STAGE + MODE_AT + 2}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.audit(change_symbol(stage_fixture(), START, **changes))
        for name in (HELPER, DECODER):
            for changes in ({"info": 0x10}, {"owner": 2}, {"size": 0}):
                with self.subTest(name=name, changes=changes), self.assertRaises(ValueError):
                    self.audit(change_symbol(stage_fixture(), name, **changes))

    def test_missing_partial_and_untyped_markers_refuse(self):
        for names in ((START,), (END,), (START, END), (HELPER,), (DECODER,)):
            raw = stage_fixture()
            for name in names: raw = hide_symbol(raw, name)
            with self.subTest(names=names), self.assertRaises(ValueError): self.audit(raw)

    def test_writable_executable_or_unallocated_blob_refuses(self):
        for flags in (0, 3, 6, 7):
            with self.subTest(flags=flags), self.assertRaises(ValueError): self.audit(stage_fixture(flags=flags))

    def test_exact_end_and_consistent_load_file_offsets(self):
        for changes in ({"value": STAGE + MODE_AT + 36}, {"owner": 1}):
            with self.assertRaises(ValueError): self.audit(change_symbol(stage_fixture(), END, **changes))
        raw = bytearray(stage_fixture()); header = EH.unpack_from(raw)
        at = header[6] + 2 * SH.size; row = list(SH.unpack_from(raw, at)); row[4] += 4
        SH.pack_into(raw, at, *row)
        with self.assertRaises(ValueError): self.audit(bytes(raw))

    def test_protected_span_overlap_is_refused(self):
        for first, last in (("__native_worker_blob_begin", "__native_worker_blob_end"),
                            ("__native_worker_reloc_begin", "__native_worker_reloc_end"),
                            ("__retail_trampoline_start", "__retail_trampoline_end"),
                            ("__vmu_sd_profile_blob_start", "__vmu_sd_profile_blob_end"),
                            ("__native_resolution_cache_seed_start", "__native_resolution_cache_seed_end")):
            raw = change_symbol(stage_fixture(), first, value=STAGE + MODE_AT + 4)
            raw = change_symbol(raw, last, value=STAGE + MODE_AT + 8)
            with self.subTest(first=first), self.assertRaisesRegex(ValueError, "overlaps"):
                self.audit(raw)

    def test_duplicate_templates_refuse_in_stage_and_outer_module(self):
        with self.assertRaisesRegex(ValueError, "unique typed"): self.audit(stage_fixture(duplicate=True))
        stage = stage_fixture(); entry, module = wrap(stage, extra=template_mode())
        with self.assertRaisesRegex(ValueError, "typed unique"): self.audit(stage, entry=entry, module=module)

    def test_substituted_stage_or_module_refuses(self):
        stage = stage_fixture(); entry, module = wrap(stage)
        changed = bytearray(module); changed[64 + 0x2000 + MODE_AT + 16] = 1
        replacement = envelope(bytes(changed[64:]), len(changed) - 64, "0123456789ab")
        with self.assertRaises(ValueError): self.audit(stage, entry=entry, module=replacement)
        changed = bytearray(entry); changed[0x100 + 0x2000 + MODE_AT + 16] = 1
        with self.assertRaises(ValueError): self.audit(stage, entry=bytes(changed), module=module)

    def test_cached_policy_must_be_explicit(self):
        for cached in (False, 1):
            with self.subTest(cached=cached), self.assertRaises(ValueError): self.audit(cached=cached)

    def test_legacy_absence_only_without_any_markers(self):
        labels = [("legacy", 64)]
        raw = elf_fixture(STAGE, b"\x09\0" * 96, labels)
        entry, module = wrap(raw)
        self.assertEqual(audit(raw, entry, module, cached_code=False), {"enabled": False})


if __name__ == "__main__": unittest.main()
