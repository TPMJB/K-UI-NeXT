#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject FPU use in the linked retail code, including libgcc helpers.

The pinned Dreamcast compiler excludes the -m4-nofpu target. Instead reserve
both FPU banks/control registers and use integer division, as documented in
https://gcc.gnu.org/onlinedocs/gcc/SH-Options.html. Verify the resulting linked
instructions, not just those compiler flags. GNU SH objdump classifies literal
pools as data directives; -d excludes the embedded non-executable blobs.
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
            ['sh-elf-objdump', '-d', '--no-show-raw-insn',
             str(directory / (name + '.elf'))], text=True)
        (directory / (name + '.dis')).write_text(disassembly)
        seen = 0
        for line in disassembly.splitlines():
            found = re.match(r'^\s*[0-9a-f]+:\s+(\S+)(?:\s+(.*))?$', line)
            if not found:
                continue
            mnemonic, operands = found.group(1), found.group(2) or ''
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
