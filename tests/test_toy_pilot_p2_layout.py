#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject wrong aliases/LMAs and check optional actual Toy linked images.

The independent fixture contains cached executable bytes and a compact
uncached initialized/BSS segment. Mutations exercise the loader's real ELF
admission rather than matching linker-script text.
"""
import argparse
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from check_loader_layout import EH, PH, SH, SYM, inspect_elf
from toy_pilot_symbols import generate

BASE, LIMIT, P2 = 0x8C004000, 0x8C007800, 0xAC004000


def fixture():
    text = b'\x09\x00' * 8
    initialized = bytes(range(16))
    strings = b'\0_start\0writable\0'
    symbols = (SYM.pack(0, 0, 0, 0, 0, 0) +
               SYM.pack(1, BASE, len(text), 0x12, 0, 1) +
               SYM.pack(8, P2 + 32, len(initialized), 0x11, 0, 2))
    text_offset, data_offset = 0x100, 0x110
    symbols_offset = 0x120
    strings_offset = symbols_offset + len(symbols)
    sections_offset = (strings_offset + len(strings) + 3) & ~3
    data = bytearray(sections_offset + 6 * SH.size)
    ident = b'\x7fELF\x01\x01\x01' + bytes(9)
    EH.pack_into(data, 0, ident, 2, 42, 1, BASE, EH.size, sections_offset,
                 0, EH.size, PH.size, 2, SH.size, 6, 0)
    PH.pack_into(data, EH.size, 1, text_offset, BASE, BASE, 16, 16, 5, 4)
    PH.pack_into(data, EH.size + PH.size, 1, data_offset, P2 + 32,
                 BASE + 32, 16, 32, 6, 32)
    data[text_offset:text_offset + len(text)] = text
    data[data_offset:data_offset + len(initialized)] = initialized
    data[symbols_offset:symbols_offset + len(symbols)] = symbols
    data[strings_offset:strings_offset + len(strings)] = strings
    sections = [
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (0, 1, 6, BASE, text_offset, 16, 0, 0, 2, 0),
        (0, 1, 3, P2 + 32, data_offset, 16, 0, 0, 32, 0),
        (0, 8, 3, P2 + 48, 0, 16, 0, 0, 16, 0),
        (0, 2, 0, 0, symbols_offset, len(symbols), 5, 1, 4, SYM.size),
        (0, 3, 0, 0, strings_offset, len(strings), 0, 0, 1, 0),
    ]
    for index, section in enumerate(sections):
        SH.pack_into(data, sections_offset + index * SH.size, *section)
    return data


def change_program(data, index, **changes):
    at = EH.size + index * PH.size
    fields = list(PH.unpack_from(data, at))
    names = ('kind', 'offset', 'va', 'pa', 'size', 'memory', 'flags', 'alignment')
    for name, value in changes.items():
        fields[names.index(name)] = value
    PH.pack_into(data, at, *fields)


def change_section(data, index, **changes):
    at = EH.unpack_from(data)[6] + index * SH.size
    fields = list(SH.unpack_from(data, at))
    names = ('name', 'kind', 'flags', 'address', 'offset', 'size',
             'link', 'info', 'alignment', 'stride')
    for name, value in changes.items():
        fields[names.index(name)] = value
    SH.pack_into(data, at, *fields)


class PrivateLayoutAdmission(unittest.TestCase):
    def test_compact_physical_payload_and_runtime_symbols(self):
        data = fixture()
        image = inspect_elf(data, BASE, LIMIT, private_p2=True)
        self.assertEqual(image['payload'], b'\x09\x00' * 8 + bytes(16) + bytes(range(16)))
        self.assertEqual(image['memory_end'], BASE + 64)
        self.assertEqual(image['symbols']['writable'], P2 + 32)
        with self.assertRaises(ValueError):
            inspect_elf(data, BASE, LIMIT)

    def test_material_segment_mutations_are_rejected(self):
        mutations = (
            {'pa': P2 + 32},                 # Noncompact uncached LMA.
            {'pa': BASE + 64},               # Different physical mapping.
            {'va': BASE + 32},               # Writable cached runtime.
            {'va': 0xCC004020},              # An unadmitted alias.
            {'flags': 7},                    # Writable executable P2.
            {'flags': 4},                    # P2 segment is not writable.
            {'memory': LIMIT - BASE},        # Physical overrun.
            {'memory': 0},                   # Empty load region.
            {'size': 33},                    # File exceeds memory.
            {'size': 8},                     # Initialized data not fully loaded.
            {'va': P2 + 8, 'pa': BASE + 8},  # Physical code/data overlap.
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                data = fixture()
                change_program(data, 1, **mutation)
                inspect_elf(data, BASE, LIMIT, private_p2=True)

    def test_material_section_mutations_are_rejected(self):
        mutations = (
            (2, {'address': BASE + 32}),       # Writable P1 even under P2 PHDR.
            (3, {'address': BASE + 48}),       # Cached BSS.
            (2, {'flags': 7}),                # Executable mutable section.
            (2, {'flags': 2}),                # Read-only section placed P2.
            (3, {'size': 32}),                # Section exceeds load memory.
            (1, {'address': P2}),             # Executable P2 section.
            (1, {'address': BASE + 16}),       # Outside executable load segment.
            (2, {'offset': 0x100}),            # Different bytes than loaded data.
        )
        for index, mutation in mutations:
            with self.subTest(section=index, mutation=mutation), self.assertRaises(ValueError):
                data = fixture()
                change_section(data, index, **mutation)
                inspect_elf(data, BASE, LIMIT, private_p2=True)

    def test_worker_style_entry_after_fixed_export_prefix(self):
        data = fixture()
        struct.pack_into('<I', data, 24, BASE + 2)
        # Keep the named function and ELF entry equal, independently of base.
        struct.pack_into('<I', data, 0x120 + SYM.size + 4, BASE + 2)
        image = inspect_elf(data, BASE, LIMIT, private_p2=True, entry_at_base=False)
        self.assertEqual(image['symbols']['_start'], BASE + 2)
        with self.assertRaises(ValueError):
            inspect_elf(data, BASE, LIMIT, private_p2=True)
        with self.assertRaises(ValueError):
            inspect_elf(data, BASE, LIMIT, private_p2=True,
                        entry_symbol='_not_the_entry', entry_at_base=False)


def linked_check(resident_path, worker_path):
    result = {}
    if resident_path:
        image = inspect_elf(resident_path.read_bytes(), BASE, LIMIT, private_p2=True)
        symbols = image['symbols']
        expected = {'__retail_hook_stack_bottom': 0xAC007800,
                    '__retail_hook_stack': 0xAC007D00,
                    '__retail_hook_stack_bottom_physical': 0x8C007800,
                    '__retail_hook_stack_physical': 0x8C007D00}
        if any(symbols.get(name) != value for name, value in expected.items()):
            raise ValueError('Actual Toy low stack aliases differ from the fixed reservation')
        bss_begin = symbols['__retail_resident_bss_begin']
        bss_end = symbols['__retail_resident_bss_end']
        saved_r9 = symbols.get('_kui_toy_pilot_heap_saved_r9', 0)
        if not (P2 <= bss_begin <= saved_r9 and saved_r9 % 4 == 0 and
                saved_r9 + 8 <= bss_end <= 0xAC007800 and
                bss_end - 0x20000000 == image['memory_end']):
            raise ValueError('Actual Toy heap r9/r10 saves must fit its uncached low BSS')
        header = generate(resident_path, private_p2=True)
        if 'LOW_CONTROL UINT32_C(0xac' not in header:
            raise ValueError('Actual low bootstrap control is not exported through P2')
        result['resident'] = {'payload_bytes': len(image['payload']),
                              'physical_end': hex(image['memory_end']),
                              'bss_stack_margin_bytes': LIMIT - image['memory_end'],
                              'heap_saved_r9': hex(saved_r9)}
    if worker_path:
        image = inspect_elf(worker_path.read_bytes(), 0x8CFD0000, 0x8CFE0000,
                            private_p2=True, entry_symbol='_kui_toy_pilot_initialize',
                            entry_at_base=False)
        symbols = image['symbols']
        bottom, top = (symbols[name] for name in
                       ('__toy_pilot_stack_bottom', '__toy_pilot_stack_top'))
        if not (0xACFD0000 <= bottom < top <= 0xACFE0000 and
                top - bottom == 8192 and bottom % 32 == top % 32 == 0 and
                symbols['__toy_pilot_worker_end'] == top - 0x20000000 == image['memory_end']):
            raise ValueError('Actual Toy worker stack/runtime/physical bound differs')
        result['worker'] = {'payload_bytes': len(image['payload']),
                            'physical_end': hex(image['memory_end']),
                            'stack_bottom': hex(bottom), 'stack_top': hex(top)}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resident-elf', type=Path)
    parser.add_argument('--worker-elf', type=Path)
    args = parser.parse_args()
    result = unittest.TextTestRunner(verbosity=1).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(PrivateLayoutAdmission))
    if not result.wasSuccessful():
        raise SystemExit(1)
    linked = linked_check(args.resident_elf, args.worker_elf)
    print('Toy P2 layout: 19 segment/section mutations rejected; compact physical '
          'payload, runtime aliases and entry separation pass ' + json.dumps(linked, sort_keys=True))
