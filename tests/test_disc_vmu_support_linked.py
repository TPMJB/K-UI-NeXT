#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Corrupt actual linked support bytes to exercise header/terminal checks."""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_disc_vmu_support import audit
from vmu_native_relocations import EH, SH


def main():
    if len(sys.argv) != 3:
        raise SystemExit("Usage: test_disc_vmu_support_linked.py <support.elf> <support.bin>")
    original = Path(sys.argv[1]).read_bytes()
    flat = Path(sys.argv[2]).read_bytes()
    result = audit(original, flat)
    h = EH.unpack_from(original)
    sections = [SH.unpack_from(original, h[6]+i*SH.size) for i in range(h[12])]
    terminal = result["terminal"]

    def positions(address, size):
        found = [s for s in sections if s[1] == 1 and s[2] & 2 and
                 s[3] <= address and address + size <= s[3] + s[5]]
        assert len(found) == 1
        s = found[0]
        return s[4]+address-s[3], (address & 0x1fffffff)-0x0c112000

    def half(address):
        offset, _ = positions(address, 2)
        return struct.unpack_from("<H", original, offset)[0]

    loads = []
    # All eight terminal controlling loads precede its final jump/stop loop.
    for address in range(terminal, terminal+112, 2):
        opcode = half(address)
        if opcode & 0xf000 == 0xd000:
            literal = ((address+4) & ~3) + (opcode & 255)*4
            offset, _ = positions(literal, 4)
            loads.append((address, (opcode >> 8) & 15, literal,
                          struct.unpack_from("<I", original, offset)[0]))

    def load(value):
        found = [row for row in loads if row[3] == value]
        assert len(found) == 1, (hex(value), found)
        return found[0]

    cases = 0

    def refuses(changes):
        nonlocal cases
        data, binary = bytearray(original), bytearray(flat)
        for address, fmt, value in changes:
            size = struct.calcsize(fmt)
            offset, physical = positions(address, size)
            struct.pack_into(fmt, data, offset, value)
            struct.pack_into(fmt, binary, physical, value)
        try:
            audit(data, binary)
        except ValueError:
            cases += 1
        else:
            raise AssertionError("Support audit admitted corrupted header or terminal")

    refuses([(0xac112004, "<I", 2)])
    refuses([(0xac112008, "<I", 80)])
    refuses([(0xac112020, "<I", 0xac113fe0)])
    refuses([(0xac112024, "<I", 1)])
    for old, new in [(0x100000f0, 0x000000f0), (0x44565342, 0),
                     (0xac112000, 0xac112020), (0x12000, 0x11000),
                     (0x4b4e5031, 0x4b4e5030), (0x20000000, 0), (4096, 2048)]:
        refuses([(load(old)[2], "<I", new)])
    refuses([(terminal, "<H", 0x0009)])
    refuses([(terminal+6, "<H", 0x0009)])
    # A validation refusal may never branch into the pointer-use sequence.
    assert half(terminal+16) & 0xff00 == 0x8b00
    refuses([(terminal+16, "<H", 0x8b01)])
    jumps = [address for address in range(terminal, terminal+112, 2)
             if half(address) == 0x432b]
    assert len(jumps) == 1
    jump = jumps[0]
    refuses([(jump-4, "<H", 0x0009)])
    refuses([(jump-2, "<H", 0x0009)])
    refuses([(jump, "<H", 0x442b)])
    refuses([(jump+2, "<H", 0x2f36)])
    print(f"PASS physical-disc linked support mutations: {cases} corruptions refused; "
          "exports, interrupt mask, exact worker target and P1 private-stack handoff")


if __name__ == "__main__":
    main()
