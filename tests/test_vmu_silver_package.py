#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exact-Silver linked identity, hostile-callee replay and packaging admission."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_loader_layout import EH, PH, SH, SYM, inspect_elf
from check_vmu_maple_observe_hook import check_hook, check_completion_hook
from package_vmu_maple_observe import (inspect_worker, inspect_linked_maple_profile,
    inspect_boot_heap_hook, inspect_sd_profile, require_worker_stack_coverage, write_test_archive)
from test_vmu_maple_observe_hook import fixture as trampoline_fixture
from test_vmu_maple_observe_package import low_reader
from vmu_maple_observe_symbols import exit_target_cell, generate
from vmu_sd_profile import template_profile


def silver_trampoline():
    """Only authored observer bytes; no owner-game executable is included."""
    image, exports = trampoline_fixture()
    payload = image["payload"]
    for toy, silver in ((0x8C0BF270, 0x8C1546DC), (0x8C0BF37A, 0x8C1547E6)):
        original = struct.pack("<I", toy)
        assert payload.count(original) == 1
        payload = payload.replace(original, struct.pack("<I", silver))
    return dict(image, payload=payload), exports


def worker_elf(profile=2, *, named=True, silver=True, readonly_mode=0):
    """Admitted ELF carrying the real authored trampoline and a typed scalar."""
    image, exports = silver_trampoline() if silver else trampoline_fixture()
    base, private = 0x8CFD0000, 0xACFD0000
    payload = bytearray(image["payload"] + struct.pack("<I", profile))
    struct.pack_into("<I", payload, 14 * 4, readonly_mode)
    if readonly_mode >= 2:
        struct.pack_into("<I", payload, 15 * 4, base + 380)
    data = bytearray(4096)
    EH.pack_into(data, 0, b"\x7fELF\x01\x01\x01" + bytes(9), 2, 42, 1,
                 exports["initialize"], 52, 0x300, 0, 52, 32, 2, 40, 7, 0)
    PH.pack_into(data, 52, 1, 0x100, base, base, len(payload), len(payload), 5, 32)
    PH.pack_into(data, 84, 1, 0x280, private + 0x200, base + 0x200,
                 0, 0x1300, 6, 32)
    SH.pack_into(data, 0x328, 0, 1, 6, base + 64, 0x140, 320, 0, 0, 4, 0)
    SH.pack_into(data, 0x350, 0, 8, 3, private + 0x200, 0x280, 0x300, 0, 0, 32, 0)
    SH.pack_into(data, 0x378, 0, 8, 3, private + 0x500, 0x280, 0x1000, 0, 0, 32, 0)
    SH.pack_into(data, 0x3F0, 0, 1, 2, base + 384, 0x280, 4, 0, 0, 4, 0)
    symbols = [
        ("_kui_vmu_maple_observer_init", exports["initialize"], 4, 0x12, 1),
        ("_kui_vmu_maple_observer_hook", exports["hook"], 148, 0x12, 1),
        ("_kui_vmu_maple_observer_terminal", exports["terminal"], 24, 0x12, 1),
        ("_kui_vmu_maple_observer_snapshot", exports["snapshot"], 708, 0x11, 2),
        ("_kui_vmu_maple_observer_complete_hook", exports["complete_hook"], 132, 0x12, 1),
        ("_kui_vmu_maple_observer_visit", image["symbols"]["_kui_vmu_maple_observer_visit"], 4, 0x12, 1),
        ("_kui_vmu_maple_observer_complete", image["symbols"]["_kui_vmu_maple_observer_complete"], 4, 0x12, 1),
    ]
    if named:
        symbols.append(("_kui_vmu_maple_profile", base + 384, 4, 0x11, 6))
    if readonly_mode >= 2:
        symbols.append(("_kui_vmu_maple_observer_sd_transaction", base + 380, 4, 0x12, 1))
    names = bytearray(b"\0")
    for index, (name, address, size, info, owner) in enumerate(symbols, 1):
        SYM.pack_into(data, 0x500 + index * 16, len(names), address, size, info, 0, owner)
        names.extend(name.encode("ascii") + b"\0")
    SH.pack_into(data, 0x3A0, 0, 2, 0, 0, 0x500, (len(symbols) + 1) * 16, 5, 1, 4, 16)
    SH.pack_into(data, 0x3C8, 0, 3, 0, 0, 0x700, len(names), 0, 0, 1, 0)
    data[0x700:0x700 + len(names)] = names
    data[0x100:0x100 + len(payload)] = payload
    return data


def inspect(data, expected=None):
    with tempfile.TemporaryDirectory() as name:
        directory = Path(name)
        (directory / "worker.elf").write_bytes(data)
        size = PH.unpack_from(data, 52)[4]
        (directory / "worker.bin").write_bytes(data[0x100:0x100 + size])
        return inspect_worker(directory, maple_profile=expected)


def silver_low_reader():
    data = low_reader()
    header = list(EH.unpack_from(data))
    header[12] = 6
    EH.pack_into(data, 0, *header)
    struct.pack_into("<I", data, 0x328 + 20, 124)
    SH.pack_into(data, 0x3C8, 0, 1, 2, 0x8C00407C, 0x17C, 4, 0, 0, 4, 0)
    old_names = SH.unpack_from(data, 0x3A0)[5]
    name = b"_kui_vmu_observe_exit_target\0"
    data[0x600 + old_names:0x600 + old_names + len(name)] = name
    struct.pack_into("<I", data, 0x3A0 + 20, old_names + len(name))
    SYM.pack_into(data, 0x500 + 5 * 16, old_names, 0x8C00407C, 4, 0x11, 0, 5)
    struct.pack_into("<I", data, 0x378 + 20, 6 * 16)
    struct.pack_into("<I", data, 0x17C, 0x8C004024)
    return data


class SilverPackageTests(unittest.TestCase):
    def test_actual_linked_silver_trampolines_run_hostile_callee_model(self):
        report = inspect(worker_elf(), "silver")
        self.assertEqual(report["maple_profile"]["id"], 2)
        self.assertEqual(report["maple_profile"]["binding"], "typed read-only linked scalar")
        self.assertEqual(report["mode"], "read-only Silver Maple observation")
        self.assertEqual(report["hook_integer_state"]["resume"], "0x8c1546dc")
        self.assertEqual(report["completion_integer_state"]["resume"], "0x8c1547e6")
        self.assertEqual(report["hook_integer_state"]["manager_pointer"], "0x8c29143c")
        for key in ("hook_integer_state", "completion_integer_state"):
            self.assertEqual(report[key]["cases"], 128)
            self.assertTrue(report[key]["integer_state_preserved"])
        for key in ("virtual_vmu_enabled", "storage_writes_enabled", "virtual_writes_enabled", "persistence"):
            self.assertFalse(report[key])

    def test_new_toy_and_historical_toy_keep_original_witnesses(self):
        for named in (True, False):
            with self.subTest(named=named):
                report = inspect(worker_elf(1, named=named, silver=False), "toy")
                self.assertEqual(report["maple_profile"]["id"], 1)
                self.assertEqual(report["hook_integer_state"]["resume"], "0x8c0bf270")
                self.assertEqual(report["completion_integer_state"]["resume"], "0x8c0bf37a")

    def test_cli_cannot_relabel_toy_or_select_silver_without_linked_scalar(self):
        for data in (worker_elf(1, silver=False), worker_elf(named=False)):
            with self.subTest(scalar=bool(data)), self.assertRaisesRegex(ValueError, "actual linked profile"):
                inspect(data, "silver")
        with self.assertRaisesRegex(ValueError, "actual linked profile"):
            inspect(worker_elf(), "toy")

    def test_removing_or_relabelling_scalar_cannot_admit_silver_as_legacy_toy(self):
        for data in (worker_elf(named=False), worker_elf(1)):
            with self.subTest(named=b"_kui_vmu_maple_profile" in data), self.assertRaises(ValueError):
                inspect(data)

    def test_silver_blank_a1_mode_is_linked_readonly_without_sd_transaction(self):
        report = inspect(worker_elf(readonly_mode=1), "silver")
        self.assertEqual(report["maple_profile"]["id"], 2)
        self.assertEqual(report["exports"]["readonly_mode"], 1)
        self.assertEqual(report["exports"]["sd_transaction"], 0)
        self.assertEqual(report["mode"], "READ-ONLY Silver virtual blank A1 proof")
        self.assertTrue(report["virtual_vmu_enabled"])
        for key in ("storage_writes_enabled", "virtual_writes_enabled", "volatile_writes_enabled", "persistence"):
            self.assertFalse(report[key])
        self.assertEqual(report["hook_integer_state"]["resume"], "0x8c1546dc")
        self.assertEqual(report["completion_integer_state"]["resume"], "0x8c1547e6")
        self.assertEqual(report["hook_integer_state"]["original_dma_start_writes"], 1)
        self.assertEqual(report["completion_integer_state"]["original_dma_start_writes"], 0)
        with self.assertRaisesRegex(ValueError, "actual linked profile"):
            inspect(worker_elf(1, silver=False, readonly_mode=1), "silver")
        data = worker_elf(readonly_mode=1)
        struct.pack_into("<I", data, 0x100 + 15 * 4, 0x8CFD017C)
        with self.assertRaisesRegex(ValueError, "exports identity"):
            inspect(data)

    def test_silver_sd_readonly_mode_requires_its_exact_typed_transaction_export(self):
        report = inspect(worker_elf(readonly_mode=2), "silver")
        self.assertEqual(report["maple_profile"]["id"], 2)
        self.assertEqual(report["exports"]["readonly_mode"], 2)
        self.assertEqual(report["exports"]["sd_transaction"], 0x8CFD017C)
        self.assertEqual(report["mode"], "READ-ONLY Silver SD-backed virtual A1 proof")
        self.assertTrue(report["virtual_vmu_enabled"])
        for key in ("storage_writes_enabled", "virtual_writes_enabled", "volatile_writes_enabled", "persistence"):
            self.assertFalse(report[key])
        self.assertEqual(report["hook_integer_state"]["resume"], "0x8c1546dc")
        self.assertEqual(report["completion_integer_state"]["resume"], "0x8c1547e6")
        for offset, fmt, value in ((0x100 + 15 * 4, "I", 0),
                                    (0x590 + 8, "I", 0), (0x590 + 12, "B", 0x11),
                                    (0x590 + 4, "I", 0xACFD017C)):
            data = worker_elf(readonly_mode=2)
            struct.pack_into("<" + fmt, data, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                inspect(data, "silver")
        with self.assertRaisesRegex(ValueError, "actual linked profile"):
            inspect(worker_elf(1, silver=False, readonly_mode=2), "silver")

    def test_silver_refuses_volatile_mode_and_incomplete_persistent_worker(self):
        with self.assertRaisesRegex(ValueError, "volatile proof mode 3"):
            inspect(worker_elf(readonly_mode=3))
        # A label alone cannot admit persistence: require ABI 6 and actual
        # private DMA buffers, then the native audit covers durable I/O too.
        data = worker_elf(readonly_mode=4)
        struct.pack_into("<I", data, 0x104, 6)
        with self.assertRaisesRegex(ValueError, "private P2 DMA object"):
            inspect(data)

    def test_silver_blank_a1_stage_must_not_embed_sd_profile(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            self.assertIsNone(inspect_sd_profile(directory, {"symbols": {}}, 1))
            for symbol in ("__vmu_sd_profile_blob_start", "__vmu_sd_profile_blob_end"):
                with self.subTest(symbol=symbol), self.assertRaisesRegex(ValueError, "must not embed an SD profile"):
                    inspect_sd_profile(directory, {"symbols": {symbol: 0x8CE00100}}, 1)

    def test_silver_sd_readonly_profile_must_match_actual_stage_blob(self):
        wire = template_profile()
        stage = {"payload": bytes(32) + wire, "symbols": {
            "__vmu_sd_profile_blob_start": 0x8CE00020,
            "__vmu_sd_profile_blob_end": 0x8CE00620}}
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            (directory / "vmu-sd-profile.bin").write_bytes(wire)
            report = inspect_sd_profile(directory, stage, 2)
            self.assertEqual(report["embedded_address"], "0x8ce00020")
            self.assertEqual(report["embedded_bytes"], 1536)
            changed = dict(stage, payload=bytes(32) + bytes(1536))
            with self.assertRaisesRegex(ValueError, "different SD profile"):
                inspect_sd_profile(directory, changed, 2)
            with self.assertRaisesRegex(ValueError, "bounded 1536-byte"):
                inspect_sd_profile(directory, {"symbols": {}}, 2)

    def test_sd_stack_coverage_requires_both_exact_silver_ownership_guards(self):
        guards = ("silver_layout", "silver_chain_matches")
        symbols = {"_" + name: 0x8CFD0080 + index * 4 for index, name in enumerate(guards)}
        rows = [(name, 32) for name in guards]
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            self.assertEqual(require_worker_stack_coverage(directory, {"symbols": symbols}, rows), 2)
            for index, guard in enumerate(guards):
                with self.subTest(guard=guard), self.assertRaisesRegex(ValueError, "Missing linked worker"):
                    require_worker_stack_coverage(directory, {"symbols": symbols}, rows[:index] + rows[index + 1:])

    def test_profile_scalar_requires_exact_type_size_alignment_and_readonly_section(self):
        for offset, fmt, value in ((0x580 + 8, "I", 0), (0x580 + 8, "I", 8),
                                   (0x580 + 12, "B", 0x12), (0x580 + 4, "I", 0x8CFD0182),
                                   (0x3F0 + 8, "I", 6), (0x3F0 + 4, "I", 8),
                                   (0x280, "I", 0), (0x280, "I", 3)):
            data = worker_elf()
            struct.pack_into("<" + fmt, data, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                inspect(data)

    def test_silver_resume_and_consumer_must_match_actual_linked_literals(self):
        for original, changed, check in ((0x8C1546DC, 0x8C0BF270, check_hook),
                                          (0x8C1547E6, 0x8C0BF37A, check_completion_hook)):
            image, exports = silver_trampoline()
            image["payload"] = image["payload"].replace(struct.pack("<I", original), struct.pack("<I", changed))
            with self.subTest(original=original), self.assertRaises(ValueError):
                check(image, exports, maple_profile=2)

    def test_silver_preserves_exact_dma_store_and_sr_replay(self):
        for before, after in ((0x12C6, 0x0009), (0x410E, 0x0009), (0x7044, 0x7040)):
            image, exports = silver_trampoline()
            data = bytearray(image["payload"])
            original = struct.pack("<H", before)
            at = data.index(original, exports["hook"] - 0x8CFD0000,
                            exports["complete_hook"] - 0x8CFD0000)
            struct.pack_into("<H", data, at, after)
            image["payload"] = data
            with self.subTest(opcode=before), self.assertRaises(ValueError):
                check_hook(image, exports, maple_profile=2)

    def test_stage_profile_uses_its_own_payload_and_typed_object(self):
        data = worker_elf()
        image = inspect_elf(data, 0x8CFD0000, 0x8CFE0000, private_p2=True,
                            entry_symbol="_kui_vmu_maple_observer_init", entry_at_base=False)
        self.assertEqual(inspect_linked_maple_profile(data, image, 0x8CFD0000)["id"], 2)
        changed = dict(image, payload=image["payload"][:-4] + struct.pack("<I", 1))
        with self.assertRaisesRegex(ValueError, "typed read-only"):
            inspect_linked_maple_profile(data, changed, 0x8CFD0000)

    def test_boot_original_initializer_is_resolved_from_actual_linked_call(self):
        for profile, target in ((1, 0x8C0B3E10), (2, 0x8C11E1E8)):
            data = low_reader() if profile == 1 else silver_low_reader()
            # Hook begins base+4; PC-relative load at+4 reads literal+16.
            struct.pack_into("<3H", data, 0x104, 0xD302, 0x430B, 0x0009)
            struct.pack_into("<I", data, 0x110, target)
            image = inspect_elf(data, 0x8C004000, 0x8C007800)
            result = inspect_boot_heap_hook(data, image, profile)
            self.assertEqual(result["original_heap_init"], f"0x{target:08x}")
            with self.subTest(profile=profile), self.assertRaisesRegex(ValueError, "differs"):
                inspect_boot_heap_hook(data, image, 3 - profile)
            for at, fmt, value in ((0x110, "I", target + 2), (0x106, "H", 0x0009),
                                   (0x104, "H", 0xD37F)):
                changed = bytearray(data)
                struct.pack_into("<" + fmt, changed, at, value)
                admitted = inspect_elf(changed, 0x8C004000, 0x8C007800)
                with self.subTest(at=at), self.assertRaises(ValueError):
                    inspect_boot_heap_hook(changed, admitted, profile)

    def test_silver_exit_cell_is_readonly_and_targets_actual_typed_return_hook(self):
        data = silver_low_reader()
        image = inspect_elf(data, 0x8C004000, 0x8C007800)
        self.assertEqual(exit_target_cell(data, image), (0x8C00407C, 0x8C004024))
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / "resident.elf"
            path.write_bytes(data)
            self.assertIn("#define KUI_VMU_OBSERVE_LOW_EXIT_TARGET 0x8c00407cu", generate(path))
        for at, fmt, value in ((0x17C, "I", 0x8C004026),
                               (0x3C8 + 8, "I", 3), (0x3C8 + 8, "I", 6),
                               (0x550 + 8, "I", 8), (0x550 + 12, "B", 0x12),
                               (0x550 + 4, "I", 0x8C00407E)):
            changed = bytearray(data)
            struct.pack_into("<" + fmt, changed, at, value)
            admitted = inspect_elf(changed, 0x8C004000, 0x8C007800)
            with self.subTest(at=at, value=value), self.assertRaises(ValueError):
                exit_target_cell(changed, admitted)

    def test_silver_blank_a1_archive_has_distinct_name_and_guide(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            module = b"Silver read-only blank A1 module"
            guide = b"Silver blank A1 read-only test guide"
            with patch("package_vmu_maple_observe.Path.read_bytes", autospec=True, return_value=guide) as read:
                target = write_test_archive(directory, module, b"{}", maple_profile=2, readonly_proof=True)
                read.assert_called_once_with(Path("docs/vmu-silver-readonly-card-test.md"))
            self.assertEqual(target.name, "vmu-silver-readonly-card-test.zip")
            with zipfile.ZipFile(target) as archive:
                self.assertEqual(archive.read("README-FIRST.md"), guide)
                self.assertEqual(archive.read("KUI/apps/games/retail-boot.kui"), module)
                self.assertNotIn("vmu-sd-profile.bin", archive.namelist())

    def test_silver_sd_readonly_archive_has_distinct_name_and_guide(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            module = b"Silver SD-backed read-only imported A1 module"
            guide = b"Silver SD-backed read-only imported A1 test guide"
            with patch("package_vmu_maple_observe.Path.read_bytes", autospec=True, return_value=guide) as read:
                target = write_test_archive(directory, module, b"{}", maple_profile=2, sd_readonly_proof=True)
                read.assert_called_once_with(Path("docs/vmu-silver-sd-readonly-card-test.md"))
            self.assertEqual(target.name, "vmu-silver-sd-readonly-card-test.zip")
            with zipfile.ZipFile(target) as archive:
                self.assertEqual(archive.read("README-FIRST.md"), guide)
                self.assertEqual(archive.read("KUI/apps/games/retail-boot.kui"), module)
                self.assertNotIn("vmu-sd-profile.bin", archive.namelist())

    def test_silver_archive_rejects_volatile_mode(self):
        with tempfile.TemporaryDirectory() as name:
            with self.assertRaisesRegex(ValueError, "volatile proof mode 3"):
                write_test_archive(Path(name), b"module", b"{}", maple_profile=2, volatile_proof=True)

    def test_silver_persistent_archive_uses_actual_p2_binding_and_own_guide(self):
        from vmu_persist_profile import template_profile as persist_template
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            wire = persist_template()
            guide = b"Silver persistent test {{PAIR_PATH}}"
            def read(path):
                if path == Path("docs/vmu-silver-persistent-card-test.md"):
                    return guide
                self.assertEqual(path, directory / "vmu-sd-profile.bin")
                return wire
            with patch("package_vmu_maple_observe.Path.read_bytes", autospec=True, side_effect=read):
                target = write_test_archive(directory, b"Silver persistent", b"{}",
                                            maple_profile=2, persistent_proof=True)
            self.assertEqual(target.name, "vmu-silver-persistent-card-test.zip")
            with zipfile.ZipFile(target) as archive:
                self.assertEqual(archive.read("README-FIRST.md"), b"Silver persistent test /KUI/vmu-test/p0001")
                self.assertEqual(archive.read("KUI/apps/games/retail-boot.kui"), b"Silver persistent")
                self.assertNotIn("vmu-sd-profile.bin", archive.namelist())


if __name__ == "__main__":
    unittest.main()
