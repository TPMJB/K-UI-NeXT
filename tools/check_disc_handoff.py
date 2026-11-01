#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the physical-disc stage's actual linked native bootstrap interface.

Host packet/hash tests cannot establish where SH assembly actually jumps.
Resolve the PC-relative loads from executable ELF bytes, require the audited
wrapper's instruction sequence, and check the literals that control its jumps
and CPU state. This is a bounded assembly-interface audit, not CPU emulation.
"""
import argparse
from pathlib import Path
import struct

from check_retail_calls import read_elf


def audit_elf(data):
    sections, symbols = read_elf(data)

    def symbol(name):
        found = [s.address for s in symbols if s.name == name]
        if len(found) != 1:
            raise ValueError(f'Requires one linked {name} symbol')
        return found[0]

    def read(address, size):
        found = [s for s in sections.values()
                 if s.address <= address and address + size <= s.address + len(s.data)]
        if len(found) != 1:
            raise ValueError(f'Instruction/literal outside executable ELF at {address:08x}')
        s = found[0]
        return s.data[address-s.address:address-s.address+size]

    entry = symbol('_kui_disc_stage_enter')
    if entry & 3 or not 0x8ce00000 <= entry < 0x8ce10000:
        raise ValueError('Disc handoff entry is outside its aligned high-stage reservation')
    pc = entry

    def instruction(expected, what):
        nonlocal pc
        actual = struct.unpack('<H', read(pc, 2))[0]
        if actual != expected:
            raise ValueError(f'{what} at {pc:08x}: opcode {actual:04x}, expected {expected:04x}')
        pc += 2

    def literal(register, expected, what):
        nonlocal pc
        code = struct.unpack('<H', read(pc, 2))[0]
        if code & 0xff00 != 0xd000 | register << 8:
            raise ValueError(f'{what} at {pc:08x}: requires PC-relative MOV.L into r{register}')
        address = ((pc + 4) & ~3) + (code & 255) * 4
        value = struct.unpack('<I', read(address, 4))[0]
        if value != expected:
            raise ValueError(f'{what} at {pc:08x}: linked literal {value:08x}, expected {expected:08x}')
        pc += 2
        return address

    literal(0, (entry + 6) | 0x20000000, 'uncached wrapper target')
    instruction(0x402b, 'jump to uncached wrapper')
    instruction(0x0009, 'uncached jump delay slot')
    literal(0, 0x8c000000, 'cache publication start')
    literal(1, 0x8d000000, 'cache publication end')
    loop = pc
    instruction(0x00a3, 'publish RAM cache line')
    instruction(0x7020, 'advance cache line by32 bytes')
    instruction(0x3012, 'compare publication end')
    branch = pc
    displacement = (loop - (branch + 4)) // 2
    instruction(0x8b00 | (displacement & 255), 'bounded cache publication loop')
    literal(0, 0xff00001c, 'CCR address')
    literal(1, 0x00000909, 'retail cache mode')
    instruction(0x2012, 'write retail cache mode')
    for _ in range(8):
        instruction(0x0009, 'CCR synchronization NOP')
    literal(0, 0x700000f0, 'masked native SR/SSR')
    instruction(0x403e, 'set SSR')
    instruction(0x400e, 'set SR')
    literal(0, 0x8c000000, 'native GBR')
    instruction(0x401e, 'set GBR')
    literal(15, 0x8c00f400, 'native SP')
    instruction(0x4f2e, 'set native VBR from SP')
    literal(0, 0x00040001, 'native FPSCR')
    if symbol('__retail_boot_fpscr_init') != pc:
        raise ValueError('FPSCR audit witness does not name its actual LDS instruction')
    instruction(0x406a, 'initialize FPSCR')
    # Return code immediately follows the final jump and its delay slot.
    returned = pc + 44
    literal(0, returned, 'bootstrap return handler')
    instruction(0x402a, 'set bootstrap PR')
    target_literal = literal(14, 0xac00e000, 'bootstrap2 jump target')
    instruction(0x4e4e, 'set bootstrap SPC')
    for register in range(14):
        instruction(0xe000 | register << 8, f'clear r{register}')
    instruction(0x0028, 'clear MAC')
    instruction(0x0008, 'clear T')
    instruction(0x4e2b, 'jump through verified bootstrap2 r14')
    instruction(0xee00, 'clear r14 in bootstrap jump delay slot')
    if pc != returned:
        raise ValueError('Bootstrap return address differs from the linked fallthrough')
    literal(15, 0x8cff0000, 'return high-stage SP')
    handler = symbol('_kui_disc_stage_returned')
    literal(0, handler, 'returned-bootstrap diagnostic function')
    instruction(0x402b, 'jump to returned-bootstrap diagnostic')
    instruction(0x0009, 'return-handler jump delay slot')
    return {'entry': entry, 'bootstrap2': 0xac00e000, 'target_literal': target_literal,
            'sp': 0x8c00f400, 'vbr': 0x8c00f400, 'sr': 0x700000f0}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', type=Path, nargs='?', default=Path('build/disc-native/stage.elf'))
    args = parser.parse_args()
    try:
        result = audit_elf(args.stage.read_bytes())
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f'Physical-disc handoff audit failed: {error}\n')
    print('PASS physical-disc linked handoff: '
          f'bootstrap2={result["bootstrap2"]:08X} SP/VBR={result["sp"]:08X} '
          f'SR/SSR={result["sr"]:08X}; controlling literals, cache publication, '
          'FPSCR, return handler and register setup verified')


if __name__ == '__main__':
    main()
