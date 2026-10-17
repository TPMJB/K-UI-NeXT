#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic decoder/bounds tests; no retail executable fixture is bundled."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from vmu_toy_completion_probe import (analyze, branch_locations, pc_relative_loads,
                                      pointer_locations, read_at, verify_identity)


class CompletionProbeTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
