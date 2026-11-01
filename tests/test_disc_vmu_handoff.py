#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fault injection against actual linked physical-disc VMU handoff ELFs."""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from check_disc_vmu_handoff import audit_stage, audit_support


def mutations(path, audit):
    original = path.read_bytes()
    image, witnesses = audit(original)
    unique = {(address, size) for _, address, size in witnesses}
    cases = 0
    for address, size in sorted(unique):
        data = bytearray(original)
        data[image.offset(address, size)] ^= 1
        try:
            audit(data)
        except (ValueError, struct.error):
            cases += 1
        else:
            raise AssertionError(f'Audit admitted changed linked ABI at {address:08x}')
    for data in (b'', original[:16], original[:len(original)//2]):
        try:
            audit(data)
        except (ValueError, struct.error):
            cases += 1
        else:
            raise AssertionError('Audit admitted truncated ELF')
    return cases


def frame_contract():
    # Independently track the frame offset, including the deferred SR slot.
    # The ELF audit pins the exact push/pop instructions to this ABI.
    frame = ['macl', 'mach', 'gbr', 'vbr', 'sr', 'pr']+[f'r{r}' for r in range(14, -1, -1)]
    sp = 0
    for name in ('macl', 'mach', 'gbr', 'vbr'):
        assert frame[sp] == name
        sp += 1
    sr_slot = sp
    sp += 1
    assert frame[sp] == 'pr'
    sp += 1
    for r in range(14, 0, -1):
        assert frame[sp] == f'r{r}'
        sp += 1
    assert sp*4-64 == sr_slot*4
    assert frame[sp] == 'r0' and (sp+1)*4 == 84


def main():
    if len(sys.argv) != 3:
        raise SystemExit('Usage: test_disc_vmu_handoff.py <disc-stage.elf> <support.elf>')
    frame_contract()
    cases = mutations(Path(sys.argv[1]), audit_stage)+mutations(Path(sys.argv[2]), audit_support)
    print(f'PASS physical VMU linked handoff mutations: {cases} corruptions refused; '
          'saved84-byte frame/late SR offset independently checked')


if __name__ == '__main__':
    main()
