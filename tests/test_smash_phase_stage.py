#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the diagnostic stage's trace import, marks and bounded photo report.

Only the separately audited observational clock and terminal display are ABI
boundaries. These fixtures establish diagnostic placement/formatting, not time.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from check_retail_stack import base, load
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from test_smash_phase_clock_linked import ClockCPU

BYTES, CAPACITY, REPORT_LINES = 992, 24, 20
PHASES = (1, 2, 3, 4, 5, *range(10, 26))
STACK, CALLER = 0x8c00d000, 0x8c003000


def require(value, message):
    if not value:
        raise ValueError(message)


def sample(phase, flags=7, rtc=None):
    cycles = phase * 200000000
    return struct.pack('<8I', flags, 0xc023, 0xe0a, cycles >> 32,
                       cycles & 0xffffffff, 100 + phase if rtc is None else rtc, 0, 0)


def trace(phases=(), flags=7, rtc=None):
    records = b''.join(struct.pack('<2I', phase, 0) + sample(phase, flags, rtc(phase) if rtc else None)
                       for phase in phases)
    return struct.pack('<8I', 0x5349554b, 0x314d4954, 1, BYTES,
                       len(phases), 0, 0, 0) + records + bytes(BYTES - 32 - len(records))


class StageCPU(ClockCPU):
    def __init__(self, image):
        super().__init__(image)
        self.symbols = image[4]
        self.lines = []
        self.resets = 0
        self.clock_reads = 0
        self.stop = None
        self.clock_stamp = sample(10)

    def string(self, address, limit=96):
        result = bytearray()
        for i in range(limit):
            byte = self.read(address + i, 1)
            if byte == 0:
                return result.decode('ascii')
            result.append(byte)
        raise ValueError('Diagnostic display read an unterminated/outsize line')

    def boundary(self):
        if self.pc == self.symbols['_kui_smash_phase_clock_read'][0]:
            self.clock_reads += 1
            self.store_block(self.r[4], self.clock_stamp)
        elif self.pc == self.symbols['_retail_display_line'][0]:
            self.lines.append(self.string(self.r[4]))
        elif self.pc == self.symbols['_retail_display_phase_reset'][0]:
            self.resets += 1
        elif self.pc == self.symbols['_stopped'][0]:
            self.stop = (self.string(self.r[4]), self.r[5])
            return True
        else:
            return False
        self.pc = self.pr
        return True

    def invoke(self, name, args=(), terminal=False):
        self.r = [(0x53530000 + i * 0x12345) & 0xffffffff for i in range(16)]
        self.r[4:4 + len(args)] = args
        self.r[15], self.pr, self.pc, self.sr = STACK, CALLER, self.symbols[name][0], 0x700000f0
        before = (self.r[8:15].copy(), self.gbr, self.vbr, self.fr.copy(), self.fpscr)
        self.writes = []
        self.instructions = 0
        pcs = []
        for _ in range(300000):
            if self.pc == CALLER:
                require(not terminal, 'Terminal timing report returned to the game')
                require(self.r[8:15] == before[0] and self.r[15] == STACK and
                        (self.gbr, self.vbr, self.fr, self.fpscr) == before[1:] and
                        self.sr & ~0x301 == 0x700000f0 & ~0x301,
                        'Trace import/mark changed caller nonvolatile or IRQ state')
                return 'returned'
            if self.boundary():
                if self.stop:
                    return 'refused'
                continue
            pcs.append(self.pc)
            self.step()
            if terminal and len(self.lines) >= REPORT_LINES and len(pcs) >= 24 and len(set(pcs[-24:])) <= 3:
                require(len(self.lines) == REPORT_LINES, 'Timing stop did not publish its complete one-page report')
                return 'held'
        raise ValueError('Actual stage diagnostic exceeded its finite instruction bound')


def audit(build):
    require(__debug__, 'Run linked models without Python optimization')
    build = Path(build)
    path = build / 'stage.elf' if build.is_dir() else build
    image = read_elf(path)
    symbols = image[4]
    helpers = {}
    for name in ('_smash_phase_stage_begin', '_smash_phase_stage_report',
                 '_kui_smash_phase_stage_mark', '_smash_phase_column', '_smash_phase_pair'):
        at, body, _ = symbol_bytes(image, name, 2)
        helpers[name] = dict(address=hex(at), bytes=len(body), sha256=hashlib.sha256(body).hexdigest())
    seed = symbols['__smash_phase_trace_seed'][0]
    target, size, kind, owner = symbols['_kui_smash_phase_stage_trace']
    require(size == BYTES and kind == 1 and image[3][owner][1] == 8,
            'Stage trace is not one exact private992-byte BSS object')
    cpu = StageCPU(image)
    require(cpu.block(seed, BYTES) == trace(), 'Linked diagnostic trace seed is not blank')
    incoming = trace(PHASES[:5])
    cpu.store_block(seed, incoming)
    cpu.store_block(target - 16, bytes([0xa7]) * (BYTES + 32))
    require(cpu.invoke('_smash_phase_stage_begin') == 'returned', 'Valid carried trace refused')
    carried = cpu.block(target, BYTES)
    require(struct.unpack_from('<I', carried, 16)[0] == 6 and
            carried[32:32 + 5 * 40] == incoming[32:32 + 5 * 40] and
            struct.unpack_from('<I', carried, 32 + 5 * 40)[0] == 10 and cpu.clock_reads == 1,
            'Stage did not import all five low marks before adding stage entry')
    require(cpu.block(target - 16, 16) == bytes([0xa7]) * 16 and
            cpu.block(target + BYTES, 16) == bytes([0xa7]) * 16,
            'Stage import escaped its private typed trace')
    # Original game loads may overwrite the entry capsule. Imported history
    # must be independent; only the next temporary high-stage mark changes it.
    cpu.store_block(seed, bytes([0xdb]) * BYTES)
    cpu.clock_stamp = sample(11)
    require(cpu.invoke('_kui_smash_phase_stage_mark', [11]) == 'returned', 'Valid card mark refused')
    require(cpu.block(target, 32 + 6 * 40)[32:] == carried[32:32 + 6 * 40] and
            struct.unpack_from('<I', cpu.block(target, BYTES), 16)[0] == 7,
            'Stage mark reread overwritten capsule history')

    refused = []
    for name, bad in (
        ('bad_magic', bytes(4) + incoming[4:]),
        ('excess_count', incoming[:16] + struct.pack('<I', CAPACITY + 1) + incoming[20:]),
        ('late_phase', trace((1, 6))),
        ('unordered', trace((2, 1))),
        ('unused_not_zero', incoming[:-1] + b'\x01'),
    ):
        malformed = StageCPU(image)
        malformed.store_block(seed, bad)
        require(malformed.invoke('_smash_phase_stage_begin') == 'refused' and
                malformed.stop[0] == 'TIMING SEED INVALID' and not malformed.clock_reads and
                not malformed.lines and not malformed.hardware_reads,
                'Malformed carried timing history did not stop before adding or displaying data: ' + name)
        refused.append(name)

    reports = []
    for name, record in (
        ('valid_nominal_and_rtc', trace(PHASES)),
        ('unavailable_samples', trace(PHASES, flags=0)),
        ('rtc_reset_maximum_delta', trace(PHASES, rtc=lambda phase: 0xffffffff if phase & 1 else 0)),
    ):
        report = StageCPU(image)
        report.store_block(target, record)
        report.store_block(symbols['_smash_phase_io'][0], struct.pack('<6I', 31, 7, 9, 65, 2, 3))
        before_trace = report.block(target, BYTES)
        require(report.invoke('_smash_phase_stage_report', terminal=True) == 'held', 'Timing report failed to hold')
        require(report.resets == 1 and len(report.lines) == REPORT_LINES and
                all(len(line) <= 49 for line in report.lines), 'Photo report escapes its20line/49column bounds')
        require(report.clock_reads == 0 and not report.hardware_reads and
                report.block(target, BYTES) == before_trace,
                'Formatting changed trace, observed hardware, or charged the photo hold to a phase')
        require('BEFORE MDE ENTRY' in report.lines[-2] and 'GAME SGA LOAD NOT TIMED' in report.lines[-1],
                'Report conceals its pre-game measurement boundary')
        require(any('VMU SINGLE' in line for line in report.lines) and
                any('         7' in line and '         9' in line and '         2' in line for line in report.lines),
                'Report omitted the temporary stage card request counters')
        aggregate = symbols['_smash_phase_delta_flags'][0]
        require(all((STACK - 4096 <= address < address + width <= STACK) or
                    (address == aggregate and width == 4)
                    for _, address, _, width in report.writes),
                'Report formatting wrote outside its private caller stack')
        accumulated = 0
        for _, address, value, width in report.writes:
            if address == aggregate:
                require(width == 4 and value | accumulated == value,
                        'Report aggregate discarded a previous clock validity flag')
                accumulated = value
        if name == 'valid_nominal_and_rtc':
            require(any('TOTAL' in line and '24000' in line for line in report.lines),
                    'Nominal total differs from authored24-second phase interval')
        if name == 'unavailable_samples':
            require(all('NA' in line for line in report.lines[2:11]), 'Unavailable samples became apparent0ms phases')
        reports.append(dict(case=name, lines=report.lines, max_columns=max(map(len, report.lines))))
    malformed = StageCPU(image)
    malformed.store_block(target, trace((2, 1)))
    require(malformed.invoke('_smash_phase_stage_report', terminal=True) == 'refused' and
            malformed.stop[0] == 'TIMING TRACE INVALID' and not malformed.lines,
            'Invalid report trace was displayed')

    # Bind the tested roots to the actual main/relay and persistence callers.
    graphs = [path.parent / 'stage/src/loader/vmu_native_heap_probe_stage.ci',
              path.parent / 'stage/src/loader/vmu_persist_stage.ci',
              path.parent / 'stage/src/core/vmu_persist_session.ci']
    _, calls = load(graphs, qualified=True)
    def edges(name):
        matches = [key for key in calls if base(key) == name]
        require(len(matches) == 1, 'Missing/ambiguous actual phase caller: ' + name)
        return {base(target) for target, _ in calls[matches[0]]}
    for caller, callee in (
        ('kui_retail_stage_main', 'smash_phase_stage_begin'),
        ('kui_retail_stage_main', 'kui_smash_phase_stage_mark'),
        ('kui_retail_stage_relay', 'smash_phase_stage_report'),
        ('kui_retail_stage_relay', 'kui_smash_phase_stage_mark'),
        ('native_persist_storage_init', 'kui_smash_phase_stage_mark'),
        ('kui_vmu_persist_session_init', 'kui_smash_phase_stage_mark'),
        ('vmu_persist_stage_prepare', 'kui_smash_phase_stage_mark'),
    ):
        require(callee in edges(caller), 'Actual stage lacks diagnostic call edge: ' + caller + '→' + callee)
    return dict(schema='kui.smash-phase.stage-linked.v1', status='PASS',
        elf_sha256=hashlib.sha256(image[0]).hexdigest(), helpers=helpers,
        imported_low_marks=5, private_trace_bytes=BYTES,
        source_capsule_not_reread=True, malformed_seed_rejections=refused,
        reports=reports, terminal_report_returns=False,
        compiler_graph_sha256={str(p.relative_to(path.parent)): hashlib.sha256(p.read_bytes()).hexdigest() for p in graphs},
        limits=['Actual linked import, mark and report instructions execute; clock samples and display callbacks are finite ABI fixtures.',
                'The first payload arrival omits the earlier selection gap; the stop precedes game/SDK/SGA execution and cannot time a later menu return.',
                'Compiler call edges bind diagnostic roots to production stage phases; source/build provenance and full launch semantics are covered by the encompassing audit.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    rendered = json.dumps(audit(args.build), indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end='')


if __name__ == '__main__':
    main()
