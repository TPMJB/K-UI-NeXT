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
from package_vmu_maple_observe import inspect_worker, write_test_archive
from vmu_maple_observe_symbols import generate


def worker(readonly_mode=0):
    base = 0x8CFD0000
    private = base + 0x20000000
    data = bytearray(4096)
    EH.pack_into(data, 0, b"\x7fELF\x01\x01\x01" + bytes(9), 2, 42, 1,
                 base + 64, 52, 0x300, 0, 52, 32, 2, 40, 6, 0)
    PH.pack_into(data, 52, 1, 0x100, base, base, 128, 128, 5, 32)
    PH.pack_into(data, 84, 1, 0x200, private + 0x100, base + 0x100,
                 0, 0x1280, 6, 32)
    SH.pack_into(data, 0x328, 0, 1, 6, base, 0x100, 128, 0, 0, 32, 0)
    SH.pack_into(data, 0x350, 0, 8, 3, private + 0x100, 0x200, 640, 0, 0, 32, 0)
    SH.pack_into(data, 0x378, 0, 8, 3, private + 0x380, 0x200, 4096, 0, 0, 32, 0)
    SH.pack_into(data, 0x3A0, 0, 2, 0, 0, 0x500, 6 * 16, 5, 1, 4, 16)
    names = bytearray(b"\0")
    symbols = (("_kui_vmu_maple_observer_init", base + 64, 2, 0x12, 1),
               ("_kui_vmu_maple_observer_hook", base + 68, 2, 0x12, 1),
               ("_kui_vmu_maple_observer_terminal", base + 72, 2, 0x12, 1),
               ("_kui_vmu_maple_observer_snapshot", private + 0x100, 612, 0x11, 2),
               ("_kui_vmu_maple_observer_complete_hook", base + 76, 2, 0x12, 1))
    for index, (name, address, size, kind, section) in enumerate(symbols, 1):
        SYM.pack_into(data, 0x500 + index * 16, len(names), address,
                      size, kind, 0, section)
        names.extend(name.encode() + b"\0")
    SH.pack_into(data, 0x3C8, 0, 3, 0, 0, 0x600, len(names), 0, 0, 1, 0)
    data[0x600:0x600 + len(names)] = names
    exports = (0x564D4F31, 4, 60, base + 64, base + 68,
               private + 0x100, private + 0x380, private + 0x380,
               private + 0x1380, base + 0x1380, 0x30000,
               private + 0x100, base + 72, base + 76, readonly_mode)
    struct.pack_into("<15I", data, 0x100, *exports)
    return data


def low_reader():
    base = 0x8C004000
    data = bytearray(4096)
    EH.pack_into(data, 0, b"\x7fELF\x01\x01\x01" + bytes(9), 2, 42, 1,
                 base, 52, 0x300, 0, 52, 32, 1, 40, 5, 0)
    PH.pack_into(data, 52, 1, 0x100, base, base, 128, 192, 7, 4)
    SH.pack_into(data, 0x328, 0, 1, 6, base, 0x100, 128, 0, 0, 4, 0)
    SH.pack_into(data, 0x350, 0, 8, 3, base + 128, 0x180, 64, 0, 0, 32, 0)
    SH.pack_into(data, 0x378, 0, 2, 0, 0, 0x500, 5 * 16, 4, 1, 4, 16)
    names = bytearray(b"\0")
    symbols = (("_start", base, 4, 0x12, 1),
               ("_kui_vmu_observe_heap_hook", base + 4, 32, 0x12, 1),
               ("_kui_vmu_observe_return_hook", base + 36, 20, 0x12, 1),
               ("_kui_vmu_observe_boot_control", base + 128, 52, 0x11, 2))
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

    def test_readonly_proof_mode_has_explicit_identity_and_no_storage_writes(self):
        report = self.inspect(worker(1))
        self.assertEqual(report["exports"]["readonly_mode"], 1)
        self.assertEqual(report["mode"], "READ-ONLY virtual blank A1 proof")
        self.assertTrue(report["virtual_vmu_enabled"])
        self.assertFalse(report["storage_writes_enabled"])

    def test_make_defaults_remain_passive_and_proof_sources_require_explicit_mode(self):
        root = Path(__file__).resolve().parents[1]
        query = "show-mode:\n\t@printf '%s\\n' '$(READONLY_PROOF)' '$(TEST_ZIP)' '$(WORKER_SRC)'\n"
        backend = {"src/core/vmu_readonly.c", "src/core/vmu_emulation.c", "src/core/vmu_image.c",
                   "src/loader/vmu_readonly_overlay.c"}
        for arguments, expected in (([], "0"), (["READONLY_PROOF=1"], "1")):
            with self.subTest(mode=expected):
                lines = subprocess.check_output(["make", "--no-print-directory", "-s", "-f", "Makefile.vmu-toy",
                                                 "--eval=" + query, *arguments, "show-mode"],
                                                cwd=root, text=True).splitlines()
                self.assertEqual(lines[0], expected)
                self.assertEqual(lines[1], "vmu-readonly-card-test.zip" if expected == "1" else
                                 "vmu-maple-observer-test.zip")
                included = set(lines[2].split())
                self.assertEqual(included & backend, backend if expected == "1" else set())
        for invalid in ("2", "yes", "0 1"):
            with self.subTest(invalid=invalid):
                result = subprocess.run(["make", "--no-print-directory", "-s", "-f", "Makefile.vmu-toy",
                                         "--eval=" + query, "READONLY_PROOF=" + invalid, "show-mode"],
                                        cwd=root, text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("READONLY_PROOF must be 0 or 1", result.stderr)

    def test_switching_native_build_mode_rebuilds_dependants_immediately(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as name:
            output = Path(name) / "object-mode-fixture"
            recipe = (f"{output}: {name}/readonly-config\n"
                      "\t@printf '%s' '$(READONLY_PROOF)' > $@\n")
            command = ["make", "--no-print-directory", "-s", "-f", "Makefile.vmu-toy",
                       "--eval=" + recipe, "BUILD=" + name, str(output)]
            for mode in ("0", "0", "1", "1", "0"):
                with self.subTest(mode=mode):
                    subprocess.run([*command, "READONLY_PROOF=" + mode], cwd=root, check=True)
                    self.assertEqual(output.read_text(), mode)

    def test_malformed_exports(self):
        for field, value in ((0, 0), (1, 1), (1, 2), (1, 3), (2, 52), (2, 56), (3, 0x8CFD0042),
                             (5, 0xACFD0104), (6, 0xACFD0120),
                             (8, 0xACFE0000), (9, 0x8CFE0020),
                             (10, 0x10000), (11, 0xACFD023C), (13, 0x8CFE0000),
                             (14, 2), (14, 0xFFFFFFFF)):
            data = worker()
            struct.pack_into("<I", data, 0x100 + field * 4, value)
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.inspect(data)

    def test_typed_export_is_required(self):
        for index, offset, value in ((1, 12, 0x11), (2, 8, 0), (4, 8, 540), (4, 8, 608)):
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
                                         (4, 8, 48), (4, 12, 0x12),
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
            struct.pack_into("<I", data, 0x350 + 20, 52)
            struct.pack_into("<I", data, 52 + 20, 180)
            path.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "cache lines"):
                generate(path)


if __name__ == "__main__":
    unittest.main()
