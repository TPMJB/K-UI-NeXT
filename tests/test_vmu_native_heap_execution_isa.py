#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""New scalar ISA cases for the bounded shared SH interpreter.

Source: Renesas SH-4 Software Manual, REJ09B0318-0600, revision 6.00,
sections 2.4/2.5 and tables 7.3/7.6 (pages 150, 151, 155):
https://www.renesas.com/en/document/mas/sh-4-software-manual

The model refuses unsupported address areas and out-of-RAM operands; it does
not emulate the processor's exception vector, MMU, or cache behavior.
"""
import unittest

from test_vmu_native_heap_execution import MASK, RAM_BEGIN, RAM_END, SH


class ObservedSH(SH):
    def __init__(self):
        super().__init__(b"")
        self.reads = []

    def read(self, address, count=4):
        self.reads.append((address, count))
        return super().read(address, count)


class IndexedWordISA(unittest.TestCase):
    def test_every_register_encoding_alias_and_sign_boundary(self):
        cpu = ObservedSH()
        patterns = (0, 1, 0x7fff, 0x8000, 0x80ff, 0xff00, 0xff80, 0xffff)
        cases = 0
        for alias in (0, 0x80000000, 0xa0000000):
            address = (RAM_BEGIN + 0x100120) | alias
            for m in range(16):
                for n in range(16):
                    for word in patterns:
                        with self.subTest(alias=hex(alias), m=m, n=n, word=hex(word)):
                            cpu.r = [(0x13579bdf + i * 0x1020304) & MASK for i in range(16)]
                            if m == 0:
                                cpu.r[0] = address // 2
                            else:
                                cpu.r[0] = 0x122
                                cpu.r[m] = (address - cpu.r[0]) & MASK
                            data = word.to_bytes(2, "little")
                            cpu.store_block(address, data)
                            expected = list(cpu.r)
                            expected[n] = int.from_bytes(data, "little", signed=True) & MASK
                            cpu.pc, cpu.pr, cpu.sr = 0x8c010030, 0x8c010040, 0x600000f1
                            cpu.instructions = 41
                            cpu.reads.clear()
                            cpu.plain((n << 8) | (m << 4) | 0x000d, 0x8c010020)
                            self.assertEqual(cpu.r, expected)
                            self.assertEqual(cpu.reads, [(address, 2)])
                            self.assertEqual((cpu.pc, cpu.pr, cpu.sr),
                                             (0x8c010030, 0x8c010040, 0x600000f1))
                            self.assertEqual((cpu.instructions, cpu.executing), (42, 0x8c010020))
                            self.assertEqual(cpu.writes, [])
                            cases += 1
        self.assertEqual(cases, 6144)

    def test_all_word_values_in_actual_r0_destination_form(self):
        cpu = ObservedSH()
        address = 0x8c100120
        for word in range(0x10000):
            data = word.to_bytes(2, "little")
            cpu.store_block(address, data)
            cpu.r[0], cpu.r[1], cpu.sr = 2, address - 2, 0x600000f0 | (word & 1)
            before_sr = cpu.sr
            cpu.reads.clear()
            cpu.plain(0x001d, 0x8c010020)
            self.assertEqual(cpu.r[0], int.from_bytes(data, "little", signed=True) & MASK)
            self.assertEqual(cpu.r[1], address - 2)
            self.assertEqual(cpu.sr, before_sr)
            self.assertEqual(cpu.reads, [(address, 2)])

    def test_address_addition_wraps_before_ram_validation(self):
        cpu = ObservedSH()
        for index, base, address in ((0xfffffff0, 0x8c100130, 0x8c100120),
                                     (0xfffffffe, 0xac100124, 0xac100122),
                                     (0x80000000, 0x8c100120, 0x0c100120)):
            with self.subTest(index=hex(index), base=hex(base)):
                cpu.store_block(address, b"\x01\x80")
                cpu.r[0], cpu.r[1] = index, base
                cpu.reads.clear()
                cpu.plain(0x021d, 0x8c010020)
                self.assertEqual(cpu.r[2], 0xffff8001)
                self.assertEqual((cpu.r[0], cpu.r[1]), (index, base))
                self.assertEqual(cpu.reads, [(address, 2)])

    def test_exact_first_and_last_word_in_each_ram_alias(self):
        cpu = ObservedSH()
        for alias in (0, 0x80000000, 0xa0000000):
            for physical in (RAM_BEGIN, RAM_END - 2):
                address = physical | alias
                with self.subTest(address=hex(address)):
                    cpu.store_block(address, b"\xff\xff")
                    cpu.r[0], cpu.r[1] = 0, address
                    cpu.plain(0x021d, 0x8c010020)
                    self.assertEqual(cpu.r[2], MASK)

    def test_odd_word_addresses_refuse_before_any_data_read(self):
        cpu = ObservedSH()
        for alias in (0, 0x80000000, 0xa0000000):
            for physical in (RAM_BEGIN + 1, RAM_END - 1):
                address = physical | alias
                with self.subTest(address=hex(address)):
                    cpu.r[0], cpu.r[1] = 0, address
                    before = list(cpu.r)
                    before_sr = cpu.sr
                    cpu.reads.clear()
                    with self.assertRaisesRegex(AssertionError, "Unaligned indexed word load"):
                        cpu.plain(0x001d, 0x8c010020)
                    self.assertEqual(cpu.r, before)
                    self.assertEqual(cpu.sr, before_sr)
                    self.assertEqual(cpu.reads, [])
                    self.assertEqual(cpu.writes, [])

    def test_invalid_ram_spans_and_address_areas_still_refuse(self):
        cpu = ObservedSH()
        addresses = (RAM_BEGIN - 2, RAM_END, 0x8bfffffe, 0x8d000000,
                     0xabfffffe, 0xad000000, 0x4c100120, 0xcc100120, 0xec100120)
        for address in addresses:
            with self.subTest(address=hex(address)):
                cpu.r[0], cpu.r[1] = 0, address
                before = list(cpu.r)
                with self.assertRaises(AssertionError):
                    cpu.plain(0x001d, 0x8c010020)
                self.assertEqual(cpu.r, before)
                self.assertEqual(cpu.writes, [])
        cpu.r[0], cpu.r[1] = MASK, 1
        before = list(cpu.r)
        with self.assertRaises(AssertionError):
            cpu.plain(0x001d, 0x8c010020)
        self.assertEqual(cpu.r, before)

    def test_unimplemented_neighboring_instructions_still_refuse(self):
        cpu = ObservedSH()
        for op in (0x001c, 0x001e, 0x001f, 0xffff):
            with self.subTest(op=hex(op)):
                before = list(cpu.r)
                with self.assertRaisesRegex(AssertionError, "Unsupported heap-model instruction"):
                    cpu.plain(op, 0x8c010020)
                self.assertEqual(cpu.r, before)


class ScalarRegisterISA(unittest.TestCase):
    @staticmethod
    def logical_shift_bits(value, shift):
        bits = format(value, "032b")
        if shift & 0x80000000:
            count = 32 - (shift & 31)
            return int("0" * count + bits[:32 - count], 2)
        count = shift & 31
        return int(bits[count:] + "0" * count, 2)

    def test_shld_register_aliases_all_low_counts_and_t_preservation(self):
        cpu = ObservedSH()
        shifts = (*range(32), *(0x80000000 | x for x in range(32)),
                  32, 0x7fffffff, 0xffffffff, 0xffffffdf)
        cases = 0
        for m in range(16):
            for n in range(16):
                for shift in shifts:
                    for t in (0, 1):
                        cpu.r = [(0x13579bdf + i * 0x1020304) & MASK for i in range(16)]
                        cpu.r[n] = 0x80000001
                        cpu.r[m] = shift
                        before = list(cpu.r)
                        expected = list(before)
                        expected[n] = self.logical_shift_bits(before[n], before[m])
                        cpu.sr = 0x600000f0 | t
                        cpu.instructions = 41
                        cpu.reads.clear()
                        cpu.plain(0x400d | (n << 8) | (m << 4), 0x8c010020)
                        self.assertEqual(cpu.r, expected)
                        self.assertEqual(cpu.sr, 0x600000f0 | t)
                        self.assertEqual((cpu.instructions, cpu.executing), (42, 0x8c010020))
                        self.assertEqual(cpu.reads, [])
                        self.assertEqual(cpu.writes, [])
                        cases += 1
        self.assertEqual(cases, 34816)

    def test_shld_value_boundaries_agree_with_existing_release_decoder(self):
        from profile_vmu_native_release_sh import ReleaseSH
        cpu, existing = ObservedSH(), ObservedSH()
        for value in (0, 1, 0x7fffffff, 0x80000000, 0x80000001, 0x12345678, MASK):
            for shift in (0, 1, 31, 32, MASK, 0xffffffe1, 0xffffffe0, 0xffffffdf):
                for c in (cpu, existing):
                    c.r[12], c.r[13], c.sr = shift, value, 0x600000f1
                cpu.plain(0x4dcd, 0x8c010020)
                ReleaseSH.plain(existing, 0x4dcd, 0x8c010020)
                self.assertEqual(cpu.r[13], self.logical_shift_bits(value, shift))
                self.assertEqual(cpu.r, existing.r)
                self.assertEqual(cpu.sr, existing.sr)

    def test_swap_lower_bytes_all_register_aliases_and_t_preservation(self):
        cpu = ObservedSH()
        cases = 0
        values = (0, 1, 0x12345678, 0x89abcdef, 0x80000000, 0x000080ff, MASK, 0x0000ff00)
        for m in range(16):
            for n in range(16):
                for value in values:
                    for t in (0, 1):
                        cpu.r = [(0x13579bdf + i * 0x1020304) & MASK for i in range(16)]
                        cpu.r[m] = value
                        before = list(cpu.r)
                        expected = list(before)
                        data = value.to_bytes(4, "little")
                        expected[n] = int.from_bytes(data[1::-1] + data[2:], "little")
                        cpu.sr = 0x600000f0 | t
                        cpu.reads.clear()
                        cpu.plain(0x6008 | (n << 8) | (m << 4), 0x8c010020)
                        self.assertEqual(cpu.r, expected)
                        self.assertEqual(cpu.sr, 0x600000f0 | t)
                        self.assertEqual(cpu.reads, [])
                        self.assertEqual(cpu.writes, [])
                        cases += 1
        self.assertEqual(cases, 4096)

    def test_swap_agrees_with_existing_sdk_poll_decoder(self):
        from test_vmu_native_sdk_poll_execution import PollSH
        cpu, existing = ObservedSH(), ObservedSH()
        for source in (0, 0x12345678, 0x89abcdef, 0x800000ff, MASK):
            for c in (cpu, existing):
                c.r[12], c.sr = source, 0x600000f1
            cpu.plain(0x6dc8, 0x8c010020)
            PollSH.plain(existing, 0x6dc8, 0x8c010020)
            self.assertEqual(cpu.r, existing.r)
            self.assertEqual(cpu.sr, existing.sr)

    def test_other_shift_and_swap_forms_remain_unsupported(self):
        cpu = ObservedSH()
        for op in (0x4dce, 0x6dc9, 0x6dc7):
            with self.subTest(op=hex(op)):
                before = list(cpu.r)
                with self.assertRaisesRegex(AssertionError, "Unsupported heap-model instruction"):
                    cpu.plain(op, 0x8c010020)
                self.assertEqual(cpu.r, before)


if __name__ == "__main__":
    unittest.main()
