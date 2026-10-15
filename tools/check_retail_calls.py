#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check actual linked SH BSR/BRA destinations in the six retail images.

GCC's call graph does not describe the displacement produced by SH linker
relaxation. A correct source-level call can therefore pass the stack audit
while its linked BSR skips a callee's saves. Read executable ELF bytes, never
objdump's nearest-symbol annotation. Function sizes are not bounds: SH
relaxation can leave a size extending into the following function.
"""
import argparse
from bisect import bisect_right
from dataclasses import dataclass
import json
from pathlib import Path
import struct

from check_loader_layout import EH, SH, SYM, region
from check_retail_instructions import IMAGES


@dataclass(frozen=True)
class Section:
    index: int
    name: str
    address: int
    flags: int
    data: bytes


@dataclass(frozen=True)
class Symbol:
    name: str
    address: int
    size: int
    kind: int
    binding: int
    section: int


# Source-defined assembly entry groups, not arbitrary NOTYPE labels. These
# wrappers share private labels/epilogues in retail_resident.S/retail_stage.S.
# None contains BSR; calls into them must name an exact entry below.
ASSEMBLY_GROUPS = (
    ('_start',),
    ('_kui_retail_resident_hook', '_kui_retail_gd_c0_hook',
     '_kui_retail_gd_1000_hook', '_kui_retail_gd_10f0_hook',
     '_kui_retail_menu_hook', '_kui_retail_skies_read_failure'),
    ('_kui_retail_ce_isr',),
    ('_kui_retail_vector_forward', '_kui_retail_vector_interrupt'),
    ('_kui_retail_irq_entry', '_kui_retail_release_100',
     '_kui_retail_release_400', '_kui_retail_release_600', '_kui_retail_rehook'),
    ('_kui_retail_bootstrap_enter',),
    ('_kui_retail_stage_sync',),
    ('_kui_retail_game_resume',),
    ('__retail_trampoline_start',),
    ('__retail_ce_vbr',),
    ('_kui_retail_sonic_g2_fifo', '_kui_retail_sonic_g2_busy',
     '_kui_retail_sonic_scope_entry', '_kui_retail_sonic_asset_guard',
     '_kui_retail_sonic_texture_entry', '_kui_retail_sonic_compressed_guard',
     '_kui_retail_sonic_decode_guard'),
    ('_kui_retail_sonic_scope_return',),
    ('_kui_retail_sonic_texture_return',),
    ('_kui_retail_startup_trace_scan', '_kui_retail_startup_trace_g2',
     '_kui_retail_startup_trace_pvr', '_kui_retail_startup_trace_gd',
     '_kui_retail_startup_trace_mount'),
    ('_kui_retail_startup_gd_proxy',),
    ('_kui_retail_startup_gd_return',),
)

# libgcc's integer SH division deliberately BSRs into unexported DIV1
# sequences before ___udivsi3, including the second instruction to perform
# seven steps. Pin the complete helper prefix AND routine from the booting
# b579 reference, with exact relative edges. No other caller gets a waiver.
UDIV_PREFIX = 34
UDIV_BODY = bytes.fromhex(
    '54345434543454345434543454340b0054345434244054342440543424400b005434'
    '224f5d605030128f190049602944e6bf2845e5bf54344d200d24e0bf4964dfbf5434'
    '264f4d20096024400b00294500e04d200d24dcbf2440dabf2440d8bf2440d6bf2440'
    '264f0b002440')
UDIV_EDGES = {14: -34, 18: -32, 26: -34, 30: -32,
              52: -16, 56: -16, 60: -16, 64: -16}


def read_elf(data):
    """Read allocated executable sections and real symbol metadata."""
    if len(data) < EH.size:
        raise ValueError('Truncated ELF header')
    h = EH.unpack_from(data)
    if (h[0][:7] != b'\x7fELF\x01\x01\x01' or h[1] != 2 or h[2] != 42 or
            h[3] != 1 or h[8] != EH.size or h[11] != SH.size or
            not 0 < h[12] <= 4096 or h[13] >= h[12]):
        raise ValueError('Requires a linked little-endian SH ELF with sections')
    rows = [SH.unpack(region(data, h[6] + i * SH.size, SH.size, 'section'))
            for i in range(h[12])]
    strings = region(data, rows[h[13]][4], rows[h[13]][5], 'section names')

    def string(table, offset):
        end = table.find(b'\0', offset)
        if offset >= len(table) or end < 0:
            raise ValueError('Invalid ELF string')
        return table[offset:end].decode('ascii')

    sections = {}
    for index, row in enumerate(rows):
        if row[2] & 4:
            if row[1] != 1 or row[2] & 2 == 0 or row[3] & 1 or row[5] & 1:
                raise ValueError('Executable section is not aligned allocated PROGBITS')
            sections[index] = Section(index, string(strings, row[0]), row[3], row[2],
                                      region(data, row[4], row[5], 'executable bytes'))
    if not sections:
        raise ValueError('No executable ELF sections')
    ordered = sorted(sections.values(), key=lambda s: s.address)
    if any(left.address + len(left.data) > right.address
           for left, right in zip(ordered, ordered[1:])):
        raise ValueError('Overlapping executable ELF sections')
    symbols = []
    for row in rows:
        if row[1] != 2:
            continue
        if row[9] != SYM.size or row[5] % SYM.size or row[6] >= len(rows):
            raise ValueError('Invalid ELF symbol table')
        names = region(data, rows[row[6]][4], rows[row[6]][5], 'symbol names')
        for at in range(row[4], row[4] + row[5], SYM.size):
            n, value, size, info, _, section = SYM.unpack(region(data, at, SYM.size, 'symbol'))
            name = string(names, n)
            if name and section in sections:
                executable = sections[section]
                if (value < executable.address or
                        value > executable.address + len(executable.data)):
                    raise ValueError('Executable symbol is outside its section')
                if info & 15 == 2 and (value & 1 or
                        value == executable.address + len(executable.data)):
                    raise ValueError('Invalid exact function entry')
                symbols.append(Symbol(name, value, size, info & 15, info >> 4, section))
    if not any(s.name == '_start' for s in symbols):
        raise ValueError('Missing linked _start symbol')
    return sections, symbols


def audit_elf(data, name='resident-scia'):
    sections, symbols = read_elf(data)
    by_name = {}
    for symbol in symbols:
        by_name.setdefault(symbol.name, []).append(symbol)
    entries = {}
    owners = {}
    for symbol in symbols:
        if symbol.kind == 2:
            entries.setdefault(symbol.address, set()).add(symbol.name)
            owners[(symbol.section, symbol.address)] = ('function', symbol.address)
    for group in ASSEMBLY_GROUPS:
        found = [s for label in group for s in by_name.get(label, ()) if s.kind == 0]
        if not found:
            continue
        if len({s.section for s in found}) != 1:
            raise ValueError('Assembly entry group crosses sections')
        owner = ('assembly', min(s.address for s in found))
        for symbol in found:
            entries.setdefault(symbol.address, set()).add(symbol.name)
            owners[(symbol.section, symbol.address)] = owner
    # The exact helper prefix is an explicit boundary, never the trailing
    # bytes of the previous C function. Invalid library bodies get no waiver.
    udiv = by_name.get('___udivsi3', ())
    udiv_entry = None
    if udiv:
        if len(udiv) != 1:
            raise ValueError('Ambiguous ___udivsi3')
        symbol = udiv[0]
        section = sections[symbol.section]
        begin = symbol.address - UDIV_PREFIX - section.address
        if (begin < 0 or section.data[begin:begin + len(UDIV_BODY)] != UDIV_BODY):
            raise ValueError('Unreviewed SH integer division helper body')
        udiv_entry = symbol.address
        owners[(symbol.section, symbol.address - UDIV_PREFIX)] = ('libgcc-helper', symbol.address)
    bounds = {}
    for index in sections:
        bounds[index] = sorted((address, owner) for (sec, address), owner in owners.items()
                               if sec == index)

    def owner_at(section, address):
        rows = bounds[section]
        at = bisect_right([p[0] for p in rows], address) - 1
        return rows[at][1] if at >= 0 else None

    # Only actual forward PC loads mark literal pool data. A pool word whose
    # bits look like another MOV may not hide later instructions. This is
    # deliberately the same address-ordered rule as the FPU audit.
    literals = set()
    decoded = []
    for section in sorted(sections.values(), key=lambda s: s.address):
        for offset in range(0, len(section.data), 2):
            pc = section.address + offset
            if pc in literals:
                continue
            opcode = struct.unpack_from('<H', section.data, offset)[0]
            if opcode & 0xf000 == 0xd000:
                target = ((pc + 4) & ~3) + (opcode & 255) * 4
                literals.update((target, target + 2))
            elif opcode & 0xf000 == 0x9000:
                literals.add(pc + 4 + (opcode & 255) * 2)
            decoded.append((section, pc, opcode))
    if entries.keys() & literals:
        raise ValueError('Function/assembly entry overlaps a PC-relative literal pool')
    violations, edges = [], []
    for section, pc, opcode in decoded:
        if opcode >> 12 not in (0xa, 0xb):
            continue
        disp = opcode & 0xfff
        if disp & 0x800:
            disp -= 0x1000
        target = pc + 4 + disp * 2
        kind = 'BSR' if opcode >> 12 == 0xb else 'BRA'
        source_owner = owner_at(section.index, pc)
        target_section = next((s for s in sections.values()
                               if s.address <= target < s.address + len(s.data)), None)
        target_owner = owner_at(target_section.index, target) if target_section else None
        library = (udiv_entry is not None and kind == 'BSR' and
                   UDIV_EDGES.get(pc - udiv_entry) == target - udiv_entry)
        exact = target in entries and target not in literals
        internal = (kind == 'BRA' and source_owner is not None and
                    source_owner == target_owner and target_section == section and
                    target not in literals)
        allowed = library or exact or internal
        if source_owner and source_owner[0] == 'assembly' and kind == 'BSR':
            allowed = False  # No BSR exists in the admitted retail .S sources.
        label = ','.join(sorted(entries.get(source_owner[1], ()))) if source_owner else '<unowned>'
        edge = {'pc': hex(pc), 'kind': kind, 'source': label, 'target': hex(target),
                'target_entries': sorted(entries.get(target, ())),
                'pinned_libgcc_helper': library, 'allowed': allowed}
        edges.append(edge)
        if not allowed:
            violations.append(f'{name}: {kind} at {pc:#010x} in {label} targets '
                              f'{target:#010x}, not an exact function entry or same-function BRA')
    return {'image': name, 'direct_edges': len(edges), 'edges': edges,
            'literal_halfwords': len(literals), 'violations': violations}


def check_directory(directory):
    results = [audit_elf((Path(directory) / (name + '.elf')).read_bytes(), name)
               for name in IMAGES]
    errors = [message for result in results for message in result['violations']]
    if errors:
        raise ValueError('\n'.join(errors))
    return {result['image']: {'direct_edges': result['direct_edges'],
                              'literal_halfwords': result['literal_halfwords']}
            for result in results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', nargs='?', type=Path, default=Path('build/retail'))
    args = parser.parse_args()
    try:
        result = check_directory(args.directory)
    except (ValueError, OSError, UnicodeError, struct.error) as exc:
        raise SystemExit('Retail direct-call audit failed: ' + str(exc)) from exc
    print('PASS retail linked direct-call audit: ' + json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
