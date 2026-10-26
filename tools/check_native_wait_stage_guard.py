#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked Evolution wait-capture guard before ordinary stage copy.

The real cold installer/original allocator prepares its real lease. Execution
then resumes at native_passive_run with verified external E1 metadata. The
actual identity predicate, memcmp and mode decoder execute; only the terminal
stop and first ordinary memcpy are observation boundaries. No worker arming,
publication, game IRQ scheduling or hardware compatibility is claimed.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path

from check_native_bottom_heap import ABSOLUTE_ONE, Cold, ROOT, Refused, require
from check_vmu_native_worker_gateway import read_elf

SCHEMA = 'kui.native.evolution1.wait-capture-stage-guard.v1'
SOURCES = ('tools/check_native_wait_stage_guard.py', 'tools/check_native_bottom_heap.py',
    'tools/profile_vmu_native_release_sh.py', 'tests/test_vmu_native_heap_execution.py',
    'tools/check_vmu_native_worker_gateway.py', 'tools/sh4_delay_slots.py',
    'src/loader/vmu_native_passive_stage.inc', 'src/loader/vmu_native_trace.c',
    'src/loader/minic.c', 'src/core/vmu_native_mode.c')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Handoff(Exception):
    pass


class FirstCopy(Exception):
    pass


class Guard(Cold):
    def __init__(self, build, row):
        self.guard_running = False
        super().__init__(build, row)

    def leaf(self, name):
        if name == '_native_passive_run' and not self.guard_running:
            require(self.lease_at == self.r[4] and self.published == self.begin + 32 and
                    self.read(self.control_at + 8) == 3,
                    'Guard entry lacks the original validated cold lease')
            raise Handoff
        if name == '_stopped':
            message = self.block(self.r[4], 80).split(b'\0', 1)[0].decode('ascii')
            self.stop = dict(message=message, detail=self.r[5])
            self.refusal = (0, self.r[5])
            raise Refused
        if name == '_memcpy':
            target, source, count = self.r[4:7]
            require(self.stage_begin <= target < target + count <= self.stage_end and count,
                    'First ordinary memcpy escaped private stage storage')
            self.first_copy = dict(target=hex(target), source=hex(source), bytes=count)
            raise FirstCopy
        return super().leaf(name)

    def read(self, address, count=4):
        if self.guard_running:
            if address == self.memcmp_at and count == 2:
                require(self.r[4] == self.driver_at + 72 and self.r[6] == 32,
                        'Actual memcmp does not compare the complete driver SHA256')
                self.memcmp_calls += 1
                self.digest_peer = self.r[5]
            if count == 1 and self.driver_at + 72 <= address < self.driver_at + 104:
                self.digest_reads.add(address - self.driver_at - 72)
        return super().read(address, count)

    def prepare(self, wire, mutation=None, mode=0):
        self.guard_running = False
        self.watch = False
        self.driver_at = self.symbols['_native_driver'][0]
        require(self.symbols['_native_driver'][1:3] == (104, 1),
                'Driver object lacks the exact typed104-byte ELF layout')
        self.store_block(self.driver_at, wire[64:168])
        self.write(self.symbols['__native_runtime_mode_start'][0] + 16, mode)
        if mutation:
            at, width, value = mutation
            self.write(self.driver_at + at, value, width)
        self.leaves.pop(self.symbols['_native_passive_run'][0])
        for name in ('_stopped', '_memcpy'):
            self.leaves[self.symbols[name][0]] = name
        self.memcmp_at = self.symbols['_memcmp'][0]
        self.stop = self.first_copy = None
        self.memcmp_calls = 0
        self.digest_peer = None
        self.digest_reads = set()
        self.entered.clear()
        self.instructions = 0
        self.writes.clear()
        self.watch = True
        self.guard_running = True


def run_case(baseline, wire, name, mutation=None, mode=0, accept=False):
    cpu = copy.deepcopy(baseline)
    cpu.prepare(wire, mutation, mode)
    try:
        result = cpu.run_cold()
    except FirstCopy:
        result = True
    require(result == accept, 'Unexpected guard result: ' + name)
    require('_kui_vmu_native_trace_image' in cpu.entered,
            'Actual linked image predicate was not entered')
    if accept:
        require(cpu.first_copy is not None and cpu.stop is None and
                cpu.memcmp_calls == 1 and cpu.digest_reads == set(range(32)),
                'Exact E1 NORMAL did not execute full memcmp before ordinary copy')
    else:
        require(cpu.first_copy is None and cpu.stop is not None,
                'Refusal reached ordinary copy or missed terminal stop')
        # Only stage-local C stacks may be written before the terminal guard.
        require(all(cpu.stack_low <= (address & 0x1fffffff) + 0x80000000 <
                    cpu.stack_low + 0x10000 for _,address,_,_ in cpu.writes),
                'Refused guard wrote beyond its private installer stack')
    return dict(name=name, mode=mode, admitted_before_copy=accept,
        refused_before_copy=not accept, instructions=cpu.instructions,
        actual_memcmp_calls=cpu.memcmp_calls, digest_byte_offsets_read=sorted(cpu.digest_reads),
        first_copy=cpu.first_copy, stop=cpu.stop, writes=len(cpu.writes))


def check(build, startup, record):
    build = build.resolve()
    artifacts = {p.name: sha(p) for p in sorted(build.iterdir())
                 if p.suffix in ('.elf', '.bin', '.reloc') and p.is_file()}
    for file, role in (('stage.elf', 'stage'), ('native-worker.elf', 'worker')):
        require(read_elf(build/file)[4].get('__kui_native_e1_wait_capture_' + role) == ABSOLUTE_ONE,
                'Frozen artifact lacks exact wait-capture marker: ' + file)
    provenance = json.loads((build/'entry.compile.json').read_text())
    require(provenance.get('source_dirty') is False and
            provenance.get('elf_sha256') == artifacts['entry.elf'],
            'Runtime compile provenance does not bind this clean frozen entry')
    row = dict(slug='evolution1', startup=str(startup.resolve()), record=str(record.resolve()))
    baseline = Guard(build, row)
    try:
        baseline.run_cold()
    except Handoff:
        pass
    else:
        raise ValueError('Original cold installer did not reach real guard entry')
    wire = record.read_bytes()
    positive = run_case(baseline, wire, 'exact-evolution1-normal', accept=True)
    mutations = [('digest-byte-' + str(i), (72+i, 1, wire[136+i] ^ 1)) for i in range(32)]
    mutations += [('image-bytes', (12, 4, len(baseline.image)-4)),
                  ('load-address', (16, 4, 0x8c010004))]
    negatives = [run_case(baseline, wire, name, mutation) for name,mutation in mutations]
    negatives += [run_case(baseline, wire, 'non-normal-mode-' + str(mode), mode=mode)
                  for mode in range(1, 7)]
    require(all(sha(build/name) == digest for name,digest in artifacts.items()),
            'Frozen runtime artifact changed during guard proof')
    return dict(schema=SCHEMA, status='PASS', source_commit=provenance['commit'],
        scope=__doc__.strip(), build=str(build), enabledmarker=True,
        artifact_sha256=artifacts, source_sha256={name:sha(ROOT/name) for name in SOURCES},
        inputs={name:dict(path=row[name], sha256=sha(row[name])) for name in ('startup','record')},
        baseline_cold=dict(original_calls=baseline.owner_calls, payload=hex(baseline.published)),
        positive_case=positive, refusal_cases=negatives, refusal_count=len(negatives))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--startup', type=Path, required=True)
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = check(args.build, args.startup, args.record)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(schema=SCHEMA, status=report['status'],
        output=str(args.output), refusals=report['refusal_count'])))


if __name__ == '__main__':
    main()
