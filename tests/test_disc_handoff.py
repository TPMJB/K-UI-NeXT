#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Corrupt a real linked physical-disc stage to exercise its ABI audit.

Usage: python3 tests/test_disc_handoff.py build/disc-native/stage.elf
No compiler or emulator is needed; the ordinary stage build supplies the ELF.
"""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from check_disc_handoff import audit_elf
from check_loader_layout import EH, SH


def main():
    if len(sys.argv) != 2:
        raise SystemExit('Usage: test_disc_handoff.py <valid linked stage.elf>')
    original = Path(sys.argv[1]).read_bytes()
    result = audit_elf(original)
    entry = result['entry']
    header = EH.unpack_from(original)
    sections = [SH.unpack_from(original, header[6] + i * SH.size)
                for i in range(header[12])]

    def offset(address, size):
        matches = [s for s in sections if s[1] == 1 and
                   s[3] <= address and address + size <= s[3] + s[5]]
        assert len(matches) == 1
        s = matches[0]
        return s[4] + address - s[3]

    def half(address):
        return struct.unpack_from('<H', original, offset(address, 2))[0]

    # Locate actual controlling loads in this fixed assembly interface,
    # independent of section placement, function location or pool displacement.
    loads = []
    for address in range(entry, entry + 112, 2):
        code = half(address)
        if code & 0xf000 == 0xd000:
            literal = ((address + 4) & ~3) + (code & 255) * 4
            value = struct.unpack_from('<I', original, offset(literal, 4))[0]
            loads.append((address, (code >> 8) & 15, literal, value))

    def load(register, value, last=False):
        found = [row for row in loads if row[1] == register and row[3] == value]
        assert found
        return found[-1] if last else found[0]

    cases = 0

    def refuses(name, changes, reason):
        nonlocal cases
        data = bytearray(original)
        for address, fmt, value in changes:
            struct.pack_into(fmt, data, offset(address, struct.calcsize(fmt)), value)
        try:
            audit_elf(data)
        except ValueError as error:
            assert reason in str(error), (name, str(error))
        else:
            raise AssertionError(f'Audit admitted corrupted linked stage: {name}')
        cases += 1

    target = result['target_literal']
    refuses('original550 bootstrap1 target', [(target, '<I', 0xac008300)], 'bootstrap2 jump target')
    refuses('bypassed bootstrap direct executable', [(target, '<I', 0xac010000)], 'bootstrap2 jump target')
    refuses('cached bootstrap alias', [(target, '<I', 0x8c00e000)], 'bootstrap2 jump target')
    stack = load(15, 0x8c00f400)
    refuses('wrong native stack', [(stack[2], '<I', 0x8c00e400)], 'native SP')
    refuses('VBR not set from stack', [(stack[0] + 2, '<H', 0x0009)], 'native VBR')
    sr = load(0, 0x700000f0)
    refuses('interrupt mask lowered', [(sr[2], '<I', 0x700000e0)], 'masked native SR/SSR')
    refuses('native bank/block bits changed', [(sr[2], '<I', 0x600000f0)], 'masked native SR/SSR')
    refuses('SSR write omitted', [(sr[0] + 2, '<H', 0x0009)], 'set SSR')
    refuses('SR write omitted', [(sr[0] + 4, '<H', 0x0009)], 'set SR')
    cache = load(1, 0x00000909)
    refuses('wrong cache mode', [(cache[2], '<I', 0x00000808)], 'retail cache mode')
    ram_end = load(1, 0x8d000000)
    refuses('partial cache publication', [(ram_end[2], '<I', 0x8c800000)], 'publication end')
    refuses('cache publication loop removed', [(entry + 16, '<H', 0x0009)], 'publication loop')
    gbr = load(0, 0x8c000000, last=True)
    refuses('wrong GBR', [(gbr[2], '<I', 0x8c010000)], 'native GBR')
    fpscr = load(0, 0x00040001)
    refuses('wrong FPSCR', [(fpscr[2], '<I', 0x00000000)], 'native FPSCR')
    refuses('FPSCR initialization omitted', [(fpscr[0] + 2, '<H', 0x0009)], 'initialize FPSCR')
    boot = load(14, 0xac00e000)
    refuses('bootstrap loaded into wrong register', [(boot[0], '<H', half(boot[0]) ^ 0x0100)], 'r14')
    refuses('SPC setup omitted', [(boot[0] + 2, '<H', 0x0009)], 'set bootstrap SPC')
    refuses('general register not cleared', [(boot[0] + 4, '<H', 0xe001)], 'clear r0')
    jump = boot[0] + 36
    assert half(jump) == 0x4e2b and half(jump + 2) == 0xee00
    refuses('final jump uses unproved register', [(jump, '<H', 0x402b)], 'verified bootstrap2 r14')
    refuses('final delay slot changes state', [(jump + 2, '<H', 0xee01)], 'delay slot')
    returned = load(15, 0x8cff0000)
    refuses('return stack overlaps game', [(returned[2], '<I', 0x8c010000)], 'return high-stage SP')
    try:
        audit_elf(original[:EH.size])
    except ValueError:
        cases += 1
    else:
        raise AssertionError('Audit admitted a truncated linked ELF')
    print(f'PASS physical-disc linked handoff mutations: {cases} corruptions refused; '
          'actual resolved jumps, SP/VBR, SR/SSR, cache, GBR, FPSCR and delay slots')


if __name__ == '__main__':
    main()
