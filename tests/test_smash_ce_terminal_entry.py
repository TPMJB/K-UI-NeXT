#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the normal CE child's actual low E0 entry to its terminal renderer.

No CE worker or renderer is simulated as successful menu return. The exact
entry must retain the confirmed mask/stack/argument contract without touching
the opt-in observer interval or reading live helper/KData memory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tests'), str(ROOT / 'tools')]
from check_vmu_native_worker_gateway import read_elf
from test_smash_menu_loader_runtime import RuntimeCPU


class TerminalCPU(RuntimeCPU):
    def __init__(self, ccr):
        super().__init__(bytes(2048))
        self.original_ccr = ccr

    def read(self, address, count=4):
        physical = address & 0x1fffffff
        if (0x0c008300 <= physical < 0x0c00c000 or
                0x0cfc4000 <= physical < 0x0cfd0000 or
                0x0c1d1800 <= physical < 0x0c1d6000):
            raise AssertionError('Normal E0 entered observer/helper/KData memory')
        return super().read(address, count)


def exercise(image, command, sr, sp, ccr):
    cpu = TerminalCPU(ccr)
    for p in image[2]:
        if p[0] == 1:
            cpu.store_block(p[2], image[0][p[1]:p[1] + p[4]] + bytes(p[5] - p[4]))
    caller = 0x01d2190e
    cpu.r[4], cpu.pr, cpu.sr, cpu.r[15] = command, caller, sr, sp
    cpu.pc = image[4]['_kui_retail_menu_hook'][0]
    report = image[4]['_kui_retail_menu_return'][0]
    for _ in range(160):
        if cpu.pc == report:
            break
        cpu.step()
    else:
        raise AssertionError('Normal CE E0 did not directly reach its renderer')
    assert cpu.r[4:7] == [command, caller, sp]
    # Both recognized CMP/EQ branches set T before the unchanged terminal.
    assert cpu.sr == sr | 0x100000f1
    private = cpu.r[15] & 0x1fffffff
    assert 0x0c004000 <= private < 0x0c008000 and cpu.r[15] != sp
    assert cpu.original_ccr == ccr and not cpu.cache_writes
    assert not cpu.mmio_reads and not cpu.mmio_writes and not cpu.raw_calls
    return dict(command=command, entry_sr=hex(sr), entry_sp=hex(sp), ccr=hex(ccr),
                renderer_sr=hex(cpu.sr), renderer_sp=hex(cpu.r[15]),
                command_caller_stack_exact=True, observer_or_helper_reads=False,
                CCR_or_hardware_writes=False, instructions=cpu.instructions)


def audit(resident, confirmed_resident=None, stage=None):
    image = read_elf(resident)
    assert '__kui_smash_ce_return_controller' not in image[4], \
        'An opted-in CE return controller needs its distinct complete linked qualification'
    assert image[4]['__kui_smash_ce_return_probe_mode'][0] == 0
    reference = read_elf(confirmed_resident) if confirmed_resident else None
    cases = []
    for command in (1, 3):
        for sr in (0x40000000, 0x40000001, 0x50000000, 0x50000001):
            for sp in (0x06123450, 0x8c123450):
                for ccr in (0x105, 0x101, 0x909, 0x901):
                    row = exercise(image, command, sr, sp, ccr)
                    if reference:
                        old = exercise(reference, command, sr, sp, ccr)
                        assert (row['renderer_sr'], row['renderer_sp']) == (
                            old['renderer_sr'], old['renderer_sp'])
                    cases.append(row)
    if stage:
        symbols = read_elf(stage)[4]
        assert '__kui_smash_ce_return_controller' not in symbols and '__kui_smash_ce_return_transport' not in symbols, \
            'New CE return stages cannot use the historical terminal-only proof'
        assert '__smash_ce_return_probe_start' not in symbols
        assert '__smash_ce_return_probe_end' not in symbols
        assert '_smash_ce_return_prepared' not in symbols
    return dict(schema='kui.smash-ce.normal-terminal-entry.v1', status='PASS',
                resident_sha256=hashlib.sha256(image[0]).hexdigest(),
                confirmed_resident_sha256=(hashlib.sha256(reference[0]).hexdigest() if reference else None),
                stage_sha256=(hashlib.sha256(Path(stage).read_bytes()).hexdigest() if stage else None),
                observer_enabled=False, actual_SH_cases=len(cases), cases=cases,
                CE_return_to_menu_implemented=False,
                limits=['The renderer is the terminal boundary; no console video or CE retirement is claimed.'])


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--resident', type=Path, required=True)
    p.add_argument('--confirmed-resident', type=Path)
    p.add_argument('--stage', type=Path)
    p.add_argument('--report', type=Path)
    a = p.parse_args()
    result = audit(a.resident, a.confirmed_resident, a.stage)
    rendered = json.dumps(result, indent=2) + '\n'
    if a.report:
        a.report.write_text(rendered)
    print(rendered, end='')
