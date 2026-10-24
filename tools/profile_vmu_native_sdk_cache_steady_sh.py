#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the linked native worker lifecycle with the bounded operand-cache model.

This is a targeted SH7750 P1 copy-back cache replay, not a game/mainloop,
interrupt, instruction-cache or timing emulator. Original executable bytes are
read locally and never included in this tool or its report. Synchronous Maple
DMA and P0 accesses retain the existing model's explicit limitations.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from profile_vmu_native_sdk_cache_sh import (
    CachedWorker, LOCK, LOCK_CELL, NORMALIZATION, install_actual_stage,
)
from profile_vmu_native_sdk_worker_sh import FRAME, make_handoff, diff
from test_vmu_native_heap_execution import SH
from test_vmu_native_sdk_poll_execution import CONSUME, DIGEST, IMAGE_BYTES, READY, RX, SUBMIT, TX


class SteadyCachedWorker(CachedWorker):
    def store_block(self, address, data):
        # The existing assembly-frame bridge supplies saved registers using
        # a raw RAM block write. With a cache enabled that would impersonate
        # an external DMA write and leave the previous cached saved PR/SR.
        # Actual owner-stack saves are CPU stores through P1. Model these
        # frame stores through the same cache; do not flush or drop a line.
        if address == FRAME and getattr(self, 'cache_enabled', False):
            assert len(data) == 76 and not address & 3
            for offset in range(0, len(data), 4):
                self.write(address + offset, struct.unpack_from('<I', data, offset)[0])
            return
        super().store_block(address, data)


def replay(startup, build, handoff, rounds, oix):
    cpu = SteadyCachedWorker(startup, build, handoff, cached_stack=True, oix=oix, pressure=True)
    cpu.prepare()
    stage = install_actual_stage(cpu, build)
    for pc in NORMALIZATION:
        cpu.plain(SH.read(cpu, pc, 2), pc)
    target = cpu.read(int(stage['call_target_slot'], 16))
    cpu.r[3] = target
    cpu.original_call(target, [0x8C248408, 0xC000, 0x8C23C3E8, 0xC000])
    coherence_state = cpu.symbols['_kui_vmu_native_sdk_coherence_state'][0]
    assert cpu.read(coherence_state + 44) == 1
    assert SH.read(cpu, LOCK, 1) == 0
    assert SH.read(cpu, LOCK_CELL) == LOCK
    history = []
    for i in range(rounds):
        cpu.original_call(SUBMIT)
        pending = cpu.read(cpu.symbols['_controller_pair'][0])
        live_ready = cpu.read(cpu.symbols['_live_plan'][0] + 4)
        cpu.original_call(0x8C1DAEF6)
        tx_before = cpu.block(TX, 0x6000)
        rx_before = cpu.block(RX, 0xC000)
        complete = cpu.complete_bridge()
        tx_after = cpu.block(TX, 0x6000)
        rx_after = cpu.block(RX, 0xC000)
        cpu.original_call(CONSUME)
        consumer_rx = cpu.block(RX, 0xC000)
        cpu.original_call(READY)
        tx_changes = diff(tx_before, tx_after)
        rx_changes = diff(rx_before, rx_after)
        consumer_changes = diff(rx_after, consumer_rx)
        assert not tx_changes and not consumer_changes, (i, tx_changes, consumer_changes)
        assert SH.read(cpu, LOCK, 1) == 0 and SH.read(cpu, LOCK_CELL) == LOCK
        if i >= 2:
            assert pending == 1 and live_ready == 0x4E4C5031, (i, pending, hex(live_ready))
            assert rx_changes == [dict(offset=hex((i % 2) * 0x6000 + 2), before='0x20', after='0x22')], (i, rx_changes)
        history.append(dict(round=i, submitted=True, completion_result=complete['result'],
            physical_frames=len(cpu.batch['frames']), controller_pair_pending=pending,
            live_ready=hex(live_ready), complete_rx_differences=rx_changes))
    pressure = [e for e in cpu.cache_events if e['kind'] == 'post_tas_index_pressure']
    assert len(pressure) == rounds
    assert all(e['flag_before'] == e['flag_after'] == 128 for e in pressure)
    # Original submit and the original completion gate each acquire the
    # same SDK lock. Forced pressure is attached only to the submit hook.
    assert cpu.tas_count == 2 * rounds
    state = cpu.symbols['_state'][0]
    assert SH.read(cpu, state + 8) == rounds and SH.read(cpu, state + 12) == rounds
    return dict(ccr=hex(cpu.ccr), rounds=rounds, stage=stage, cold_handoffs=1,
        submitted_callbacks=SH.read(cpu, state + 8), completed_callbacks=SH.read(cpu, state + 12),
        tas_instructions=cpu.tas_count, forced_index_conflicts=len(pressure),
        held_lock_preserved_for_every_conflict=True, final_lock=SH.read(cpu, LOCK, 1),
        cold_ocbp_instructions=cpu.ocbp_records, history=history)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=64)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 6 <= args.rounds <= 1000:
        parser.error('--rounds must be between 6 and 1000')
    original = args.startup.read_bytes()
    assert len(original) == IMAGE_BYTES and hashlib.sha256(original).hexdigest() == DIGEST
    handoff = make_handoff()
    results = [replay(args.startup, args.build, handoff, args.rounds, oix) for oix in (False, True)]
    report = dict(schema='kui.native.sdk-cache-steady.v1', original_executable_sha256=DIGEST,
        worker_elf_sha256=hashlib.sha256((args.build / 'native-worker.elf').read_bytes()).hexdigest(),
        worker_binary_sha256=hashlib.sha256((args.build / 'native-worker.bin').read_bytes()).hexdigest(),
        original_bytes_embedded=False, results=results,
        limitations=['Bounded P1 copy-back operand-cache model, both OIX index modes; P0 accesses remain flat.',
            'Original normalization, SDK initializer, submit, consumer and ready code execute. IRQ-registration scaffolding and assembly save/restore frames are provided by the existing bridge.',
            'The frame bridge models owner-stack saves as cached P1 stores, rather than an out-of-band RAM copy; no cache lines are discarded to make frame updates visible.',
            'Maple DMA packets are synchronous. No interrupts, write-buffer timing, instruction-cache timing, SD transport, game mainloop, video or FMV model.',
            'A forced same-index P1 data read after every original P2 TAS is explicit test pressure, not an observed full-console access trace.'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(output=str(args.output), modes=len(results), pairs=sum(r['rounds'] for r in results),
        all_forced_conflicts_preserved_held_lock=True)))


if __name__ == '__main__':
    main()
