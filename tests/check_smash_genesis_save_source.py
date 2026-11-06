#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the reviewed external MDE SRAM-header parser against synthetic headers.

The owner must supply the exact original executable. No retail instructions or
ROM payloads are embedded or written to the report. This is a bounded scalar
contract check; it does not execute a game, VMU filesystem or hardware.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tests'), str(ROOT / 'tools')]
from test_native_standalone_foreground_gin import Gin
from test_vmu_native_heap_execution import MASK

SPEC = importlib.util.spec_from_file_location('smash_headers',
    ROOT / 'tools/kui-smash-genesis-save-headers.py')
headers = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(headers)
EXPECTED = '04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'
ENTRY, LOAD, ROM, SIZE, TYPE, DIRTY = (
    0x8c018066, 0x8c010000, 0x8cc00000, 0x8c48f3e0, 0x8c48f3e4, 0x8c48f3e8)
RETURN = 0x8c00dead


class HeaderSH(Gin):
    """Existing bounded SH interpreter plus this path's ADDC instruction."""
    def plain(self, op, pc):
        n, m = op >> 8 & 15, op >> 4 & 15
        if op & 0xf00f == 0x300e:  # addc Rm,Rn
            value = self.r[n] + self.r[m] + (self.sr & 1)
            self.r[n] = value & MASK
            self.flag(value > MASK)
            self.instructions += 1
        else:
            super().plain(op, pc)


def check(image):
    digest = hashlib.sha256(image).hexdigest()
    if digest != EXPECTED or len(image) != 1848288:
        raise ValueError('MDE does not match the reviewed source owner')
    cpu = HeaderSH(b'', image, b'')
    cpu.store_block(LOAD, image)
    count, instructions = 0, 0

    def case(kind, high, invalid=None):
        nonlocal count, instructions
        header = bytearray(512)
        header[0x1b0:0x1b4] = b'RA' + bytes((kind, 0x20))
        header[0x1ba] = high
        if invalid is not None:
            header[invalid] = 0
        cpu.store_block(ROM, header)
        cpu.write(SIZE, 0x11111111)
        cpu.write(TYPE, 0x22222222)
        cpu.write(DIRTY, 0xa5a5a5a5)
        cpu.pc, cpu.pr = ENTRY, RETURN
        cpu.writes, cpu.instructions = [], 0
        for _ in range(200):
            if cpu.pc == RETURN:
                break
            cpu.step()
        expected = headers.engine_sram_bytes(header)
        if cpu.pc != RETURN or cpu.r[0] != expected or cpu.read(SIZE) != expected:
            raise AssertionError(('Original SRAM parser differs', kind, high, invalid))
        if cpu.read(DIRTY) != 0xa5a5a5a5 or cpu.mmio_reads or cpu.mmio_writes:
            raise AssertionError('Header parser changed dirty state or touched MMIO')
        if any(address not in (SIZE, TYPE) or width != 4
               for _, address, _, width in cpu.writes):
            raise AssertionError('Header parser wrote outside its two scalar cells')
        count += 1
        instructions += cpu.instructions

    # Every admitted type and size byte; each unrecognized type; each fixed
    # signature/attribute rejection. Header payloads are authored synthetic data.
    for kind in (0xf0, 0xf8, 0xe0):
        for high in range(256):
            case(kind, high)
    for kind in range(256):
        if kind not in (0xf0, 0xf8, 0xe0):
            case(kind, 0x3f)
    for invalid in (0x1b0, 0x1b1, 0x1b3):
        case(0xf8, 0x3f, invalid)
    return dict(schema='kui.smash-genesis-save-source-check.v1', owner_sha256=digest,
        parser_entry=hex(ENTRY), synthetic_cases=count, instructions=instructions,
        result='Original source parser matches header collector classification',
        model='Bounded SH integer execution; no game, filesystem or timing execution',
        limitations='SRAM eligibility does not prove high-score persistence')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mde', type=Path, help='Original owner-supplied MDE_US.BIN')
    args = parser.parse_args()
    print(json.dumps(check(args.mde.read_bytes()), indent=2))


if __name__ == '__main__':
    main()
