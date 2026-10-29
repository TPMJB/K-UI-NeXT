#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the General-only removal of unreachable PROFILE observations.

This checks actual linked state absence and the retained ABI callback, not
gameplay compatibility. General's cold mode refusal is exercised separately
by the production split-controller host and linked-worker models.
"""
import argparse
import json
from pathlib import Path

from check_vmu_native_worker_gateway import read_elf, symbol_bytes

ONE = (1, 0, 0, 0xfff1)
ELIDED = '__kui_native_general_timeline_elided_worker'
GENERAL = '__kui_native_split_controller_general_worker'
SPLIT = '__kui_native_split_controller_isolation_worker'
PRIVATE = ('_native_loader_timeline', '_native_loader_event', '_native_loader_clock',
           '_diagnostic_phase', '_diagnostic')
HELPERS = ('_native_loader_snapshot', '_native_loader_clock_read', '_native_phase_image',
           '_native_phase_begin', '_native_phase_end', '_native_raw_owner_capture', '_bin_at')
CALLBACK = '_kui_native_loader_timeline_visit'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def inspect(worker, stage=None):
    symbols = worker[4]
    if ELIDED not in symbols:
        return None
    require(all(symbols.get(name) == ONE for name in (ELIDED, GENERAL, SPLIT,
                '__kui_native_loader_timeline_worker',
                '__kui_native_general_fault_capture_elided_worker')),
            'Elided timeline requires the exact General split-controller profile')
    if stage is not None:
        require(stage[4].get('__kui_native_split_controller_general_stage') == ONE,
                'Elided worker lacks its paired General stage')
    require(all(name not in symbols for name in PRIVATE + HELPERS),
            'General retained an unreachable PROFILE collector, state or helper')
    require('_kui_vmu_native_fault_capture_install' not in symbols,
            'General retained its unreachable optional vector installer')
    address, body, section = symbol_bytes(worker, CALLBACK, 2)
    require(body == bytes.fromhex('0b000900') and section[2] & 7 == 6,
            'General timeline ABI callback must be exactly RTS / NOP')
    return dict(enabled=True, scope='General NORMAL-only', callback_address=hex(address),
                callback_bytes=4, callback_exact_rts_nop=True,
                callback_memory_reads=0, callback_memory_writes=0,
                callback_register_and_sr_changes=0,
                removed_private_objects=list(PRIVATE),
                historical_profile_variants_changed=False,
                cold_mode_refusal_requires_separate_worker_test=True)


def check(worker, stage=None, *, self_test=False):
    result = inspect(worker, stage)
    if result is None:
        return None
    rejected = 0
    if self_test:
        variants = []
        changed = list(worker); changed[4] = dict(worker[4]); del changed[4][GENERAL]
        variants.append(changed)
        changed = list(worker); changed[4] = dict(worker[4]); changed[4][PRIVATE[0]] = (0, 3420, 1, 0)
        variants.append(changed)
        address, body, section = symbol_bytes(worker, CALLBACK, 2)
        offset = section[4] + address - section[3]
        for at, word in ((0, 0x0009), (2, 0xe001)):
            changed = list(worker); data = bytearray(worker[0])
            data[offset + at:offset + at + 2] = word.to_bytes(2, 'little')
            changed[0] = bytes(data); variants.append(changed)
        for variant in variants:
            try:
                inspect(variant, stage)
            except (ValueError, KeyError):
                rejected += 1
            else:
                raise ValueError('General diagnostic audit accepted a state/opcode/profile mutant')
    result['mutants_rejected'] = rejected
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('worker', type=Path)
    parser.add_argument('--stage', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    try:
        result = check(read_elf(args.worker), read_elf(args.stage) if args.stage else None,
                       self_test=args.self_test)
        require(result is not None, 'Worker is not the explicit elided General profile')
    except (OSError, ValueError, KeyError) as error:
        parser.exit(2, 'General diagnostic audit refused: ' + str(error) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
