#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject FPU use in the linked retail code, including libgcc helpers.

The pinned Dreamcast compiler excludes the -m4-nofpu target. Instead reserve
the exposed FPU registers and use integer division, as documented in
https://gcc.gnu.org/onlinedocs/gcc/SH-Options.html. Verify the resulting linked
instructions, not just those compiler flags. SH literal pools need explicit
PC-relative load classification; -d excludes embedded non-executable blobs.
"""
from pathlib import Path
import re
import subprocess
import sys


def main():
    directory = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/retail')
    instructions = 0
    for name in ('resident', 'stage', 'entry'):
        disassembly = subprocess.check_output(
            ['sh-elf-objdump', '-d',
             str(directory / (name + '.elf'))], text=True)
        (directory / (name + '.dis')).write_text(disassembly)
        decoded = []
        for line in disassembly.splitlines():
            found = re.match(r'^\s*([0-9a-f]+):\s+([0-9a-f]{2})\s+([0-9a-f]{2})\s+(\S+)(?:\s+(.*))?$', line)
            if not found:
                continue
            address = int(found.group(1), 16)
            opcode = int(found.group(2), 16) | int(found.group(3), 16) << 8
            decoded.append((address, opcode, found.group(4), found.group(5) or '', line))
        # MOV.L @(disp,PC),Rn loads four bytes at aligned(PC+4)+4*disp.
        # MOV.W uses PC+4+2*disp. These referenced bytes are embedded data,
        # even when objdump happens to decode e.g. the FF00 halfword of a
        # FF000000 address mask as FADD. Never classify an opcode from its
        # bit pattern alone: require objdump to identify the PC load as well.
        literals = set()
        for address, opcode, mnemonic, _, _ in decoded:
            if opcode & 0xf000 == 0xd000 and mnemonic == 'mov.l':
                target = ((address + 4) & ~3) + (opcode & 255) * 4
                literals.update((target, target + 2))
            elif opcode & 0xf000 == 0x9000 and mnemonic == 'mov.w':
                literals.add(address + 4 + (opcode & 255) * 2)
        seen = 0
        for address, _, mnemonic, operands, line in decoded:
            if address in literals:
                continue
            if mnemonic.startswith('.'):
                continue
            operands = operands.split('!', 1)[0].split(';', 1)[0]
            if mnemonic.startswith('f') or re.search(
                    r'\b(?:fpul|fpscr|(?:fr|dr|xd|xf)\d+)\b', operands):
                raise SystemExit(f'FPU use in {name}: {line.strip()}')
            seen += 1
        if not seen:
            raise SystemExit(f'No decoded instructions in {name}')
        instructions += seen
    print(f'PASS retail linked instruction audit: {instructions} instructions, no FPU use')


if __name__ == '__main__':
    main()
