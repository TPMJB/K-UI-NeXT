#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject observer binaries with mismatched exports or private memory bounds."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_loader_layout import EH, PH, SH, SYM
from package_vmu_maple_observe import (inspect_worker, write_test_archive,
    inspect_sd_profile, check_sd_gateway, check_sd_stacks, linked_stack_rows,
    require_worker_stack_coverage)
from check_loader_layout import inspect_elf
from vmu_maple_observe_symbols import generate
from test_vmu_sd_profile_tool import fixture as sd_profile_fixture
from test_personalize_vmu_sd_proof import build_fixture, owner_profile, COMMIT
from personalize_vmu_sd_proof import personalize


def worker(readonly_mode=0, abi=6):
    base = 0x8CFD0000
    private = base + 0x20000000
    data = bytearray(4096)
    EH.pack_into(data, 0, b"\x7fELF\x01\x01\x01" + bytes(9), 2, 42, 1,
                 base + 64, 52, 0x300, 0, 52, 32, 2, 40, 6, 0)
    PH.pack_into(data, 52, 1, 0x100, base, base, 128, 128, 5, 32)
    bss_end = 0x1800 if readonly_mode == 3 else 0x400
    PH.pack_into(data, 84, 1, 0x200, private + 0x100, base + 0x100,
                 0, bss_end + 4096 - 0x100, 6, 32)
    SH.pack_into(data, 0x328, 0, 1, 6, base, 0x100, 128, 0, 0, 32, 0)
    SH.pack_into(data, 0x350, 0, 8, 3, private + 0x100, 0x200, bss_end - 0x100, 0, 0, 32, 0)
    SH.pack_into(data, 0x378, 0, 8, 3, private + bss_end, 0x200, 4096, 0, 0, 32, 0)
    names = bytearray(b"\0")
    symbols = (("_kui_vmu_maple_observer_init", base + 64, 2, 0x12, 1),
               ("_kui_vmu_maple_observer_hook", base + 68, 2, 0x12, 1),
               ("_kui_vmu_maple_observer_terminal", base + 72, 2, 0x12, 1),
               ("_kui_vmu_maple_observer_snapshot", private + 0x100, 708 if abi == 6 else 644, 0x11, 2),
               ("_kui_vmu_maple_observer_complete_hook", base + 76, 2, 0x12, 1))
    if readonly_mode >= 2:
        symbols += (("_kui_vmu_maple_observer_sd_transaction", base + 80, 2, 0x12, 1),)
    if readonly_mode == 3:
        symbols += (("_issued_chain", private + 0x400, 512, 0x11, 2),
                    ("_captured_chain", private + 0x600, 512, 0x11, 2),
                    ("_write_payloads", private + 0x800, 4096, 0x11, 2))
    SH.pack_into(data, 0x3A0, 0, 2, 0, 0, 0x500, (len(symbols) + 1) * 16, 5, 1, 4, 16)
    for index, (name, address, size, kind, section) in enumerate(symbols, 1):
        SYM.pack_into(data, 0x500 + index * 16, len(names), address,
                      size, kind, 0, section)
        names.extend(name.encode() + b"\0")
    SH.pack_into(data, 0x3C8, 0, 3, 0, 0, 0x600, len(names), 0, 0, 1, 0)
    data[0x600:0x600 + len(names)] = names
    exports = (0x564D4F31, abi, 64, base + 64, base + 68,
               private + 0x100, private + bss_end, private + bss_end,
               private + bss_end + 4096, base + bss_end + 4096, 0x30000,
               private + 0x100, base + 72, base + 76, readonly_mode,
               base + 80 if readonly_mode >= 2 else 0)
    struct.pack_into("<16I", data, 0x100, *exports)
    return data


def low_reader(sd_lease=False):
    base = 0x8C004000
    data = bytearray(4096)
    EH.pack_into(data, 0, b"\x7fELF\x01\x01\x01" + bytes(9), 2, 42, 1,
                 base, 52, 0x300, 0, 52, 32, 1, 40, 7 if sd_lease else 5, 0)
    PH.pack_into(data, 52, 1, 0x100, base, base, 128, 192, 7, 4)
    SH.pack_into(data, 0x328, 0, 1, 6, base, 0x100, 64 if sd_lease else 128, 0, 0, 4, 0)
    SH.pack_into(data, 0x350, 0, 8, 3, base + 128, 0x180, 64, 0, 0, 32, 0)
    names = bytearray(b"\0")
    symbols = (("_start", base, 4, 0x12, 1),
               ("_kui_vmu_observe_heap_hook", base + 4, 32, 0x12, 1),
               ("_kui_vmu_observe_return_hook", base + 36, 20, 0x12, 1),
               ("_kui_vmu_observe_boot_control", base + 128, 56, 0x11, 2))
    if sd_lease:
        # --relax leaves a12-byte nominal symbol around this8-byte section;
        # the adjacent normal end callback owns its separate4-byte section.
        SH.pack_into(data, 0x3C8, 0, 1, 6, base + 64, 0x140, 8, 0, 0, 4, 0)
        SH.pack_into(data, 0x3F0, 0, 1, 6, base + 72, 0x148, 4, 0, 0, 4, 0)
        symbols += (("_kui_vmu_observe_sd_lease", base + 64, 12, 0x12, 5),
                    ("_end", base + 72, 4, 0x12, 6),
                    ("__vmu_sd_lease_begin", base + 64, 0, 0x10, 5),
                    ("__vmu_sd_lease_end", base + 72, 0, 0x10, 5))
    SH.pack_into(data, 0x378, 0, 2, 0, 0, 0x500, (len(symbols) + 1) * 16, 4, 1, 4, 16)
    for index, (name, address, size, kind, section) in enumerate(symbols, 1):
        SYM.pack_into(data, 0x500 + index * 16, len(names), address,
                      size, kind, 0, section)
        names.extend(name.encode() + b"\0")
    SH.pack_into(data, 0x3A0, 0, 3, 0, 0, 0x600, len(names), 0, 0, 1, 0)
    data[0x600:0x600 + len(names)] = names
    return data


class ObserverPackageTests(unittest.TestCase):
    def inspect(self, data):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            (directory / "worker.elf").write_bytes(data)
            (directory / "worker.bin").write_bytes(data[0x100:0x180])
            # These fixtures exercise ELF admission independently of the
            # actual authored trampoline, which has its own mutation suite.
            with patch("package_vmu_maple_observe.check_hook", return_value={}), \
                    patch("package_vmu_maple_observe.check_completion_hook", return_value={}):
                return inspect_worker(directory)

    def test_exact_private_layout(self):
        report = self.inspect(worker())
        self.assertEqual(report["binary_bytes"], 128)
        self.assertEqual(report["exports"]["snapshot"], 0xACFD0100)
        self.assertEqual(report["exports"]["readonly_mode"], 0)
        self.assertEqual(report["mode"], "read-only Toy Maple observation")
        self.assertFalse(report["virtual_vmu_enabled"])
        self.assertFalse(report["storage_writes_enabled"])
        self.assertFalse(report["volatile_writes_enabled"])

    def test_readonly_proof_mode_has_explicit_identity_and_no_storage_writes(self):
        report = self.inspect(worker(1))
        self.assertEqual(report["exports"]["readonly_mode"], 1)
        self.assertEqual(report["mode"], "READ-ONLY virtual blank A1 proof")
        self.assertTrue(report["virtual_vmu_enabled"])
        self.assertFalse(report["storage_writes_enabled"])
        self.assertFalse(report["volatile_writes_enabled"])

    def test_sd_readonly_mode_requires_its_typed_transaction_export(self):
        report = self.inspect(worker(2))
        self.assertEqual(report["mode"], "READ-ONLY SD-backed virtual A1 proof")
        self.assertEqual(report["exports"]["sd_transaction"], 0x8CFD0050)
        self.assertTrue(report["virtual_vmu_enabled"])
        self.assertFalse(report["storage_writes_enabled"])
        self.assertFalse(report["volatile_writes_enabled"])
        for at, fmt, value in ((0x100 + 15 * 4, "I", 0),
                               (0x500 + 6 * 16 + 12, "B", 0x11),
                               (0x500 + 6 * 16 + 8, "I", 0)):
            data = worker(2)
            struct.pack_into("<" + fmt, data, at, value)
            with self.subTest(at=at), self.assertRaises(ValueError):
                self.inspect(data)

    def test_volatile_mode_has_separate_identity_and_keeps_sd_storage_readonly(self):
        report = self.inspect(worker(3))
        self.assertEqual(report["exports"]["readonly_mode"], 3)
        self.assertEqual(report["mode"], "VOLATILE RAM-save virtual A1 proof; SD read-only")
        self.assertEqual(report["exports"]["sd_transaction"], 0x8CFD0050)
        self.assertTrue(report["virtual_vmu_enabled"])
        self.assertTrue(report["volatile_writes_enabled"])
        self.assertTrue(report["virtual_writes_enabled"])
        self.assertFalse(report["storage_writes_enabled"])
        self.assertFalse(report["persistence"])
        self.assertEqual(report["private_dma"]["issued_chain"],
                         dict(address="0xacfd0400", bytes=512, alignment=32,
                              physical_address="0x0cfd0400"))
        for at, fmt, value in ((0x100 + 15 * 4, "I", 0),
                               (0x500 + 6 * 16 + 12, "B", 0x11),
                               (0x500 + 6 * 16 + 8, "I", 0)):
            data = worker(3)
            struct.pack_into("<" + fmt, data, at, value)
            with self.subTest(at=at), self.assertRaises(ValueError):
                self.inspect(data)

    def test_abi6_snapshot_is_exact_while_abi5_readonly_profiles_remain_admitted(self):
        for mode in (0, 1, 2):
            self.assertEqual(self.inspect(worker(mode, abi=5))["exports"]["version"], 5)
        with self.assertRaises(ValueError):
            self.inspect(worker(3, abi=5))
        for mode in (0, 1, 2, 3):
            data = worker(mode)
            struct.pack_into("<I", data, 0x500 + 4 * 16 + 8, 644)
            with self.subTest(mode=mode), self.assertRaisesRegex(ValueError, "708-byte"):
                self.inspect(data)

    def test_volatile_dma_objects_require_exact_typed_distinct_private_bounds(self):
        for index, field, value in ((7, 4, 0x8CFD0400), (7, 4, 0xACFD0404),
                                     (7, 4, 0xACFD0120),
                                     (7, 8, 508), (7, 12, 0x12),
                                     (8, 4, 0xACFD0400), (8, 8, 516),
                                     (9, 4, 0xACFD0804), (9, 8, 4092)):
            data = worker(3)
            struct.pack_into("<B" if field == 12 else "<I", data,
                             0x500 + index * 16 + field, value)
            with self.subTest(index=index, field=field), self.assertRaises(ValueError):
                self.inspect(data)
        data = worker(3)
        struct.pack_into("<I", data, 0x100 + 14 * 4, 2)
        with self.assertRaisesRegex(ValueError, "Non-volatile"):
            self.inspect(data)
        for mode in (0, 1):
            data = worker(mode)
            struct.pack_into("<I", data, 0x100 + 15 * 4, 0x8CFD0050)
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                self.inspect(data)

    def test_make_defaults_remain_passive_and_proof_sources_require_explicit_mode(self):
        root = Path(__file__).resolve().parents[1]
        query = "show-mode:\n\t@printf '%s\\n' '$(READONLY_PROOF)' '$(TEST_ZIP)' '$(WORKER_SRC)'\n"
        backend = {"src/core/vmu_readonly.c", "src/core/vmu_emulation.c", "src/core/vmu_image.c",
                   "src/loader/vmu_readonly_overlay.c"}
        with tempfile.TemporaryDirectory() as name:
            profile = Path(name) / "profile.bin";profile.write_bytes(sd_profile_fixture())
            for arguments, expected in (([], "0"), (["READONLY_PROOF=1"], "1"),
                                         (["READONLY_PROOF=2", "SD_PROFILE=" + str(profile)], "2"),
                                         (["READONLY_PROOF=3", "SD_PROFILE=" + str(profile)], "3")):
                with self.subTest(mode=expected):
                    lines = subprocess.check_output(["make", "--no-print-directory", "-s", "-f", "Makefile.vmu-toy",
                                                     "--eval=" + query, *arguments, "show-mode"],
                                                    cwd=root, text=True).splitlines()
                    self.assertEqual(lines[0], expected)
                    self.assertEqual(lines[1], {"0": "vmu-maple-observer-test.zip",
                        "1": "vmu-readonly-card-test.zip", "2": "vmu-sd-readonly-card-test.zip",
                        "3": "vmu-volatile-card-test.zip"}[expected])
                    included = set(lines[2].split())
                    self.assertEqual(included & backend, backend if expected != "0" else set())
        for invalid in ("4", "yes", "0 1"):
            with self.subTest(invalid=invalid):
                result = subprocess.run(["make", "--no-print-directory", "-s", "-f", "Makefile.vmu-toy",
                                         "--eval=" + query, "READONLY_PROOF=" + invalid, "show-mode"],
                                        cwd=root, text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("READONLY_PROOF must be 0, 1, 2 or 3", result.stderr)

    def test_switching_native_build_mode_rebuilds_dependants_immediately(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as name:
            output = Path(name) / "object-mode-fixture"
            profile = Path(name) / "profile.bin";profile.write_bytes(sd_profile_fixture())
            recipe = (f"{output}: {name}/readonly-config\n"
                      "\t@printf '%s' '$(READONLY_PROOF)' > $@\n")
            command = ["make", "--no-print-directory", "-s", "-f", "Makefile.vmu-toy",
                       "--eval=" + recipe, "BUILD=" + name, "SD_PROFILE=" + str(profile), str(output)]
            for mode in ("0", "0", "1", "1", "2", "2", "3", "3", "0"):
                with self.subTest(mode=mode):
                    subprocess.run([*command, "READONLY_PROOF=" + mode], cwd=root, check=True)
                    self.assertEqual(output.read_text(), mode)

    def test_malformed_exports(self):
        for field, value in ((0, 0), (1, 1), (1, 2), (1, 3), (1, 4), (2, 52), (2, 56), (2, 60), (3, 0x8CFD0042),
                             (5, 0xACFD0104), (6, 0xACFD0120),
                             (8, 0xACFE0000), (9, 0x8CFE0020),
                             (10, 0x10000), (11, 0xACFD023C), (13, 0x8CFE0000),
                             (14, 4), (14, 0xFFFFFFFF)):
            data = worker()
            struct.pack_into("<I", data, 0x100 + field * 4, value)
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.inspect(data)

    def test_typed_export_is_required(self):
        for index, offset, value in ((1, 12, 0x11), (2, 8, 0), (4, 8, 540), (4, 8, 612)):
            data = worker()
            struct.pack_into("<B" if offset == 12 else "<I", data,
                             0x500 + index * 16 + offset, value)
            with self.subTest(index=index), self.assertRaises(ValueError):
                self.inspect(data)

    def test_writable_code_and_alias_mismatch_are_rejected(self):
        for offset, value in ((76, 7), (84 + 12, 0xACFD0100),
                               (84 + 8, 0x8CFD0100), (84 + 24, 7)):
            data = worker()
            struct.pack_into("<I", data, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                self.inspect(data)

    def test_objcopy_must_match_elf(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            data = worker()
            (directory / "worker.elf").write_bytes(data)
            (directory / "worker.bin").write_bytes(bytes(128))
            with self.assertRaisesRegex(ValueError, "objcopy"):
                inspect_worker(directory)

    def test_proof_archive_uses_same_retail_route_and_separate_name_and_instructions(self):
        module = b"read-only proof module\0"
        report = json.dumps(self.inspect(worker(1))).encode()
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            target = write_test_archive(directory, module, report, readonly_proof=True)
            self.assertEqual(target.name, "vmu-readonly-card-test.zip")
            with zipfile.ZipFile(target) as archive:
                self.assertEqual(archive.read("KUI/apps/games/retail-boot.kui"), module)
                self.assertEqual(archive.read("observer-artifact-audit.json"), report)
                readme = archive.read("README-FIRST.md").decode()
                self.assertIn("Remove every physical VMU from controller 1", readme)
                self.assertIn("LOAD/card menu", readme)
                self.assertIn("no SD backing", readme)
                self.assertIn("RAM patches Off", readme)
                for line in archive.read("SHA256SUMS.txt").decode().splitlines():
                    digest, entry = line.split("  ", 1)
                    self.assertEqual(hashlib.sha256(archive.read(entry)).hexdigest(), digest)
            passive = write_test_archive(directory, module, b"{}")
            self.assertEqual(passive.name, "vmu-maple-observer-test.zip")
            with zipfile.ZipFile(passive) as archive:
                self.assertIn("Insert a physical VMU", archive.read("README-FIRST.md").decode())

    def test_low_symbol_binding(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / "resident.elf"
            data = low_reader()
            path.write_bytes(data)
            self.assertIn("#define KUI_VMU_OBSERVE_LOW_CONTROL 0x8c004080u", generate(path))
            for index, offset, value in ((2, 8, 0), (3, 12, 0x11),
                                         (4, 8, 52), (4, 12, 0x12),
                                         (4, 4, 0x8C004084)):
                bad = low_reader()
                struct.pack_into("<B" if offset == 12 else "<I", bad,
                                 0x500 + index * 16 + offset, value)
                path.write_bytes(bad)
                with self.subTest(index=index), self.assertRaises(ValueError):
                    generate(path)

    def test_low_control_must_reserve_both_cache_lines(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / "resident.elf"
            data = low_reader()
            # Original faulty layout: the object fits, but its last line
            # has no exclusive reservation and may contain P1 state.
            struct.pack_into("<I", data, 0x350 + 20, 56)
            struct.pack_into("<I", data, 52 + 20, 184)
            path.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "cache lines"):
                generate(path)

    def test_sd_profile_must_match_the_exact_stage_blob(self):
        wire = bytes(sd_profile_fixture())
        stage = {"payload": bytes(32) + wire + bytes(32), "symbols": {
            "__vmu_sd_profile_blob_start": 0x8CE00020,
            "__vmu_sd_profile_blob_end": 0x8CE00620}}
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            (directory / "vmu-sd-profile.bin").write_bytes(wire)
            summary = inspect_sd_profile(directory, stage, 2)
            self.assertEqual(summary["wire_sha256"], hashlib.sha256(wire).hexdigest())
            self.assertEqual(summary["embedded_bytes"], 1536)
            self.assertNotIn("cid", summary)
            self.assertEqual(inspect_sd_profile(directory, stage, 3), summary)
            for mode in (0, 1):
                with self.subTest(mode=mode), self.assertRaises(ValueError):
                    inspect_sd_profile(directory, stage, mode)
            self.assertIsNone(inspect_sd_profile(directory, {"symbols": {}}, 0))
            changed = bytearray(wire);changed[400] ^= 1
            (directory / "vmu-sd-profile.bin").write_bytes(changed)
            with self.assertRaisesRegex(ValueError, "different SD profile"):
                inspect_sd_profile(directory, stage, 2)
            stage["payload"] = bytes(32) + changed + bytes(32)
            with self.assertRaisesRegex(ValueError, "checksum"):
                inspect_sd_profile(directory, stage, 2)
            stage["symbols"]["__vmu_sd_profile_blob_end"] -= 4
            with self.assertRaisesRegex(ValueError, "1536-byte"):
                inspect_sd_profile(directory, stage, 2)

    def test_sd_gateway_requires_typed_code_and_no_incoming_resident_reference(self):
        elf = bytes(low_reader(True))
        resident = inspect_elf(elf, 0x8C004000, 0x8C007800)
        graph = 'node: { title: "kui_vmu_observe_sd_lease" label: "lease\\n16 bytes (static)" }\n'
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name);reports = directory / "resident/lto";reports.mkdir(parents=True)
            path = reports / "resident.elf.ltrans0.ltrans.ci";path.write_text(graph)
            proof = check_sd_gateway(directory, resident, elf)
            self.assertEqual(proof["incoming_resident_edges"], 0)
            self.assertEqual(proof["bytes"], 8)
            self.assertEqual(proof["declared_symbol_bytes"], 12)
            # An actual neighbouring routine is not gateway code, even when
            # the pre-relaxation ST_SIZE overlaps its independent section.
            neighbour = dict(resident, payload=bytearray(resident["payload"]))
            struct.pack_into("<I", neighbour["payload"], 100, 0x8C004048)
            self.assertEqual(check_sd_gateway(directory, neighbour, elf)["bytes"], 8)
            with tempfile.TemporaryDirectory() as sym_name:
                sym_path = Path(sym_name) / "resident.elf";sym_path.write_bytes(elf)
                self.assertIn("KUI_VMU_OBSERVE_LOW_SD_LEASE 0x8c004040u", generate(sym_path))
            path.write_text(graph + 'edge: { sourcename: "kui_retail_resident_dispatch" '
                            'targetname: "kui_vmu_observe_sd_lease" label: "resident.c:1" }\n')
            with self.assertRaisesRegex(ValueError, "Resident call enters"):
                check_sd_gateway(directory, resident, elf)
            path.write_text(graph)
            for value in (0x8C004040, 0x0C004040, 0xAC004046):
                changed = dict(resident, payload=bytearray(resident["payload"]))
                struct.pack_into("<I", changed["payload"], 100, value)
                with self.subTest(value=value), self.assertRaisesRegex(ValueError, "literal references"):
                    check_sd_gateway(directory, changed, elf)
            bad = bytearray(elf);struct.pack_into("<B", bad, 0x500 + 5 * 16 + 12, 0x11)
            with self.assertRaisesRegex(ValueError, "bounded typed"):
                check_sd_gateway(directory, resident, bad)
            path.unlink()
            with self.assertRaisesRegex(ValueError, "callgraph proof"):
                check_sd_gateway(directory, resident, elf)

    def test_sd_cross_image_stack_keeps_low_budget_and_counts_resident_callbacks(self):
        roots = ("kui_retail_resident_dispatch", "kui_retail_gd_dispatch",
                 "kui_retail_image_read", "kui_loader_sd_stream_next")
        def row(name, count, kind="static"):
            return f"unit.c:1:1:{name}\t{count}\t{kind}\n"
        resident = {"symbols": {"_" + name: 1 for name in (*roots, "kui_vmu_observe_sd_lease")}}
        worker_image = {"symbols": {"_worker_root": 1}}
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name);(directory / "resident/lto").mkdir(parents=True)
            (directory / "worker").mkdir()
            low = directory / "resident/lto/resident.elf.ltrans0.ltrans.su"
            high = directory / "worker/worker.su"
            low.write_text("".join(row(root, 244) for root in roots) + row("kui_vmu_observe_sd_lease", 512))
            high.write_text(row("worker_root", 1000) + row("unlinked_collect_hashes", 9999, "dynamic"))
            proof = {"incoming_resident_edges": 0}
            low_proof, high_proof = check_sd_stacks(directory, resident, worker_image, proof)
            self.assertEqual(low_proof["conservative_bytes"], 1232)
            self.assertEqual(low_proof["available_bytes"], 1232)
            self.assertEqual(high_proof["resident_callback_c_frame_sum"], 1488)
            self.assertEqual(high_proof["conservative_bytes"], 2604)
            high.write_text(row("worker_root", 3000))
            with self.assertRaisesRegex(ValueError, "worker/gateway/bus"):
                check_sd_stacks(directory, resident, worker_image, proof)
            high.write_text(row("worker_root", 1000))
            low.write_text(low.read_text().replace("\t244\t", "\t248\t", 1))
            with self.assertRaisesRegex(ValueError, "Resident conservative"):
                check_sd_stacks(directory, resident, worker_image, proof)
            high.write_text(row("worker_root", 4, "dynamic"))
            with self.assertRaisesRegex(ValueError, "Unbounded linked"):
                linked_stack_rows([high], worker_image["symbols"])

    def test_sd_archive_has_separate_identity_and_excludes_other_mode_selectors(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            with patch("package_vmu_maple_observe.Path.read_bytes", return_value=b"SD test guide"):
                target = write_test_archive(directory, b"module", b"{}", sd_readonly_proof=True)
            self.assertEqual(target.name, "vmu-sd-readonly-card-test.zip")
            with zipfile.ZipFile(target) as archive:
                self.assertEqual(archive.read("README-FIRST.md"), b"SD test guide")
                self.assertEqual(archive.read("KUI/apps/games/retail-boot.kui"), b"module")
            with self.assertRaises(ValueError):
                write_test_archive(directory, b"module", b"{}", readonly_proof=True, sd_readonly_proof=True)

    def test_worker_stack_coverage_rejects_missing_volatile_chain_and_guard_frames(self):
        c_names = ("kui_vmu_volatile_handle", "kui_vmu_volatile_chain_build",
                   "kui_vmu_volatile_chain_verify", "kui_vmu_emulation_handle",
                   "ram_packet", "ram_admit", "chain_status", "chain_fail",
                   "chain_physical", "original_fingerprint", "physical_response_valid",
                   "source_span", "proof_layout", "read_block", "commit_ram",
                   "shape", "slot", "geometry", "classify", "receive_geometry",
                   "kui_vmu_sd_identity_read.constprop.0")
        assembly = ("kui_vmu_maple_observer_hook", "kui_vmu_maple_observer_complete_hook",
                    "kui_vmu_maple_observer_terminal")
        symbols = {"_" + name: 0x8CFD0040 + index * 4
                   for index, name in enumerate((*c_names, *assembly))}
        symbols["_chain_state"] = 0xACFD0120  # Private data needs no stack row.
        symbols["_kui_vmu_maple_observer_snapshot"] = 0xACFD0200
        rows = [(name, 32) for name in c_names]
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            self.assertEqual(require_worker_stack_coverage(directory, {"symbols": symbols}, rows), len(rows))
            for index, function in enumerate(c_names):
                with self.subTest(function=function), self.assertRaisesRegex(ValueError, "Missing linked worker"):
                    require_worker_stack_coverage(directory, {"symbols": symbols}, rows[:index] + rows[index + 1:])
            # A base function's frame cannot substitute for a missing clone.
            altered = rows[:-1] + [("kui_vmu_sd_identity_read", 32)]
            with self.assertRaisesRegex(ValueError, "constprop"):
                require_worker_stack_coverage(directory, {"symbols": symbols}, altered)

    def test_native_stack_audit_rejects_missing_volatile_source_report(self):
        roots = ("kui_retail_resident_dispatch", "kui_retail_gd_dispatch",
                 "kui_retail_image_read", "kui_loader_sd_stream_next")
        resident = {"symbols": {"_" + name: 1 for name in (*roots, "kui_vmu_observe_sd_lease")}}
        worker_image = {"symbols": {"_kui_vmu_maple_observer_visit": 0x8CFD0040,
                                    "_kui_vmu_volatile_handle": 0x8CFD0080,
                                    "_commit_ram": 0x8CFD00C0,
                                    "_kui_vmu_volatile_chain_build": 0x8CFD0100}}
        def row(name):
            return f"unit.c:1:1:{name}\t16\tstatic\n"
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            low = directory / "resident/lto/runtime.ltrans0.ltrans.su"
            low.parent.mkdir(parents=True)
            low.write_text("".join(row(name) for name in (*roots, "kui_vmu_observe_sd_lease")))
            high = directory / "worker/worker.su";high.parent.mkdir()
            high.write_text(row("kui_vmu_maple_observer_visit"))
            volatile = directory / "worker/vmu_volatile.su"
            volatile.write_text(row("kui_vmu_volatile_handle") + row("commit_ram"))
            chain = directory / "worker/vmu_volatile_chain.su"
            chain.write_text(row("kui_vmu_volatile_chain_build"))
            _, report = check_sd_stacks(directory, resident, worker_image, {})
            self.assertEqual(report["required_worker_frame_coverage"], 4)
            for path in (volatile, chain):
                before = path.read_text()
                path.unlink()
                with self.subTest(source=path.name), self.assertRaisesRegex(ValueError, "Missing linked worker"):
                    check_sd_stacks(directory, resident, worker_image, {})
                path.write_text(before)

    def test_volatile_archive_has_distinct_name_guide_and_exclusive_selector(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            with patch("package_vmu_maple_observe.Path.read_bytes", return_value=b"Volatile RAM saves; no SD writes"):
                target = write_test_archive(directory, b"volatile module", b"{}", volatile_proof=True)
            self.assertEqual(target.name, "vmu-volatile-card-test.zip")
            with zipfile.ZipFile(target) as archive:
                self.assertEqual(archive.read("README-FIRST.md"), b"Volatile RAM saves; no SD writes")
                self.assertEqual(archive.read("KUI/apps/games/retail-boot.kui"), b"volatile module")
                self.assertNotIn("KUI/tools.kui", archive.namelist())
            for other in ("readonly_proof", "sd_readonly_proof"):
                with self.assertRaises(ValueError):
                    write_test_archive(directory, b"module", b"{}", volatile_proof=True, **{other: True})
            for invalid in (1, "3", None):
                with self.assertRaises(ValueError):
                    write_test_archive(directory, b"module", b"{}", volatile_proof=invalid)

    def test_personalized_archive_binds_mode_module_and_original_compile_record(self):
        original_read = Path.read_bytes
        def read_guide(path):
            if path.parent == Path("docs"):
                return b"Synthetic test instructions"
            return original_read(path)
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            profile = root / "profile.bin";profile.write_bytes(owner_profile())
            for mode in (2, 3):
                base = root / ("base" + str(mode))
                directory = root / ("personalized" + str(mode))
                files = build_fixture(base, mode=mode)
                derivation = personalize(base, profile, directory,
                    expected_commit=COMMIT, expected_mode=mode)
                module = (directory / "retail-maple-observe.kui").read_bytes()
                selected = {"sd_readonly_proof" if mode == 2 else "volatile_proof": True}
                wrong = {"volatile_proof" if mode == 2 else "sd_readonly_proof": True}
                with patch.object(Path, "read_bytes", read_guide):
                    target = write_test_archive(directory, module, b"{}", **selected)
                    with self.assertRaisesRegex(ValueError, "Personalization mode"):
                        write_test_archive(directory, module, b"{}", **wrong)
                    with self.assertRaisesRegex(ValueError, "Archive module"):
                        write_test_archive(directory, module[:-4] + bytes(4), b"{}", **selected)
                with zipfile.ZipFile(target) as archive:
                    self.assertEqual(archive.read("KUI/apps/games/retail-boot.kui"), module)
                    self.assertEqual(json.loads(archive.read("personalization-audit.json")), derivation)
                    self.assertEqual(archive.read("base-provenance/entry.compile.json"),
                                     files["entry.compile.json"])
                    for line in archive.read("SHA256SUMS.txt").decode().splitlines():
                        digest, entry = line.split("  ", 1)
                        self.assertEqual(hashlib.sha256(archive.read(entry)).hexdigest(), digest)


if __name__ == "__main__":
    unittest.main()
