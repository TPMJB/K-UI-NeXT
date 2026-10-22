#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise full selected-image package validation without a SH toolchain."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_loader_layout import EH, PH, SH, SYM, HIGH, LOW
from check_image_loader_layout import (BLOB_OFFSET, CLIENT_STACK, HEADER,
                                       HOOK_STACK, MANIFEST_BYTES,
                                       MANIFEST_OFFSET, RESIDENT_STACK,
                                       VECTOR, check_directory)


def executable(base, payload, memory, symbols):
    """Small static ELF containing one allocated segment and real symbols."""
    strings = bytearray(b"\0")
    syms = bytearray(SYM.size)
    for label, value in {"_start": base, **symbols}.items():
        name = len(strings)
        strings += label.encode("ascii") + b"\0"
        syms += SYM.pack(name, value, 0, 0x12, 0, 0xFFF1)
    section_at = (0x100 + len(payload) + 15) & ~15
    strings_at = section_at + SH.size * 3
    symbols_at = (strings_at + len(strings) + 3) & ~3
    data = bytearray(symbols_at + len(syms))
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    EH.pack_into(data, 0, ident, 2, 42, 1, base, EH.size, section_at, 0,
                 EH.size, PH.size, 1, SH.size, 3, 0)
    PH.pack_into(data, EH.size, 1, 0x100, base, base, len(payload), memory, 7, 4)
    SH.pack_into(data, section_at + SH.size, 0, 2, 0, 0,
                 symbols_at, len(syms), 2, 1, 4, SYM.size)
    SH.pack_into(data, section_at + SH.size * 2, 0, 3, 0, 0,
                 strings_at, len(strings), 0, 0, 1, 0)
    data[0x100:0x100 + len(payload)] = payload
    data[strings_at:strings_at + len(strings)] = strings
    data[symbols_at:symbols_at + len(syms)] = syms
    return data


class ImageLayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.payload = {}
        self.symbols = {}
        self.memory = {}
        client = bytearray(128)
        client[:4] = b"test"
        struct.pack_into("<I", client, 0x58, VECTOR)
        self.payload["image_client"] = client
        self.memory["image_client"] = 0x1100
        self.symbols["image_client"] = {
            "__binary_end": LOW + len(client), "__bss_start": LOW + len(client),
            "__bss_end": LOW + 0x1100,
            "_kui_image_client_main": LOW + 0x10,
            "_kui_image_client_gd_call": LOW + 0x40,
            "__image_gd_vector_address": VECTOR,
            "__image_gd_vector_literal": LOW + 0x58,
        }
        resident = bytearray(256) + client
        resident[:4] = b"test"
        self.payload["image_resident"] = resident
        self.memory["image_resident"] = len(resident) + 53248
        self.symbols["image_resident"] = {
            "__binary_end": HIGH + len(resident), "__bss_start": HIGH + len(resident),
            "__bss_end": HIGH + self.memory["image_resident"],
            "__client_image_start": HIGH + 256, "__client_image_end": HIGH + len(resident),
            "__image_gd_hook_stack": HOOK_STACK,
        }
        for i, name in enumerate(("kui_image_resident_main", "kui_image_gd_hook",
                                  "kui_image_gd_dispatch", "kui_resident_image_read",
                                  "kui_gd_service_dispatch", "kui_loader_sd_read")):
            self.symbols["image_resident"]["_" + name] = HIGH + 0x10 + i * 8
        entry = bytearray(BLOB_OFFSET) + resident
        entry[:4] = b"test"
        entry[0x100:0x140] = HEADER.pack(
            b"KUIIMG01", 1, 64, MANIFEST_OFFSET, MANIFEST_BYTES, HIGH,
            len(resident), HIGH, LOW, CLIENT_STACK, RESIDENT_STACK,
            BLOB_OFFSET, 0x100000, 0, 0)
        self.payload["image_entry"] = entry
        self.memory["image_entry"] = len(entry)
        self.symbols["image_entry"] = {
            "__loader_manifest": LOW + MANIFEST_OFFSET,
            "__resident_blob_start": LOW + BLOB_OFFSET,
            "__resident_blob_end": LOW + len(entry),
        }
        self.write()

    def tearDown(self):
        self.temp.cleanup()

    def write(self, names=None):
        for name in names or self.payload:
            base = HIGH if name == "image_resident" else LOW
            (self.directory / (name + ".elf")).write_bytes(executable(
                base, self.payload[name], self.memory[name], self.symbols[name]))
            if name != "image_entry":
                (self.directory / (name + ".bin")).write_bytes(self.payload[name])

    def test_valid_three_image_package(self):
        result = check_directory(self.directory)
        self.assertEqual(set(result), set(self.payload))
        self.assertEqual(result["image_entry"]["payload_bytes"], BLOB_OFFSET + 384)

    def test_inner_header_mismatch(self):
        for offset in (0, 8, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60):
            self.payload["image_entry"][0x100 + offset] ^= 1
            self.write(["image_entry"])
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "header"):
                check_directory(self.directory)
            self.payload["image_entry"][0x100 + offset] ^= 1

    def test_missing_native_entry_or_hook(self):
        for name, symbol in (("image_client", "_kui_image_client_main"),
                             ("image_client", "_kui_image_client_gd_call"),
                             ("image_resident", "_kui_image_resident_main"),
                             ("image_resident", "_kui_image_gd_hook"),
                             ("image_resident", "_kui_image_gd_dispatch")):
            old = self.symbols[name].pop(symbol)
            self.write([name])
            with self.subTest(symbol=symbol), self.assertRaisesRegex(ValueError, "symbol"):
                check_directory(self.directory)
            self.symbols[name][symbol] = old
            self.write([name])

    def test_wrong_actual_vector_literal(self):
        self.payload["image_client"][0x58] ^= 4
        self.write(["image_client"])
        with self.assertRaisesRegex(ValueError, "BIOS vector"):
            check_directory(self.directory)

    def test_wrong_vector_symbol(self):
        self.symbols["image_client"]["__image_gd_vector_address"] += 4
        self.write(["image_client"])
        with self.assertRaisesRegex(ValueError, "BIOS vector"):
            check_directory(self.directory)

    def test_wrong_hook_stack(self):
        self.symbols["image_resident"]["__image_gd_hook_stack"] = RESIDENT_STACK
        self.write(["image_resident"])
        with self.assertRaisesRegex(ValueError, "separate resident stack"):
            check_directory(self.directory)

    def test_resident_bss_cannot_enter_hook_stack(self):
        self.memory["image_resident"] = 0x1C0004
        self.symbols["image_resident"]["__bss_end"] = HIGH + 0x1C0004
        self.write(["image_resident"])
        with self.assertRaisesRegex(ValueError, "reservation"):
            check_directory(self.directory)

    def test_reversed_bss(self):
        self.symbols["image_client"]["__bss_start"] = LOW + 0x1104
        self.write(["image_client"])
        with self.assertRaisesRegex(ValueError, "BSS"):
            check_directory(self.directory)

    def test_changed_embed(self):
        self.payload["image_resident"][-1] ^= 1
        self.write(["image_resident"])
        with self.assertRaisesRegex(ValueError, "embedded client"):
            check_directory(self.directory)

    def test_changed_packed_resident(self):
        self.payload["image_entry"][-1] ^= 1
        self.write(["image_entry"])
        with self.assertRaisesRegex(ValueError, "packed"):
            check_directory(self.directory)

    def test_populated_manifest(self):
        self.payload["image_entry"][MANIFEST_OFFSET + MANIFEST_BYTES - 1] = 1
        self.write(["image_entry"])
        with self.assertRaisesRegex(ValueError, "must be empty"):
            check_directory(self.directory)

    def test_changed_flat_binary(self):
        path = self.directory / "image_resident.bin"
        data = bytearray(path.read_bytes())
        data[0] ^= 1
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "does not match"):
            check_directory(self.directory)

    def test_dynamic_image(self):
        path = self.directory / "image_client.elf"
        data = bytearray(path.read_bytes())
        struct.pack_into("<I", data, EH.size, 2)
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "Dynamic"):
            check_directory(self.directory)

    def test_unresolved_symbol(self):
        path = self.directory / "image_resident.elf"
        data = bytearray(path.read_bytes())
        section_at = struct.unpack_from("<I", data, 32)[0]
        symbol_at = SH.unpack_from(data, section_at + SH.size)[4]
        struct.pack_into("<H", data, symbol_at + SYM.size + 14, 0)
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "Unresolved"):
            check_directory(self.directory)

    def test_linked_kos_runtime(self):
        self.symbols["image_resident"]["_thd_sleep"] = HIGH + 0x60
        self.write(["image_resident"])
        with self.assertRaisesRegex(ValueError, "KOS/runtime"):
            check_directory(self.directory)


if __name__ == "__main__":
    unittest.main()
