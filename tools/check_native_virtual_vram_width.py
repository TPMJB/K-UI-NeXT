#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded emitted-instruction audit of the native halfword payload sink.

This follows direct branches and delay slots in the actual linked symbol.
It rejects calls, unknown instructions and every store except MOV.W Rm,@Rn.
It is not a CPU execution model or a proof of caller routing/MMU behavior.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_vmu_native_worker_gateway import read_elf, symbol_bytes


def require(condition, message):
    if not condition:
        raise ValueError(message)


def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def audit_code(address, code):
    require(0 < len(code) <= 256 and not (address | len(code)) & 1,
            "sink must be an aligned, bounded SH symbol")
    visited, literals, pending = set(), set(), [(address, False)]
    stores, byte_reads, halfword_reads, returns = [], [], [], []

    def word(pc):
        require(address <= pc and pc + 2 <= address + len(code),
                "control flow or literal escapes sink at " + hex(pc))
        return struct.unpack_from('<H', code, pc - address)[0]

    while pending:
        pc, delay_slot = pending.pop()
        if (pc, delay_slot) in visited:
            continue
        require(len(visited) < 256, "sink instruction bound exceeded")
        visited.add((pc, delay_slot))
        op = word(pc)
        successors = []
        if op == 0x000B:  # RTS, delayed
            require(not delay_slot, "return in delay slot")
            returns.append(pc)
            successors = [(pc + 2, True)]
        elif op & 0xF000 == 0xA000:  # BRA, delayed
            require(not delay_slot, "branch in delay slot")
            successors = [(pc + 2, True),
                          (pc + 4 + 2 * signed(op & 0xFFF, 12), False)]
        elif op & 0xFF00 in (0x8900, 0x8B00, 0x8D00, 0x8F00):
            require(not delay_slot, "conditional branch in delay slot")
            target = pc + 4 + 2 * signed(op & 255, 8)
            if op & 0x0400:  # BT/S or BF/S
                successors = [(pc + 2, True), (pc + 4, False), (target, False)]
            else:
                successors = [(pc + 2, False), (target, False)]
        else:
            if op & 0xF000 == 0x9000:  # PC-relative MOV.W literal
                at = pc + 4 + 2 * (op & 255)
                word(at)
                literals.add(at)
            elif op & 0xF000 == 0xD000:  # PC-relative MOV.L literal
                at = ((pc + 4) & ~3) + 4 * (op & 255)
                word(at)
                word(at + 2)
                literals.update((at, at + 2))
            elif op & 0xF00F == 0x2001:  # Only admitted store form
                stores.append(dict(pc=hex(pc), opcode=f'{op:04x}', bytes=2))
            elif op & 0xF00F == 0x6004:  # MOV.B @Rm+,Rn
                byte_reads.append(pc)
            elif op & 0xF00F == 0x6001:  # MOV.W @Rm,Rn
                halfword_reads.append(pc)
            else:
                # Integer register operations emitted by this bounded sink.
                plain = (op == 0x0009 or  # NOP
                         op & 0xF000 in (0x7000, 0xE000) or  # ADD/MOV imm
                         op & 0xFF00 in (0xC800, 0xC900) or  # TST/AND imm
                         op & 0xF0FF in (0x4010, 0x4018) or  # DT/SHLL8
                         op & 0xF00F in (0x6003, 0x600C, 0x600D,
                                           0x2009, 0x200B, 0x300C))
                require(plain, "unadmitted instruction/store/call at " +
                        hex(pc) + ': ' + f'{op:04x}')
            if not delay_slot:
                successors = [(pc + 2, False)]
        pending.extend(successors)

    instructions = {pc for pc, _ in visited}
    require(not instructions & literals, "literal pool is executable")
    require(len(stores) == 1, "require exactly one reachable halfword store site")
    require(len(byte_reads) == 1 and len(halfword_reads) == 1 and len(returns) == 1,
            "unexpected source-read/destination-read/return sites")
    return dict(instruction_sites=len(instructions), halfword_store_sites=stores,
                source_byte_read_sites=[hex(x) for x in byte_reads],
                halfword_read_sites=[hex(x) for x in halfword_reads],
                literal_halfwords=[hex(x) for x in sorted(literals)],
                calls=0, byte_stores=0, wider_stores=0)


def audit(path):
    path = Path(path)
    image = read_elf(path)
    address, code, _ = symbol_bytes(image, '_native_vram_write', 2)
    result = audit_code(address, code)
    # Check that changing this emitted destination store to MOV.B or MOV.L
    # cannot accidentally pass the same instruction decoder.
    offset = int(result['halfword_store_sites'][0]['pc'], 16) - address
    original = struct.unpack_from('<H', code, offset)[0]
    rejected = []
    for width, low in ((1, 0), (4, 2)):
        changed = bytearray(code)
        struct.pack_into('<H', changed, offset, (original & ~15) | low)
        try:
            audit_code(address, changed)
        except ValueError:
            rejected.append(width)
        else:
            raise ValueError('store-width mutation unexpectedly admitted')
    return dict(status='PASS', schema='kui.native.virtual-vram-width.v1',
                elf=str(path), elf_sha256=hashlib.sha256(image[0]).hexdigest(),
                symbol='_native_vram_write', address=hex(address), bytes=len(code),
                symbol_sha256=hashlib.sha256(code).hexdigest(),
                emitted_store_width_mutation_rejections=rejected, **result,
                hardware_executed=False,
                limits=[
                    'Audits reachable emitted sink instructions and literal pools only.',
                    'Register values, address alignment and byte-preservation semantics require separate source/host checks.',
                    'Caller dispatch, MMU translation, cache/DMA timing and console acceptance are not established.'
                ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = audit(args.elf)
    encoded = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    print(encoded, end='')


if __name__ == '__main__':
    main()
