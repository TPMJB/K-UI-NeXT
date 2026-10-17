#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Architectural state and mutation checks for the authored SH4 trampoline."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_vmu_maple_observe_hook import check_hook


def fixture():
    # Our authored vmu_maple_hook.S assembled as SH4, with three local RTS
    # stubs for C entry points. This fixture contains no owner-game bytes.
    payload = bytes.fromhex(
        "314f4d560100000034000000e000fd8c3400fd8c0001fdac2002fdac2002fdac"
        "2012fdac2012fd8c000003000001fdacc800fd8c162f0201162f1360f0cb0e40"
        "262f362f462f562f662f762f862f962fa62fb62fc62fd62fe62f224f024f124f"
        "134ff36413df462f43604470026412d00b400900f26f174f164f064f264ff66e"
        "f66df66cf66bf66af669f668f667f666f665f664f663f662f6610e41f66107d0"
        "e2633e02c61206d0e263262f05d22b42f66209002012fdace400fd8cf4100000"
        "d80f000070f20b8c0200f0cb0e4002df02d02b40090009002012fdace800fd8c"
        "0b0009000b0009000b000900")
    keys = ("magic", "version", "bytes", "initialize", "hook", "bss_begin",
            "bss_end", "stack_bottom", "stack_top", "worker_end", "lease_bytes",
            "snapshot", "terminal")
    exports = dict(zip(keys, struct.unpack_from("<13I", payload)))
    return {"payload": payload, "symbols": {"_kui_vmu_maple_observer_visit": 0x8CFD00E4}}, exports


class ObserverHookTests(unittest.TestCase):
    def test_full_inherited_integer_state(self):
        image, exports = fixture()
        result = check_hook(image, exports)
        self.assertEqual(result["cases"], 128)
        self.assertEqual(result["original_dma_start_writes"], 1)

    def mutate(self, before, after):
        image, exports = fixture()
        data = bytearray(image["payload"])
        original = struct.pack("<H", before)
        self.assertEqual(data.count(original), 1)
        at = data.index(original)
        struct.pack_into("<H", data, at, after)
        image["payload"] = data
        with self.assertRaises(ValueError):
            check_hook(image, exports)

    def test_missing_original_start_store_is_rejected(self):
        self.mutate(0x12C6, 0x0009)

    def test_missing_sr_restore_is_rejected(self):
        self.mutate(0x410E, 0x0009)

    def test_wrong_frame_offset_is_rejected(self):
        self.mutate(0x7044, 0x7040)

    def test_wrong_saved_register_is_rejected(self):
        self.mutate(0x64F6, 0x65F6)

    def test_bad_c_entry_is_rejected(self):
        image, exports = fixture()
        image["symbols"].clear()
        with self.assertRaisesRegex(ValueError, "visit symbol"):
            check_hook(image, exports)


if __name__ == "__main__":
    unittest.main()
