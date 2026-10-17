#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Architectural state and mutation checks for the authored SH4 trampoline."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_vmu_maple_observe_hook import check_hook, check_completion_hook


def fixture():
    # Our authored vmu_maple_hook.S assembled as SH4, with four local RTS
    # stubs for C entry points. This fixture contains no owner-game bytes.
    payload = bytes.fromhex(
        "314f4d56040000003c0000006c01fd8c3c00fd8c8001fdac0004fdac0004fdac"
        "0014fdac0014fd8c000003008001fdac5401fd8cd000fd8c00000000162f0201"
        "162f1360f0cb0e40262f362f462f562f662f762f862f962fa62fb62fc62fd62f"
        "e62f224f024f124f134ff36413df462f43604470026412d00b400900f26f174f"
        "164f064f264ff66ef66df66cf66bf66af669f668f667f666f665f664f663f662"
        "f6610e41f66107d0e2633e02c61206d0e263262f05d22b42f66209000014fdac"
        "7001fd8cf4100000d80f000070f20b8c062f162f0201162f1360f0cb0e40262f"
        "362f462f562f662f762f862f962fa62fb62fc62fd62fe62f224f024f124f134f"
        "f36411df462f43554360447002640fd00b400900f26f174f164f064f264ff66e"
        "f66df66cf66bf66af669f668f667f666f665f664f663f662f6610e41f661f660"
        "03d32b43090009000014fdac7401fd8c7af30b8c0200f0cb0e4002df02d02b40"
        "090009000014fdac7801fd8c0b0009000b0009000b0009000b000900")
    keys = ('magic', 'version', 'bytes', 'initialize', 'hook', 'bss_begin', 'bss_end', 'stack_bottom', 'stack_top', 'worker_end', 'lease_bytes', 'snapshot', 'terminal', 'complete_hook', 'readonly_mode')
    exports = dict(zip(keys, struct.unpack_from("<15I", payload)))
    return {"payload": payload, "symbols": {'_kui_vmu_maple_observer_visit': 2365391216, '_kui_vmu_maple_observer_complete': 2365391220}}, exports


class ObserverHookTests(unittest.TestCase):
    def test_full_inherited_integer_state(self):
        image, exports = fixture()
        result = check_hook(image, exports)
        self.assertEqual(result["cases"], 128)
        self.assertEqual(result["original_dma_start_writes"], 1)

    def mutate(self, before, after, *, completion=False):
        image, exports = fixture()
        data = bytearray(image["payload"])
        original = struct.pack("<H", before)
        base = 0x8CFD0000
        begin = exports["complete_hook" if completion else "hook"] - base
        end = exports["terminal" if completion else "complete_hook"] - base
        self.assertEqual(data[begin:end].count(original), 1)
        at = data.index(original, begin, end)
        struct.pack_into("<H", data, at, after)
        image["payload"] = data
        with self.assertRaises(ValueError):
            (check_completion_hook if completion else check_hook)(image, exports)

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


    def test_completion_preserves_integer_state_and_tail_calls_consumer(self):
        image, exports = fixture()
        result = check_completion_hook(image, exports)
        self.assertEqual(result["cases"], 128)
        self.assertEqual(result["original_dma_start_writes"], 0)
        self.assertEqual(result["resume"], "0x8c0bf37a")

    def test_completion_must_restore_r0(self):
        self.mutate(0x60F6, 0x0009, completion=True)

    def test_completion_must_restore_sr(self):
        self.mutate(0x410E, 0x0009, completion=True)

    def test_completion_must_pass_exact_original_pr(self):
        self.mutate(0x5543, 0x5542, completion=True)

    def test_completion_must_pass_exact_original_sr(self):
        self.mutate(0x7044, 0x7040, completion=True)

    def test_completion_tail_target_is_original_consumer(self):
        image, exports = fixture()
        data = bytearray(image["payload"])
        target = struct.pack("<I", 0x8C0BF37A)
        self.assertEqual(data.count(target), 1)
        struct.pack_into("<I", data, data.index(target), 0x8C0BF37C)
        image["payload"] = data
        with self.assertRaises(ValueError):
            check_completion_hook(image, exports)

    def test_completion_c_symbol_must_be_present(self):
        image, exports = fixture()
        del image["symbols"]["_kui_vmu_maple_observer_complete"]
        with self.assertRaisesRegex(ValueError, "completion symbol"):
            check_completion_hook(image, exports)


if __name__ == "__main__":
    unittest.main()
