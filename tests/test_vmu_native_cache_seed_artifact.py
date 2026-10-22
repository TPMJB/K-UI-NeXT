#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent SH ELF and seed mutations for the optional cache admission."""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_loader_layout import EH, PH, SH, SYM, inspect_elf
from check_vmu_native_cache_seed import audit, empty_seed, generic_seed, START, END, STAGE, SEED_BYTES
from runtime_package import ADDRESS, envelope
from test_personalize_vmu_sd_proof import elf_fixture

BUILD = "0123456789ab"
SEED_AT = 512
LABELS = [("__native_worker_blob_begin", 64), ("__native_worker_blob_end", 192),
          ("__native_worker_reloc_begin", 192), ("__native_worker_reloc_end", 256),
          ("__retail_trampoline_start", 256), ("__retail_trampoline_end", 384),
          (START, SEED_AT), (END, SEED_AT + SEED_BYTES),
          ("__vmu_sd_profile_blob_start", 2048), ("__vmu_sd_profile_blob_end", 6144)]


def change_symbol(elf, wanted, **changes):
    raw = bytearray(elf); header = EH.unpack_from(raw)
    sections = [SH.unpack_from(raw, header[6] + i * SH.size) for i in range(header[12])]
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        names = raw[strings[4]:strings[4] + strings[5]]
        for at in range(section[4], section[4] + section[5], SYM.size):
            row = list(SYM.unpack_from(raw, at))
            label = names[row[0]:].split(b"\0", 1)[0].decode("ascii")
            if label == wanted:
                for field, value in changes.items():
                    row[{"name": 0, "value": 1, "size": 2, "info": 3, "owner": 5}[field]] = value
                SYM.pack_into(raw, at, *row)
                return bytes(raw)
    raise AssertionError("fixture symbol absent: " + wanted)


def stage_fixture(*, duplicate=False, flags=2):
    payload = bytearray(b"\x09\x00" * 3200)
    payload[SEED_AT:SEED_AT + SEED_BYTES] = empty_seed()
    if duplicate:
        payload.extend(empty_seed())
    elf = elf_fixture(STAGE, bytes(payload), LABELS, readonly_flags=flags)
    return change_symbol(elf, START, size=SEED_BYTES, info=0x11)


def hide_symbol(elf, wanted):
    raw = bytearray(elf); header = EH.unpack_from(raw)
    sections = [SH.unpack_from(raw, header[6] + i * SH.size) for i in range(header[12])]
    for section in sections:
        if section[1] == 3:
            at = raw.find(wanted.encode() + b"\0", section[4], section[4] + section[5])
            if at >= 0:
                raw[at] = ord("x")
                return bytes(raw)
    raise AssertionError("fixture symbol name absent")


def wrap(stage, *, entry_extra=b""):
    payload = inspect_elf(stage, STAGE, 0x8CFE0000)["payload"]
    entry_payload = b"\x09\x00" * 32 + bytes(0x2000 - 64) + payload + entry_extra
    entry = elf_fixture(ADDRESS, entry_payload,
        [("__retail_stage_blob_start", 0x2000), ("__retail_stage_blob_end", 0x2000 + len(payload))])
    return entry, envelope(entry_payload, len(entry_payload), BUILD)


class SeedArtifactTests(unittest.TestCase):
    def run_audit(self, stage=None, *, cached=True, entry=None, module=None):
        stage = stage_fixture() if stage is None else stage
        if entry is None:
            entry, default_module = wrap(stage)
            module = default_module if module is None else module
        return audit(stage, entry, module, cached_code=cached)

    def test_typed_unique_seed_and_all_embeddings(self):
        report = self.run_audit()
        self.assertTrue(report["enabled"])
        self.assertEqual(report["seed_module_offset"], 64 + 0x2000 + SEED_AT)
        self.assertEqual(report["seed_bytes"], 1120)
        self.assertTrue(report["typed_immutable_object"])

    def test_every_authored_seed_byte_corruption_is_refused(self):
        good = empty_seed()
        self.assertEqual(len(good), 1120)
        self.assertEqual(good[:24], b"KUINVS01" + struct.pack("<4I", 1, 1120, 0, 1024))
        for i in range(len(good)):
            changed = bytearray(good); changed[i] ^= 1
            with self.subTest(offset=i), self.assertRaises(ValueError):
                generic_seed(bytes(changed))
        for changed in (good[:-1], good + b"\0", b""):
            with self.assertRaises(ValueError): generic_seed(changed)

    def test_generic_build_never_contains_a_pre_filled_record(self):
        stage = bytearray(stage_fixture())
        struct.pack_into("<I", stage, 0x100 + SEED_AT + 16, 1)
        with self.assertRaisesRegex(ValueError, "empty template"):
            self.run_audit(bytes(stage))

    def test_start_requires_exact_stt_object_type_and_size(self):
        stage = stage_fixture()
        for changes in ({"info": 0x10}, {"info": 0x12}, {"size": 0},
                        {"size": 1116}, {"size": 1124}, {"value": STAGE + SEED_AT + 2}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.run_audit(change_symbol(stage, START, **changes))

    def test_end_and_section_bounds_are_exact(self):
        stage = stage_fixture()
        for changes in ({"value": STAGE + SEED_AT + SEED_BYTES + 4}, {"owner": 1}):
            with self.assertRaises(ValueError):
                self.run_audit(change_symbol(stage, END, **changes))
        raw = bytearray(stage); header = EH.unpack_from(raw)
        at = header[6] + 2 * SH.size; section = list(SH.unpack_from(raw, at))
        section[5] = SEED_AT + SEED_BYTES - 64 - 4; SH.pack_into(raw, at, *section)
        with self.assertRaises(ValueError): self.run_audit(bytes(raw))

    def test_cache_requires_cached_native_worker(self):
        with self.assertRaisesRegex(ValueError, "cached native code"):
            self.run_audit(cached=False)
        with self.assertRaisesRegex(ValueError, "explicit worker alias"):
            self.run_audit(cached=1)

    def test_missing_or_partial_exports_cannot_hide_a_seed(self):
        stage = stage_fixture()
        # Rename without changing any bytes, sizes or symbol-table topology.
        for names in ((START,), (END,), (START, END)):
            raw = stage
            for name in names:
                raw = hide_symbol(raw, name)
            with self.subTest(names=names), self.assertRaises(ValueError):
                self.run_audit(raw)

    def test_writable_executable_or_unallocated_seed_is_refused(self):
        for flags in (0, 3, 6, 7):
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                self.run_audit(stage_fixture(flags=flags))

    def test_section_and_program_header_must_name_same_file_bytes(self):
        raw = bytearray(stage_fixture()); header = EH.unpack_from(raw)
        at = header[6] + 2 * SH.size; section = list(SH.unpack_from(raw, at))
        section[4] += 4; SH.pack_into(raw, at, *section)
        with self.assertRaisesRegex(ValueError, "file offsets"):
            self.run_audit(bytes(raw))

    def test_duplicate_coherent_templates_are_refused(self):
        with self.assertRaisesRegex(ValueError, "unique typed"):
            self.run_audit(stage_fixture(duplicate=True))
        stage = stage_fixture(); entry, module = wrap(stage, entry_extra=empty_seed())
        with self.assertRaisesRegex(ValueError, "unique empty seed"):
            self.run_audit(stage, entry=entry, module=module)

    def test_other_profile_blob_overlap_is_refused(self):
        for first, last in (("__native_worker_blob_begin", "__native_worker_blob_end"),
                            ("__native_worker_reloc_begin", "__native_worker_reloc_end"),
                            ("__retail_trampoline_start", "__retail_trampoline_end"),
                            ("__vmu_sd_profile_blob_start", "__vmu_sd_profile_blob_end")):
            stage = change_symbol(stage_fixture(), first, value=STAGE + SEED_AT + 16)
            stage = change_symbol(stage, last, value=STAGE + SEED_AT + 32)
            with self.subTest(first=first), self.assertRaisesRegex(ValueError, "overlaps"):
                self.run_audit(stage)

    def test_module_and_embedded_stage_cannot_be_substituted(self):
        stage = stage_fixture(); entry, module = wrap(stage)
        changed = bytearray(module); changed[64 + 0x2000 + SEED_AT + 24] ^= 1
        replacement = envelope(bytes(changed[64:]), len(changed) - 64, BUILD)
        with self.assertRaises(ValueError): self.run_audit(stage, entry=entry, module=replacement)
        other = bytearray(stage); other[0x100 + 66] ^= 1
        entry, module = wrap(bytes(other))
        with self.assertRaisesRegex(ValueError, "different cache-enabled stage"):
            self.run_audit(stage, entry=entry, module=module)

    def test_a_legacy_stage_without_cache_exports_is_unaffected(self):
        payload = b"\x09\x00" * 3200
        stage = elf_fixture(STAGE, payload, [("unrelated", 64)])
        entry, module = wrap(stage)
        self.assertEqual(self.run_audit(stage, entry=entry, module=module), {"enabled": False})


if __name__ == "__main__":
    unittest.main()
