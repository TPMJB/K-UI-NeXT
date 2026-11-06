#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked early-reset entry with an ABI-conforming busy callee."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tools")]
from check_vmu_native_worker_gateway import read_elf
from test_smash_menu_loader_entry import LoaderCPU

ENTRY, CALLER = 0xac008300, 0x8c003000


def exercise(image, segments, symbols, pattern, sr, ccr):
    cpu = LoaderCPU(ccr)
    for p in segments:
        if p[0] == 1:
            cpu.store_block(p[2], image[p[1]:p[1] + p[4]])
    cpu.r = [((0x12345678 + 0x11111111 * i) ^ pattern) & 0xffffffff for i in range(15)] + [0x8c00f400]
    cpu.sr, cpu.pr, cpu.gbr, cpu.vbr = sr, CALLER, 0x8c2f9b80 ^ pattern, 0x8c004000 ^ pattern
    cpu.macl, cpu.mach, cpu.pc = 0xabcd ^ pattern, 0x1234 ^ pattern, ENTRY
    before = (list(cpu.r), cpu.sr, cpu.pr, cpu.gbr, cpu.vbr, cpu.macl, cpu.mach)
    main = symbols['_kui_smash_menu_return_main'][0]
    calls = waits = 0
    expected_frame = [before[5], before[6], before[3], before[4], before[1], before[2]] + list(reversed(before[0][:15]))
    for _ in range(10000):
        assert cpu.pc != CALLER, 'Busy reset resumed an old cleanup caller'
        if cpu.pc == main:
            calls += 1
            assert cpu.r[15] == 0xac00c000 and cpu.r[4] == 0x8c00f3ac
            assert cpu.sr == (sr | 0x100000f0)
            words = [cpu.read(cpu.r[4] + 4 * i) for i in range(21)]
            assert words == expected_frame
            if calls == 2:
                break
            for i in range(8):
                cpu.r[i] = 0xa5a50000 + i
            cpu.r[0] = 0xffffffff
            cpu.gbr, cpu.vbr, cpu.macl, cpu.mach = 0x8c111111, 0x8c222222, 0x3333, 0x4444
            cpu.sr ^= 1
            cpu.pc = cpu.pr
        if cpu.pc == symbols['__smash_menu_return_wait_owner'][0]:
            waits += 1
            assert (cpu.sr, cpu.gbr, cpu.vbr, cpu.r[15]) == (sr, before[3], before[4], 0x8c00f3ac)
            # An IRQ saves below the parked owner SP and restores its GPRs.
            for i in range(16):
                cpu.write(cpu.r[15]-4*(i+1), 0x1a1a0000+i)
            assert [cpu.read(0x8c00f3ac+4*i) for i in range(21)] == expected_frame
        cpu.step()
    else:
        raise AssertionError('Busy entry exceeded its instruction bound')
    assert calls == 2 and waits == 1 and not cpu.mmio_writes and not cpu.mmio_reads
    return cpu.instructions


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf', type=Path, required=True)
    p.add_argument('--report', type=Path)
    a = p.parse_args()
    image, _, segments, _, symbols = read_elf(a.elf)
    assert symbols['_kui_smash_menu_return_entry'][0] == ENTRY
    values = [exercise(image, segments, symbols, pattern, sr, ccr)
              for pattern in range(8) for sr in (0x40000000, 0x40000001, 0x60000000, 0x60000001)
              for ccr in (0, 0x100, 0x909, 0x109)]
    report = dict(schema='kui.smash-menu.return-entry.v1', passed=True,
                  elf_sha256=hashlib.sha256(image).hexdigest(), busy_cases=len(values),
                  instructions=sorted(set(values)), actual_gbr_preserved=True,
                  original_owner_frame_preserved=True, no_old_cleanup_caller_resumed=True,
                  owner_irq_context_restored_between_retries=True,
                  limits=['The C body is replaced with one ABI-conforming busy result and a second-entry stop; the separate runtime test executes C and its retry bound.',
                          'IRQ stack pushes are explicit fixtures; register banking and interrupt timing are not emulated. SR masks leave RB unchanged. No FPU instruction is used.'])
    text = json.dumps(report, indent=2) + '\n'
    if a.report:
        a.report.parent.mkdir(parents=True, exist_ok=True);a.report.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
