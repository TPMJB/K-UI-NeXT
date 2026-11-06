#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked handoff's new typed warm-image branch.

Large pure CRC/copy calls use bounded independent host oracles; their actual
cache-scope assembly, all selection/seed/publication C, and ordinary capsule
CRC execute as linked SH. The authored source has the known CRC but a different
SHA: it is data for testing the handoff, never a verified Dreamcast executable.
The separate cold-image test requires the stage's fresh complete SHA owner.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from test_smash_menu_loader_runtime import (prepare, RuntimeCPU, RAW, TARGET,
                                           returning_seed, crc)
from check_vmu_native_worker_gateway import read_elf

BYTES, EXPECTED_CRC, SOURCE, SCRATCH = 1848288, 0xd2f2eafe, 0xace00000, 0xac400000


def collision_source():
    data = bytearray((i * 17 + 3) & 255 for i in range(BYTES))
    initial = crc(data)
    columns = []
    for bit in range(32):
        changed = bytearray(data)
        changed[-4 + bit // 8] ^= 1 << (bit % 8)
        columns.append(crc(changed) ^ initial)
    target = EXPECTED_CRC ^ initial
    rows = [sum(((columns[c] >> r) & 1) << c for c in range(32)) |
            (((target >> r) & 1) << 32) for r in range(32)]
    pivot = 0
    for column in range(32):
        found = next(r for r in range(pivot, 32) if rows[r] >> column & 1)
        rows[pivot], rows[found] = rows[found], rows[pivot]
        for r in range(32):
            if r != pivot and rows[r] >> column & 1:
                rows[r] ^= rows[pivot]
        pivot += 1
    mask = sum(((row >> 32) & 1) << ((row & 0xffffffff).bit_length() - 1) for row in rows)
    for bit in range(32):
        if mask >> bit & 1:
            data[-4 + bit // 8] ^= 1 << (bit % 8)
    assert crc(data) == EXPECTED_CRC
    assert hashlib.sha256(data).hexdigest() != '04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'
    return bytes(data)


class WarmCPU(RuntimeCPU):
    def step(self):
        pc = self.pc & ~0x20000000
        if pc == self.crc_entry and self.r[5:7] == [SOURCE, BYTES]:
            assert self.r[4] == 0 and self.original_ccr == 0x100
            self.large_crc_calls += 1
            self.r[0], self.pc = crc(self.source_bytes), self.pr
            return
        if pc == self.copy_entry and self.r[4:7] == [SCRATCH, SOURCE, BYTES]:
            assert self.original_ccr == 0x100
            self.large_copy_calls += 1
            self.store_block(SCRATCH, self.source_bytes)
            self.r[0], self.pc = SCRATCH, self.pr
            return
        super().step()


def exercise(image, segments, symbols, source, kind):
    cpu, _, argv, ip, _, _ = prepare(image, segments, symbols, 'shining_force')
    cpu.__class__ = WarmCPU
    cpu.large_crc_calls = cpu.large_copy_calls = 0
    cpu.crc_entry = symbols['_kui_cold_load_crc32_leaf'][0] & ~0x20000000
    cpu.copy_entry = symbols['_memcpy'][0] & ~0x20000000
    cpu.source_bytes = source if kind != 'crc_miss' else bytes([source[0] ^ 1]) + source[1:]
    payload = bytearray((i * 13 + 7) & 255 for i in range(2048))
    native = struct.pack('<8I', 0x5349554b, 0x31554e4d, 1, 304, 0, 0, 0, 0) + bytes(272)
    payload[768:1072] = native
    payload[1072:1440] = struct.pack('<8I', 0x5349554b, 0x3154524d, 1, 368, 0, 0, 0, 0) + bytes(336)
    payload[1536:1568] = struct.pack('<8I', 0x4349554b, 0x31304d49, 1, 32, 0, 0, 0, 0)
    if kind != 'no_capability':
        payload[448:472] = struct.pack('<6I', 0x4349554b, 0x31305041, 1, 24, BYTES, ~BYTES & 0xffffffff)
    cpu.capsule = bytes(payload)
    address = symbols['_kui_smash_menu_loader_runtime'][0]
    runtime = bytearray(cpu.block(address, 632))
    plan = bytearray(runtime[24:88])
    struct.pack_into('<2I', plan, 24, crc(payload), 768)
    struct.pack_into('<I', plan, 60, 0)
    struct.pack_into('<I', plan, 60, crc(plan))
    runtime[24:88] = plan
    struct.pack_into('<I', runtime, 220, 0)
    struct.pack_into('<I', runtime, 220, crc(runtime[:224]))
    cpu.store_block(address, runtime)
    for _ in range(14000000):
        if cpu.pc in (RAW, RAW - 0x20000000):
            cpu.raw_read()
        if cpu.pc == TARGET:
            break
        assert cpu.pc != 0xac007000, 'warm handoff stopped unexpectedly'
        cpu.step()
    else:
        raise AssertionError('warm handoff exceeded instruction bound')
    configured = struct.unpack('<8I', cpu.block(TARGET + 1536, 32))
    expected_warm = kind == 'crc_match'
    assert bool(configured[4]) == expected_warm
    assert cpu.large_crc_calls == (kind != 'no_capability')
    assert cpu.large_copy_calls == expected_warm
    if expected_warm:
        assert configured[:7] == (0x4349554b, 0x31304d49, 1, 32, 0x8c400000, BYTES, EXPECTED_CRC)
        normalized = struct.pack('<8I', *configured[:7], 0)
        assert configured[7] == crc(normalized)
        assert cpu.block(SCRATCH, BYTES) == source
    else:
        assert configured[4:] == (0, 0, 0, 0)
    assert cpu.block(TARGET + 768 + 32, 256) == argv
    assert cpu.block(TARGET + 768 + 288, 16) == ip
    assert cpu.block(TARGET + 1072, 368) == returning_seed()
    return dict(case=kind, instructions=cpu.instructions,
                fresh_source_crc_calls=cpu.large_crc_calls,
                preserved_source_copies=cpu.large_copy_calls,
                warm_seed_published=expected_warm, scratch_contents_checked=expected_warm,
                raw_capsule_reads=len(cpu.raw_calls))


def audit(elf):
    image, _, segments, _, symbols = read_elf(elf)
    source = collision_source()
    cases = [exercise(image, segments, symbols, source, kind)
             for kind in ('crc_match', 'crc_miss', 'no_capability')]
    return dict(schema='kui.smash.cold-image-handoff.v1', status='PASS',
                elf_sha256=hashlib.sha256(image).hexdigest(), cases=cases,
                source_bytes=BYTES, source_crc32=hex(EXPECTED_CRC),
                source_is_original_MDE=False, source_SHA_owner_proof_tested_separately=True,
                large_pure_crc_and_copy_oracles_explicit=True, console_timing_measured=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True, type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = audit(args.elf)
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
