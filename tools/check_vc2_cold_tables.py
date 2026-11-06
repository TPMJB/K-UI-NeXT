#!/usr/bin/env python3
"""Audit the linked, one-way lifetime of the VC2 cold callback tables.

The C storage test checks by-value adoption. This checks the actual SH image:
the original SCI table and every retained absolute reference to cold data must
fit wholly within the receive-buffer span that retires after initialization.
"""
import argparse
from pathlib import Path
import struct


def audit(path):
    image = path.read_bytes()
    assert image[:7] == b'\x7fELF\x01\x01\x01', 'Expected little-endian ELF32'
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', image)
    assert header[2] == 42, 'Expected SH machine'
    section_offset, section_size, section_count, names_index = (
        header[6], header[11], header[12], header[13])
    sections = [struct.unpack_from('<IIIIIIIIII', image,
                section_offset + index * section_size)
                for index in range(section_count)]

    def section_bytes(section):
        return image[section[4]:section[4] + section[5]]

    names = section_bytes(sections[names_index])

    def name_at(table, offset):
        return table[offset:table.index(b'\0', offset)].decode()

    named_sections = {name_at(names, section[0]): section
                      for section in sections}
    symbols = {}
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strings = section_bytes(sections[section[6]])
        for offset in range(section[4], section[4] + section[5], section[9]):
            entry = struct.unpack_from('<IIIBBH', image, offset)
            symbols[name_at(strings, entry[0])] = entry

    def address(name):
        return symbols[name][1]

    region = address('_standalone_reader_region')
    cold_begin = address('__native_dma_cold_begin')
    cold_end = address('__native_dma_cold_end')
    data_begin = address('__native_dma_cold_data_begin')
    data_end = address('__native_dma_cold_data_end')
    cold = named_sections['.native_dma_cold_init']
    assert region == cold_begin == cold[3]
    assert cold_begin < data_begin < data_end == cold_end
    assert cold_end <= region + 0x440, 'Cold data overlaps live engine state'
    bus = symbols['_bus']
    assert bus[2] == 28 and (bus[3] & 15) == 1, 'SCI bus is not seven words'
    assert data_begin <= bus[1] and bus[1] + bus[2] <= data_end
    assert sections[bus[5]] == cold, 'SCI bus did not enter cold storage'
    bus_offset = cold[4] + bus[1] - cold[3]
    words = struct.unpack_from('<7I', image, bus_offset)
    assert words[:3] == (0, 0, 0) and words[5:] == (0, 0)
    assert words[3] == address('_select_card')
    assert words[4] == address('_transfer')
    gd_tables = [symbol for name, symbol in symbols.items()
                 if name.startswith('_ops.') and symbol[2] == 16
                 and data_begin <= symbol[1] < data_end]
    assert len(gd_tables) == 1, 'Expected one cold GD callback table'
    gd = gd_tables[0]
    assert gd[1] + gd[2] <= data_end and sections[gd[5]] == cold
    gd_offset = cold[4] + gd[1] - cold[3]
    assert struct.unpack_from('<4I', image, gd_offset) == (
        0, address('_map_guest'), address('_check_sectors'), 0)
    assert bus[2] + gd[2] == data_end - data_begin, (
        'Unexpected data in the retiring callback-table span')

    references = []
    for section in sections:
        if not section[2] & 2 or section[1] == 8:  # alloc, not NOBITS
            continue
        payload = section_bytes(section)
        for offset in range(0, len(payload) - 3, 4):
            word = struct.unpack_from('<I', payload, offset)[0]
            physical = word & 0x1fffffff
            if data_begin & 0x1fffffff <= physical < data_end & 0x1fffffff:
                source = section[3] + offset
                assert cold_begin <= source < data_begin, (
                    f'Live pointer to retired cold data at {source:08x}')
                references.append(source)
    assert len(references) >= 2, 'Both cold table adoption references are required'
    stack_bottom = address('__retail_hook_stack_bottom')
    bss_end = address('__retail_resident_bss_end')
    assert bss_end <= stack_bottom
    assert address('__retail_hook_stack') - stack_bottom == 352
    return dict(bytes=data_end-data_begin, cold_only_references=len(references),
                cold_bytes=cold_end-cold_begin,
                stack_clearance=stack_bottom-bss_end, stack_bytes=352)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    print(audit(parser.parse_args().elf))
