#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject observer binaries with mismatched exports or private memory bounds."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_loader_layout import EH, PH, SH, SYM
from package_vmu_maple_observe import inspect_worker
from vmu_maple_observe_symbols import generate


def worker():
    base = 0x8CFD0000
    private = base + 0x20000000
    data = bytearray(4096)
    EH.pack_into(data, 0, b"\x7fELF\x01\x01\x01" + bytes(9), 2, 42, 1,
                 base + 64, 52, 0x300, 0, 52, 32, 2, 40, 6, 0)
    PH.pack_into(data, 52, 1, 0x100, base, base, 128, 128, 5, 32)
    PH.pack_into(data, 84, 1, 0x200, private + 0x100, base + 0x100,
                 0, 0x1140, 6, 32)
    SH.pack_into(data, 0x328, 0, 1, 6, base, 0x100, 128, 0, 0, 32, 0)
    SH.pack_into(data, 0x350, 0, 8, 3, private + 0x100, 0x200, 320, 0, 0, 32, 0)
    SH.pack_into(data, 0x378, 0, 8, 3, private + 0x240, 0x200, 4096, 0, 0, 32, 0)
    SH.pack_into(data, 0x3A0, 0, 2, 0, 0, 0x500, 5 * 16, 5, 1, 4, 16)
    names = bytearray(b"\0")
    symbols = (("_kui_vmu_maple_observer_init", base + 64, 2, 0x12, 1),
               ("_kui_vmu_maple_observer_hook", base + 68, 2, 0x12, 1),
               ("_kui_vmu_maple_observer_terminal", base + 72, 2, 0x12, 1),
               ("_kui_vmu_maple_observer_snapshot", private + 0x100, 272, 0x11, 2))
    for index, (name, address, size, kind, section) in enumerate(symbols, 1):
        SYM.pack_into(data, 0x500 + index * 16, len(names), address,
                      size, kind, 0, section)
        names.extend(name.encode() + b"\0")
    SH.pack_into(data, 0x3C8, 0, 3, 0, 0, 0x600, len(names), 0, 0, 1, 0)
    data[0x600:0x600 + len(names)] = names
    exports = (0x564D4F31, 1, 52, base + 64, base + 68,
               private + 0x100, private + 0x240, private + 0x240,
               private + 0x1240, base + 0x1240, 0x30000,
               private + 0x100, base + 72)
    struct.pack_into("<13I", data, 0x100, *exports)
    return data


def low_reader():
    base = 0x8C004000
    data = bytearray(4096)
    EH.pack_into(data, 0, b"\x7fELF\x01\x01\x01" + bytes(9), 2, 42, 1,
                 base, 52, 0x300, 0, 52, 32, 1, 40, 5, 0)
    PH.pack_into(data, 52, 1, 0x100, base, base, 128, 180, 7, 4)
    SH.pack_into(data, 0x328, 0, 1, 6, base, 0x100, 128, 0, 0, 4, 0)
    SH.pack_into(data, 0x350, 0, 8, 3, base + 128, 0x180, 52, 0, 0, 4, 0)
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
            with patch("package_vmu_maple_observe.check_hook", return_value={}):
                return inspect_worker(directory)

    def test_exact_private_layout(self):
        report = self.inspect(worker())
        self.assertEqual(report["binary_bytes"], 128)
        self.assertEqual(report["exports"]["snapshot"], 0xACFD0100)

    def test_malformed_exports(self):
        for field, value in ((0, 0), (2, 48), (3, 0x8CFD0042),
                             (5, 0xACFD0104), (6, 0xACFD0120),
                             (8, 0xACFE0000), (9, 0x8CFE0020),
                             (10, 0x10000), (11, 0xACFD023C)):
            data = worker()
            struct.pack_into("<I", data, 0x100 + field * 4, value)
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.inspect(data)

    def test_typed_export_is_required(self):
        for index, offset, value in ((1, 12, 0x11), (2, 8, 0), (4, 8, 268)):
            data = worker()
            struct.pack_into("<I", data, 0x500 + index * 16 + offset, value)
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

    def test_low_symbol_binding(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / "resident.elf"
            data = low_reader()
            path.write_bytes(data)
            self.assertIn("#define KUI_VMU_OBSERVE_LOW_CONTROL 0x8c004080u", generate(path))
            for index, offset, value in ((2, 8, 0), (3, 12, 0x11),
                                         (4, 8, 48), (4, 12, 0x12)):
                bad = low_reader()
                struct.pack_into("<I", bad, 0x500 + index * 16 + offset, value)
                path.write_bytes(bad)
                with self.subTest(index=index), self.assertRaises(ValueError):
                    generate(path)


if __name__ == "__main__":
    unittest.main()
