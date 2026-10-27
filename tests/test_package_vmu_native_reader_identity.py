#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The packaging exemption is one decoded hook literal, never a word-wide rewrite."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from package_vmu_native_dual_delivery import normalize_owner_stack_delta, owner_stack_literal


class OwnerStackReaderIdentityTests(unittest.TestCase):
    address = 0x8c004040
    offset = 64

    def fixture(self):
        # AND physical mask; lower MOV.L/CMP-HS/BF; upper MOV.L/CMP-HI/BT.
        body = bytearray(struct.pack('<8H', 0x2139, 0xd005, 0x3102, 0x8b00,
                                     0xd004, 0x3106, 0x8900, 0x0009) + bytes(16))
        struct.pack_into('<II', body, 24, 0x0c00c100, 0x0c00f400)
        old = bytes(64) + body + bytes(64)
        new = bytearray(old)
        struct.pack_into('<I', new, self.offset + 24, 0x0c008100)
        return old, bytes(new)

    def normalize(self, old, new):
        return normalize_owner_stack_delta(old, new, self.offset, self.address, 32)

    def test_exact_single_decoded_literal(self):
        old, new = self.fixture()
        normalized, proof = self.normalize(old, new)
        self.assertEqual(normalized, old)
        self.assertEqual((proof['binary_offset'], proof['hook_offset'], proof['changed_literals']), (88, 24, 1))

    def test_every_other_changed_byte_is_refused(self):
        old, new = self.fixture()
        for at in range(len(new)):
            if self.offset + 24 <= at < self.offset + 28:
                continue
            changed = bytearray(new)
            changed[at] ^= 1
            with self.subTest(offset=at), self.assertRaises(ValueError):
                self.normalize(old, bytes(changed))

    def test_same_word_elsewhere_is_not_normalized(self):
        old, new = map(bytearray, self.fixture())
        struct.pack_into('<I', old, 8, 0x0c00c100)
        struct.pack_into('<I', new, 8, 0x0c008100)
        with self.assertRaises(ValueError):
            self.normalize(bytes(old), bytes(new))

    def test_unrelated_matching_word_preserved(self):
        old, new = map(bytearray, self.fixture())
        for image in (old, new):
            struct.pack_into('<I', image, 8, 0x0c00c100)
        self.assertEqual(self.normalize(bytes(old), bytes(new))[0], bytes(old))

    def test_duplicate_hook_literal_refused(self):
        old, new = map(bytearray, self.fixture())
        struct.pack_into('<I', old, self.offset + 16, 0x0c00c100)
        struct.pack_into('<I', new, self.offset + 16, 0x0c008100)
        with self.assertRaises(ValueError):
            self.normalize(bytes(old), bytes(new))

    def test_comparison_role_required_even_when_bodies_match(self):
        old, new = map(bytearray, self.fixture())
        for image in (old, new):
            struct.pack_into('<H', image, self.offset + 4, 0x3106)
        with self.assertRaises(ValueError):
            self.normalize(bytes(old), bytes(new))

    def test_upper_bound_role_and_value_required(self):
        old, new = map(bytearray, self.fixture())
        for image in (old, new):
            struct.pack_into('<I', image, self.offset + 28, 0x0c00f404)
        with self.assertRaises(ValueError):
            self.normalize(bytes(old), bytes(new))

    def test_hook_extent_required(self):
        old, new = self.fixture()
        for offset, length in ((-1, 32), (64, 24), (len(old), 32)):
            with self.subTest(offset=offset, length=length), self.assertRaises(ValueError):
                normalize_owner_stack_delta(old, new, offset, self.address, length)

    def test_lower_constant_without_instruction_relationship_refused(self):
        with self.assertRaises(ValueError):
            owner_stack_literal(bytes(24) + struct.pack('<II', 0x0c00c100, 0x0c00f400),
                                self.address, 0x0c00c100)


if __name__ == '__main__':
    unittest.main()
