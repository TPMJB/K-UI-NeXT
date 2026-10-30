#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay the first native submit with a bounded SH7750 operand-cache model.

Supply an original Evolution executable and linked worker build. Actual original
SDK instructions execute its lock-pointer normalization, initializer and submit;
actual relocated C callbacks execute until return or their existing failure.
P1 operand accesses use 512 direct-mapped 32-byte lines, copy-back and optional
OIX indexing. P2 accesses bypass cache. Instruction fetch remains separate from
operand accesses, including PC-relative literal reads. DMA remains synchronous.

This is a targeted coherence model, not a full boot/cache/interrupt emulator.
IRQ registration scaffolding between normalization and SDK initialization is
not executed. It establishes a reachable dirty-line counterexample and a
specific linked eviction, not the entire console's preceding cache history.
No original executable bytes are embedded or written in the output.
"""
import argparse
import hashlib
import json
from pathlib import Path

from profile_vmu_native_sdk_worker_sh import WorkerSH, make_handoff
from check_vmu_native_worker_gateway import read_elf
from test_vmu_native_heap_execution import SH
from test_vmu_native_sdk_poll_execution import INITIALIZE, SUBMIT, DIGEST, IMAGE_BYTES

LOCK_CELL = 0x8C1DC538
LOCK = 0xAC1DC53C
LOCK_LINE = 0x0C1DC520
NORMALIZATION = (0x8C1B1790, 0x8C1B1792, 0x8C1B1794, 0x8C1B1796, 0x8C1B179A)


class CachedWorker(WorkerSH):
    def install_sdk_initializer(self):
        # Keep the original slot intact: this model executes the production
        # stage installer separately, rather than the flat model's shortcut.
        self.sdk_initializer_target = INITIALIZE

    def __init__(self, *args, oix=False, pre_tas_clean=False, pressure=False, **kwargs):
        self.cache = {}
        self.cache_enabled = False
        self.oix = oix
        self.pre_tas_clean = pre_tas_clean
        self.cache_events = []
        self.operand_accesses = []
        self.tas_count = 0
        self.pressure = pressure
        self.ocbp_records = []
        self.ccr = 0x105 | (0x80 if oix else 0)
        self.publication_events = []
        super().__init__(*args, **kwargs)

    def index(self, address):
        return (((address >> 25) & 1) << 8 | ((address >> 5) & 255)) if self.oix else ((address >> 5) & 511)

    def cached(self, address):
        return (self.cache_enabled and address & 0xE0000000 == 0x80000000 and
                0x0C000000 <= address & 0x1FFFFFFF < 0x0D000000)

    def writeback(self, entry, trigger, reason):
        if not entry[2]:
            return
        flag_before = SH.read(self, LOCK, 1)
        offset = entry[0] - 0x0C000000
        self.memory[offset:offset + 32] = entry[1]
        if entry[0] == LOCK_LINE:
            self.cache_events.append(dict(kind=reason, pc=hex(self.executing), trigger=hex(trigger),
                line=hex(entry[0]), flag_before=flag_before, flag_after=SH.read(self, LOCK, 1)))
        entry[2] = False

    def line(self, address):
        base, index = address & 0x1FFFFFE0, self.index(address)
        entry = self.cache.get(index)
        if entry is not None and entry[0] == base:
            return entry
        if entry is not None:
            self.writeback(entry, address, 'dirty_eviction')
        offset = base - 0x0C000000
        entry = [base, bytearray(self.memory[offset:offset + 32]), False]
        assert len(entry[1]) == 32
        self.cache[index] = entry
        return entry

    def clean_lock_before_tas(self):
        index = self.index(LOCK_CELL)
        entry = self.cache.get(index)
        if entry is not None and entry[0] == LOCK_LINE:
            self.writeback(entry, LOCK_CELL, 'pre_tas_ocbp')
            del self.cache[index]

    def read(self, address, count=4):
        if address == 0xFF00001C:
            assert count == 4
            return self.ccr
        # The bounded integer interpreters fetch exactly current PC and,
        # for delayed branches, PC+2 as two-byte instruction words. All data
        # loads in this replay target other addresses. PC-relative operands
        # start at PC+4 or later and therefore remain cached operand reads.
        fetch = count == 2 and address in (self.pc, self.pc + 2)
        if self.cached(address) and not fetch:
            entry = self.line(address)
            offset = address & 31
            assert offset + count <= 32
            self.operand_accesses.append((self.executing, address, count, 'read'))
            return int.from_bytes(entry[1][offset:offset + count], 'little')
        return super().read(address, count)

    def write(self, address, value, count=4):
        if address == 0xFF00001C:
            assert count == 4 and value & ~0x800 == self.ccr
            self.publication_events.append(dict(pc=hex(self.executing), ccr_write=hex(value)))
            self.ccr = value & ~0x800
            return
        if self.cached(address):
            entry = self.line(address)
            offset = address & 31
            assert offset + count <= 32
            entry[1][offset:offset + count] = (value & ((1 << (8 * count)) - 1)).to_bytes(count, 'little')
            entry[2] = True
            self.writes.append((self.executing, address, value, count))
            self.operand_accesses.append((self.executing, address, count, 'write'))
        else:
            super().write(address, value, count)

    def bridge_submit(self):
        if self.pressure:
            before = SH.read(self, LOCK, 1)
            self.executing = self.pc
            # An admitted P1 operand address in the worker, with exactly the
            # lock line's index and a different tag. Its value is unused.
            self.read(0x8CFD053C, 4)
            self.cache_events.append(dict(kind='post_tas_index_pressure', pc=hex(self.pc),
                target='0x8cfd053c', flag_before=before, flag_after=SH.read(self, LOCK, 1)))
        return super().bridge_submit()

    def plain(self, op, pc):
        if pc == getattr(self, 'stage_terminal', None):
            raise AssertionError('Actual stage installer refused: '+repr(self.r[4:6]))
        if pc == self.symbols.get('_kui_vmu_native_runtime_fault_hook', (None,))[0]:
            raise AssertionError('Actual coherence gateway refused: '+repr([hex(v) for v in self.r[4:8]]))
        if op & 0xF0FF == 0x00A3:
            address = self.r[(op >> 8) & 15]
            entry = self.cache.get(self.index(address))
            self.executing = pc
            self.ocbp_records.append(dict(pc=hex(pc), address=hex(address)))
            if entry is not None and entry[0] == address & 0x1FFFFFE0:
                self.writeback(entry, address, 'actual_ocbp')
                del self.cache[self.index(address)]
            self.instructions += 1
            return
        tas = op & 0xF0FF == 0x401B
        if tas:
            target = self.r[(op >> 8) & 15]
            assert target == LOCK, ('Unexpected TAS target', hex(target))
            if self.pre_tas_clean:
                self.executing = pc
                self.clean_lock_before_tas()
            before = SH.read(self, target, 1)
        super().plain(op, pc)
        if tas:
            self.tas_count += 1
            self.cache_events.append(dict(kind='original_tas_p2', pc=hex(pc), target=hex(target),
                flag_before=before, flag_after=SH.read(self, target, 1)))


def install_actual_stage(cpu, build):
    stage = read_elf(build / 'stage.elf')
    data, _, segments, _, symbols = stage
    for kind, offset, address, _, stored, memory, _, _ in segments:
        if kind == 1:
            cpu.store_block(address, data[offset:offset + stored] + bytes(memory - stored))
    wire = (Path(__file__).resolve().parents[1] / 'tests/fixtures/native-cache/evolution1.nvr').read_bytes()
    for name, payload in (('_native_driver', wire[64:168]), ('_passive_layout', wire[168:608]),
                          ('_native_heap', wire[608:680]), ('_native_exits', wire[700:752])):
        assert symbols[name][1] == len(payload)
        cpu.store_block(symbols[name][0], payload)
    SH.write(cpu, symbols['_exec_bytes'][0], IMAGE_BYTES)
    cpu.symbols['_stage_coherence_install'] = symbols['_native_sdk_coherence_install']
    cpu.stage_terminal = symbols['_native_report'][0]
    before = bytes(cpu.memory[0x10000:0x10000 + IMAGE_BYTES])
    cpu.cache_enabled = True
    exports = cpu.symbols['_kui_vmu_native_worker_exports'][0]
    size = len((build / 'native-worker.bin').read_bytes())
    result = cpu.worker('_stage_coherence_install', [exports, exports, size])
    assert result['result'] == 1
    after = bytes(cpu.memory[0x10000:0x10000 + IMAGE_BYTES])
    changes = [i for i, (a, b) in enumerate(zip(before, after)) if a != b]
    assert changes
    slot = (0x8C010000 + min(changes)) & ~3
    assert all(slot <= 0x8C010000 + i < slot + 4 for i in changes), 'Installer must modify one call-target word only'
    original_prefix = cpu.startup.read_bytes()[INITIALIZE - 0x8C010000:INITIALIZE - 0x8C010000 + 12]
    assert cpu.block(INITIALIZE, 12) == original_prefix, 'Original SDK initializer prefix must remain unchanged'
    target = SH.read(cpu, slot)
    gate = cpu.symbols['_kui_vmu_native_sdk_coherence_uniform_gate'][0] + 0x20000000
    assert target == gate
    return dict(stage_elf_sha256=hashlib.sha256((build / 'stage.elf').read_bytes()).hexdigest(),
        installer_instructions=result['instructions'], call_target_slot=hex(slot), installed_target=hex(target),
        changed_bytes=len(changes), original_initializer_prefix_unchanged=True)


def replay(startup, build, handoff, *, cached_stack, oix, pre_tas_clean,
           actual_stage=False, pressure=False, initializations=1, nop_second_ocbp=False):
    cpu = CachedWorker(startup, build, handoff, cached_stack=cached_stack,
        oix=oix, pre_tas_clean=pre_tas_clean, pressure=pressure)
    cpu.prepare()
    stage = install_actual_stage(cpu, build) if actual_stage else None
    cpu.cache_enabled = True
    mutation = None
    for cycle in range(initializations):
        if cycle == 1 and nop_second_ocbp:
            gates = [r for r in cpu.ocbp_records if int(r['address'], 16) in (LOCK_CELL, LOCK_CELL + 4)]
            assert [int(r['address'], 16) for r in gates] == [LOCK_CELL, LOCK_CELL + 4]
            mutations = []
            for gate in gates:
                at = int(gate['pc'], 16)
                op = SH.read(cpu, at, 2)
                assert op & 0xF0FF == 0x00A3 and at & 0xE0000000 == 0xA0000000
                SH.write(cpu, at, 9, 2)
                mutations.append(dict(pc=hex(at), before=hex(op), after='0x9'))
            mutation = dict(instructions=mutations, phase='second actual SDK initialization')
        for pc in NORMALIZATION:
            cpu.plain(SH.read(cpu, pc, 2), pc)
        assert cpu.read(LOCK_CELL) == LOCK
        if cycle == 0:
            assert SH.read(cpu, LOCK_CELL) == 0x8C1DC53C
        # This is the exact original wrapper call-target slot, populated by
        # the actual stage installer. Authentic R4..R7 and IMASK15 are passed;
        # neither original SDK entry nor cold gateway is substituted.
        target = cpu.read(int(stage['call_target_slot'], 16)) if stage else INITIALIZE
        cpu.original_call(target, [0x8C248408, 0xC000, 0x8C23C3E8, 0xC000])
    before = dict(pointer_p1=hex(cpu.read(LOCK_CELL)), pointer_ram=hex(SH.read(cpu, LOCK_CELL)),
        flag_ram=SH.read(cpu, LOCK, 1))
    refusal = None
    try:
        cpu.original_call(SUBMIT)
    except AssertionError as error:
        if not str(error).startswith(('Actual worker refused with code ', 'Actual coherence gateway refused:')):
            raise
        refusal = str(error)
    assert cpu.tas_count == 1
    state = cpu.symbols['_state'][0]
    fields = ('armed', 'pending', 'submitted', 'completed', 'error', 'source', 'bytes',
        'descriptors', 'nops', 'submit_sr', 'complete_sr', 'rx_slots', 'hash', 'first_rx',
        'first_response', 'last_response', 'fault_at', 'fault_value', 'fault_expect', 'runtime_result')
    sampled = {name: hex(SH.read(cpu, state + i * 4)) for i, name in enumerate(fields)}
    return dict(build=str(build), worker_elf_sha256=hashlib.sha256((build / 'native-worker.elf').read_bytes()).hexdigest(),
        cached_stack=cached_stack, ccr=hex(0x105 | (0x80 if oix else 0)), pre_tas_clean=pre_tas_clean,
        initial_operand_cache='empty before stage installer or original pointer-normalization store',
        stage=stage, post_tas_pressure=pressure, initializations=initializations, ocbp_mutation=mutation,
        actual_ocbp_instructions=cpu.ocbp_records, stage_publication=cpu.publication_events,
        before_submit=before, events=cpu.cache_events, refusal=refusal, state=sampled,
        final_ram_flag=SH.read(cpu, LOCK, 1), first_transfer_submitted=sampled['submitted'] == '0x1')


def primitive_cases(startup, build, handoff):
    checked = 0
    literal = 0x8CFD053C
    for oix in (False, True):
        def pending():
            cpu = CachedWorker(startup, build, handoff, cached_stack=True, oix=oix)
            cpu.cache_enabled = True
            cpu.write(LOCK_CELL, LOCK)
            cpu.r[0] = LOCK
            return cpu
        def tas(cpu):
            cpu.plain(SH.read(cpu, 0x8C1DAD78, 2), 0x8C1DAD78)
            assert SH.read(cpu, LOCK, 1) == 128
        cpu = pending();tas(cpu)
        cpu.pc = literal
        cpu.read(literal, 2)  # instruction fetch must not evict operand data
        assert SH.read(cpu, LOCK, 1) == 128
        assert cpu.cache[cpu.index(LOCK_CELL)][0] == LOCK_LINE
        checked += 1
        cpu.pc = literal - 16
        cpu.read(literal, 4)  # actual operand miss to identical index
        assert SH.read(cpu, LOCK, 1) == 0
        checked += 1
        cpu = pending();tas(cpu)
        cpu.read(literal + 0x20000000, 4)  # P2 operand access cannot evict
        assert SH.read(cpu, LOCK, 1) == 128
        checked += 1
        cpu = pending();cpu.clean_lock_before_tas();tas(cpu)
        cpu.read(literal, 4)
        assert SH.read(cpu, LOCK, 1) == 128
        checked += 1
        cpu = pending();tas(cpu);cpu.clean_lock_before_tas()
        assert SH.read(cpu, LOCK, 1) == 0  # cleaning after TAS is unsafe
        checked += 1
        assert cpu.index(LOCK_CELL) == cpu.index(literal)
        if oix:
            assert cpu.index(LOCK_CELL) == cpu.index(LOCK_CELL ^ 0x2000)
            assert cpu.index(LOCK_CELL) != cpu.index(LOCK_CELL ^ 0x02000000)
        else:
            assert cpu.index(LOCK_CELL) != cpu.index(LOCK_CELL ^ 0x2000)
            assert cpu.index(LOCK_CELL) == cpu.index(LOCK_CELL ^ 0x02000000)
        checked += 1
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--comparison-build', type=Path)
    parser.add_argument('--p2-stack-build', type=Path)
    parser.add_argument('--candidate-build', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    original = args.startup.read_bytes()
    assert len(original) == IMAGE_BYTES and hashlib.sha256(original).hexdigest() == DIGEST
    handoff = make_handoff()
    primitives = primitive_cases(args.startup, args.build, handoff)
    results = []
    for oix in (False, True):
        for preclean in (False, True):
            result = replay(args.startup, args.build, handoff, cached_stack=True,
                oix=oix, pre_tas_clean=preclean)
            if preclean:
                assert result['refusal'] is None and result['first_transfer_submitted']
            else:
                assert result['refusal'] == 'Actual worker refused with code 0x5'
                assert result['state']['fault_at'] == '0xac1dc53c'
                assert result['state']['fault_value'] == '0x0'
                assert result['state']['runtime_result'] == '0xfffffffc'
                assert any(e['kind'] == 'dirty_eviction' and e['flag_before'] == 128 and e['flag_after'] == 0 for e in result['events'])
            results.append(result)
        if args.comparison_build:
            result = replay(args.startup, args.comparison_build, handoff, cached_stack=False,
                oix=oix, pre_tas_clean=False)
            assert result['refusal'] is None and result['first_transfer_submitted']
            results.append(result)
        if args.p2_stack_build:
            result = replay(args.startup, args.p2_stack_build, handoff, cached_stack=False,
                oix=oix, pre_tas_clean=False)
            assert result['refusal'] == 'Actual worker refused with code 0x5'
            assert result['state']['fault_at'] == '0xac1dc53c'
            results.append(result)
        if args.candidate_build:
            for cycles, mutant in ((1, False), (2, False), (2, True)):
                result = replay(args.startup, args.candidate_build, handoff, cached_stack=True,
                    oix=oix, pre_tas_clean=False, actual_stage=True, pressure=True,
                    initializations=cycles, nop_second_ocbp=mutant)
                if mutant:
                    assert result['refusal'] == 'Actual worker refused with code 0x5'
                    assert result['state']['fault_at'] == '0xac1dc53c'
                else:
                    assert result['refusal'] is None and result['first_transfer_submitted']
                    assert result['final_ram_flag'] == 0
                    pressure_events = [r for r in result['events'] if r['kind'] == 'post_tas_index_pressure']
                    assert len(pressure_events) == 1 and pressure_events[0]['flag_before'] == pressure_events[0]['flag_after'] == 128
                results.append(result)
    output = dict(cache_primitive_cases=primitives, schema='kui.native.sdk-cache-first-submit.v1', executable_sha256=DIGEST,
        original_bytes_embedded=False, results=results,
        limitations=['Targeted P1-copy-back/direct-mapped operand cache model; P0 accesses and synchronous Maple DMA retain the existing flat model.',
            'Original IRQ registration scaffolding is omitted; initial empty-cache input is explicit. This reproduces a reachable dirty-line history, not every prior console cache access.',
            'No instruction cache timing, write-buffer timing, interrupts, bus latency, game mainloop or FMV model.',
            'The pre-TAS-clean switch is a counterfactual. Candidate cases execute the linked stage installer, its four-byte call-target change, the actual cold gateway and original SDK; a separately labeled OCBP-to-NOP mutation tests causality.'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + '\n')
    print(json.dumps(dict(output=str(args.output), cases=len(results), results=[dict(build=r['build'], ccr=r['ccr'],
        pre_tas_clean=r['pre_tas_clean'], refusal=r['refusal'], submitted=r['first_transfer_submitted']) for r in results])))


if __name__ == '__main__':
    main()
