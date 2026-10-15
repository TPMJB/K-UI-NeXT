# SPDX-License-Identifier: GPL-3.0-only
"""Linked SH call corruption fixtures, without a compiler/toolchain dependency."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import check_retail_calls as tool

BASE = 0x8c008300


def branch(kind, source, destination):
    displacement = (destination - source - 4) // 2
    assert -2048 <= displacement < 2048
    return (0xb000 if kind == 'BSR' else 0xa000) | (displacement & 0xfff)


def fixture(*, call_skip=0, tail_skip=0, late_skip=0, local_label=False,
            recursive_body_call=False):
    """A linked image with a caller, a callee with saves, and a later caller.

    A PC-loaded literal spells MOV.W followed by BSR. Treating its MOV.W as
    code would incorrectly hide the later caller's BSR at +108. The caller's
    deliberately stale size also overlaps both following function entries,
    as sizes can after SH relaxation; actual entry boundaries must win.
    """
    text = bytearray(struct.pack('<H', 0x0009) * 64)

    def word(offset, opcode):
        struct.pack_into('<H', text, offset, opcode)

    word(0, 0x4f22)  # sts.l pr,@-r15
    word(2, branch('BSR', 2, 32 + call_skip))
    word(6, 0x4f26)  # lds.l @r15+,pr
    word(8, branch('BRA', 8, 32 + tail_skip))
    word(12, 0xd002)  # mov.l @(2,PC),r0 -> literal at +24
    word(14, 0x000b)
    struct.pack_into('<I', text, 24, 0xb0049128)
    for offset, opcode in zip(range(32, 40, 2), (0x2f86, 0x2f96, 0x2fa6, 0x6953)):
        word(offset, opcode)
    word(40, branch('BSR' if recursive_body_call else 'BRA', 40, 40))
    word(44, 0x000b)
    word(108, branch('BSR', 108, 32 + late_skip))
    word(112, 0x000b)
    symbols = [('_start', BASE, 128, 0x12, 1),
               ('_callee', BASE + 32, 76, 0x02, 1),
               ('_later_caller', BASE + 108, 20, 0x02, 1)]
    if local_label:
        symbols.append(('.Llooks_like_entry', BASE + 40, 0, 0x00, 1))
    return elf(text, symbols)


def elf(text, symbols):
    """Minimal linked SH ELF: executable text, symbol table and string tables."""
    strings = bytearray(b'\0')
    rows = [bytes(tool.SYM.size)]
    for name, address, size, info, section in symbols:
        offset = len(strings)
        strings.extend(name.encode('ascii') + b'\0')
        rows.append(tool.SYM.pack(offset, address, size, info, 0, section))
    names = b'\0.text\0.symtab\0.strtab\0.shstrtab\0'
    contents = [bytes(text), b''.join(rows), bytes(strings), names]
    payload = bytearray(tool.EH.size)
    sections = [bytes(tool.SH.size)]
    for index, content in enumerate(contents, 1):
        payload.extend(bytes(-len(payload) % 4))
        offset = len(payload)
        payload.extend(content)
        section_name = (1, 7, 15, 23)[index - 1]
        sections.append(tool.SH.pack(section_name, 2 if index == 2 else 3 if index > 2 else 1,
                                     6 if index == 1 else 0, BASE if index == 1 else 0,
                                     offset, len(content), 3 if index == 2 else 0,
                                     1 if index == 2 else 0, 4, tool.SYM.size if index == 2 else 0))
    payload.extend(bytes(-len(payload) % 4))
    section_offset = len(payload)
    payload.extend(b''.join(sections))
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    payload[:tool.EH.size] = tool.EH.pack(ident, 2, 42, 1, BASE, 0, section_offset, 0,
                                        tool.EH.size, 32, 0, tool.SH.size, len(sections), 4)
    return bytes(payload)


class LinkedRetailCalls(unittest.TestCase):
    def audit(self, **kwargs):
        return tool.audit_elf(fixture(**kwargs), 'fixture')

    def test_real_entries_loops_and_literal_pools_pass(self):
        result = self.audit()
        self.assertEqual(result['violations'], [])
        self.assertEqual(result['direct_edges'], 4)
        self.assertEqual(result['literal_halfwords'], 2)

    def test_bsr_skipping_saves_is_rejected(self):
        result = self.audit(call_skip=8)
        self.assertEqual(len(result['violations']), 1)
        self.assertIn('BSR at 0x8c008302', result['violations'][0])
        self.assertIn('0x8c008328', result['violations'][0])

    def test_tail_bra_skipping_saves_is_rejected_despite_stale_function_size(self):
        result = self.audit(tail_skip=8)
        self.assertEqual(len(result['violations']), 1)
        self.assertIn('BRA at 0x8c008308', result['violations'][0])

    def test_debug_label_cannot_legitimize_a_body_call(self):
        self.assertEqual(len(self.audit(call_skip=8, local_label=True)['violations']), 1)

    def test_literal_mov_bits_cannot_hide_later_corrupt_call(self):
        result = self.audit(late_skip=8)
        self.assertEqual(len(result['violations']), 1)
        self.assertIn('BSR at 0x8c00836c', result['violations'][0])

    def test_intrafunction_bsr_still_requires_an_entry(self):
        result = self.audit(recursive_body_call=True)
        self.assertEqual(len(result['violations']), 1)
        self.assertIn('BSR at 0x8c008328', result['violations'][0])

    def test_all_corrupt_edges_are_reported(self):
        self.assertEqual(len(self.audit(call_skip=8, tail_skip=8, late_skip=8)['violations']), 3)

    def test_reviewed_libgcc_local_division_entries_pass(self):
        result = tool.audit_elf(elf(tool.UDIV_BODY,
                               [('_start', BASE, 0, 0x10, 1),
                                ('___udivsi3', BASE + tool.UDIV_PREFIX, 74, 0x12, 1)]))
        self.assertEqual(result['violations'], [])
        self.assertEqual(sum(edge['pinned_libgcc_helper'] for edge in result['edges']), 8)

    def test_unpinned_libgcc_helper_body_is_rejected(self):
        data = bytearray(tool.UDIV_BODY)
        data[0] ^= 1
        with self.assertRaisesRegex(ValueError, 'Unreviewed SH integer division'):
            tool.audit_elf(elf(data, [('_start', BASE, 0, 0x10, 1),
                                     ('___udivsi3', BASE + tool.UDIV_PREFIX, 74, 0x12, 1)]))


if __name__ == '__main__':
    unittest.main()
