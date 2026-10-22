#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic decoder/bounds tests; no retail executable fixture is bundled."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from vmu_toy_completion_probe import (analyze, branch_locations, pc_relative_loads,
                                      pointer_locations, read_at, verify_identity,
                                      BASE, OWNER_BYTES, RX_WITNESSES, WITNESSES)


class CompletionProbeTests(unittest.TestCase):
    @staticmethod
    def scalar_fixture():
        """Independent zero-filled input containing sparse public metadata only."""
        data = bytearray(OWNER_BYTES)
        for address, size, value, _ in WITNESSES + RX_WITNESSES:
            data[address - BASE:address - BASE + size] = value.to_bytes(size, "little")
        return data

    def test_pc_longword_alignment_and_register(self):
        data = struct.pack("<4H", 0xD301, 0xD701, 0x0009, 0x0009) + bytes(8)
        self.assertEqual(pc_relative_loads(data, 0x1008, 0x1000), [0x1000, 0x1002])
        self.assertEqual(pc_relative_loads(data, 0x100C, 0x1000), [])

    def test_pointer_scan_is_word_aligned_and_bounded(self):
        data = struct.pack("<II", 0x12345678, 0x87654321) + b"\x78\x56\x34"
        self.assertEqual(pointer_locations(data, 0x12345678, 0x2000), [0x2000])
        self.assertEqual(pointer_locations(data, 0xDEADBEEF, 0x2000), [])

    def test_direct_and_conditional_signed_branches(self):
        data = struct.pack("<5H", 0xA000, 0xBFFF, 0x89FE, 0x8FFD, 0x0009)
        self.assertEqual(branch_locations(data, 0x3004, 0x3000), [0x3000, 0x3002, 0x3004, 0x3006])

    def test_read_bounds(self):
        self.assertEqual(read_at(b"\x12\x34\x56\x78", 0x4000, 4, 0x4000), 0x78563412)
        for address, size in ((0x3FFE, 2), (0x4002, 4), (0x4000, 3)):
            with self.subTest(address=address, size=size), self.assertRaises(ValueError):
                read_at(bytes(4), address, size, 0x4000)

    def test_identity_requires_both_whole_size_and_hash(self):
        sample = b"independent synthetic input"
        digest = hashlib.sha256(sample).hexdigest()
        verify_identity(sample, len(sample), digest)
        for data, count, sha in ((sample + b"x", len(sample), digest),
                                 (sample, len(sample), "0" * 64)):
            with self.subTest(count=count), self.assertRaises(ValueError):
                verify_identity(data, count, sha)

    def test_unrecognized_input_cannot_emit_owner_claims(self):
        with self.assertRaises(ValueError):
            analyze(bytes(748444))

    def test_initializer_route_decodes_from_sparse_fixture(self):
        # Only synthetic fixture tests bypass identity. The production analyzer
        # still refuses any input not matching the exact entire owner executable.
        with patch("vmu_toy_completion_probe.verify_identity"):
            result = analyze(self.scalar_fixture())
        route = result["receive_ownership"]["uniform_initializer"]
        self.assertEqual(route["game_target_pointer_locations"], ["0x8c04e9c8"])
        self.assertEqual(route["game_target_load_references"], ["0x8c04e930"])
        self.assertEqual(route["custom_wrapper_pointer_locations"], [])
        self.assertEqual(route["custom_wrapper_direct_branches"], [])
        self.assertFalse(result["retail_admitted"])
        self.assertFalse(result["patches_generated"])

    def test_rx_scalar_mismatch_refuses_even_when_completion_scalars_match(self):
        data = self.scalar_fixture()
        # A wrong allocation stride must not silently retain a capacity claim.
        data[0x8C0BF7B4 - BASE] ^= 1
        with patch("vmu_toy_completion_probe.verify_identity"), self.assertRaises(ValueError):
            analyze(data)

    def test_uniform_capacity_is_disjoint_and_fits_longest_card_reply(self):
        with patch("vmu_toy_completion_probe.verify_identity"):
            contract = analyze(self.scalar_fixture())["receive_ownership"]["uniform_initializer"]
        slots = [int(slot, 16) for bank in contract["rx_slots"] for slot in bank]
        self.assertEqual(len(slots), 48)
        self.assertEqual(len(set(slots)), 48)
        self.assertEqual(contract["a1_slots"], ["0xac0e0480", "0xac0e6480"])
        self.assertEqual(contract["rx_slot_bytes"], 1024)
        self.assertEqual(contract["rx_to_tx_gap_bytes"], 32)
        self.assertEqual(contract["rx_arena_end_exclusive"], "0xac0ec080")
        for left, right in zip(slots, slots[1:]):
            self.assertEqual(right - left, 1024)
        for reply_bytes in (116, 524):
            self.assertLessEqual(reply_bytes, contract["rx_slot_bytes"])
        self.assertLessEqual(slots[-1] + 1024, int(contract["tx_banks"][0], 16))


if __name__ == "__main__":
    unittest.main()
