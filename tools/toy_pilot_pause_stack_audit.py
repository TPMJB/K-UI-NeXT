#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit authored linked pause stack switching, without original game bytes."""
import argparse
import copy
import json
from pathlib import Path
import struct

WORKER_BASE = 0x8CFD0000
WORKER_END = 0x8CFE0000
OWNER_HEADER = WORKER_BASE - 32
GAME_BEGIN = 0x8C008000
GAME_END = 0x8D000000
NATIVE_CALL_BYTES = 128  # Independently reviewed SDK68 + CHECK24 + GD entry36.


def audit_pause_stack(worker):
    """Fail if the actual helper/bridge loses the reviewed native scratch ABI.

    This deliberately admits one reviewed instruction sequence. Changes to
    its stack switching require a new review rather than a permissive decoder.
    The native SDK call is an opaque callee with its existing integer ABI;
    its instructions and unconditional termination are not certified here.
    """
    payload = worker['payload']
    symbols, sizes = worker['symbols'], worker['symbol_sizes']

    def data(address, length):
        offset = address - WORKER_BASE
        if offset < 0 or length < 0 or offset + length > len(payload):
            raise ValueError('Pause stack audit address outside linked worker')
        return payload[offset:offset + length]

    def half(address):
        return struct.unpack('<H', data(address, 2))[0]

    def word(address):
        return struct.unpack('<I', data(address, 4))[0]

    def literal(address, register):
        opcode = half(address)
        if (opcode & 0xFF00) != (0xD000 | register << 8):
            raise ValueError('Pause stack audit expected exact PC-relative load')
        target = ((address + 4) & ~3) + (opcode & 255) * 4
        return target, word(target)

    def code(name):
        begin, size = symbols.get(name, 0), sizes.get(name, 0)
        if not begin or size < 4 or size % 2:
            raise ValueError('Pause stack audit missing linked function: ' + name)
        data(begin, size)
        return begin, size

    pump, pump_size = code('_kui_toy_pilot_native_pump')
    bridge, bridge_size = code('_kui_toy_pilot_worker_bridge')
    top = symbols.get('__toy_pilot_stack_top', 0)
    bottom = symbols.get('__toy_pilot_stack_bottom', 0)
    if not WORKER_BASE <= bottom < top <= WORKER_END or top - bottom != 8192:
        raise ValueError('Pause stack audit lacks the guarded private stack')

    # PR/r8 live on the private stack. r8 retains private SP across the
    # native call; its own scratch and GD result are on the borrowed game SP.
    expected = (0x4F22, 0x2F86, 0x68F3, None, 0x6F02, None,
                0x400B, 0x0009, 0x6F83, 0x68F6, 0x4F26, 0x000B, 0x0009)
    for index, opcode in enumerate(expected):
        if opcode is not None and half(pump + index * 2) != opcode:
            raise ValueError('Pause native helper changed stack/register contract')
    anchor_literal, anchor = literal(pump + 6, 0)
    target_literal, target = literal(pump + 10, 0)
    if anchor != top - 16 or target != 0x8C0B1704:
        raise ValueError('Pause native helper changed bridge anchor or SDK target')
    tail = (pump + 26 + 3) & ~3
    if (anchor_literal != tail or target_literal != tail + 4 or
            pump + pump_size != tail + 8):
        raise ValueError('Pause native helper gained unreviewed trailing code')

    # The shared bridge creates precisely the anchor the helper consumes.
    # Saved SR, PR and r8..r14 take 36 bytes on the original caller stack;
    # the destination anchor remains claimed on the private stack.
    prefix = (0x2F16, 0x4F22, 0x2F86, 0x2F96, 0x2FA6, 0x2FB6,
              0x2FC6, 0x2FD6, 0x2FE6, 0x68F3)
    sequence = struct.pack('<10H', *prefix)
    body = data(bridge, bridge_size)
    locations = [index for index in range(0, len(body) - len(sequence) + 1, 2)
                 if body[index:index + len(sequence)] == sequence]
    if len(locations) != 1:
        raise ValueError('Pause bridge lacks its unique preserved caller frame')
    install = bridge + locations[0] + len(sequence)
    _, loaded_top = literal(install, 15)
    if loaded_top != top:
        raise ValueError('Pause bridge selects a different private stack')
    expected_anchor = (0x7FF0, 0x2F82, 0x400B, 0x0009, 0x68F2, 0x6F83)
    if tuple(half(install + 2 + index * 2) for index in range(6)) != expected_anchor:
        raise ValueError('Pause bridge changed saved-SP anchor or restoration')
    expected_pops = (0x6EF6, 0x6DF6, 0x6CF6, 0x6BF6,
                     0x6AF6, 0x69F6, 0x68F6, 0x4F26)
    if tuple(half(install + 14 + index * 2) for index in range(8)) != expected_pops:
        raise ValueError('Pause bridge changed preserved integer/PR restoration')

    # Exercise the actual reviewed helper's stack transitions and the native
    # CHECK scratch position under ordinary, unscoped map admission. Borrowing
    # the suspended game frame does not need the narrow native IRQ capability.
    private_sp, game_sp = top - 64, 0x8C0BEDB4
    memory = {anchor: game_sp}
    saved_r8, saved_pr = 0x87654321, 0x8C012346
    registers = {'sp': private_sp, 'r8': saved_r8, 'pr': saved_pr}
    for register in ('pr', 'r8'):
        registers['sp'] -= 4
        memory[registers['sp']] = registers[register]
    registers['r8'] = registers['sp']
    registers['sp'] = memory[anchor]

    def mapped(address, length):
        return (address >= GAME_BEGIN and address + length <= GAME_END and
                not (address < WORKER_END and address + length > OWNER_HEADER))

    check_scratch = registers['sp'] - 68 - 24
    if (not mapped(check_scratch, 16) or
            not mapped(registers['sp'] - NATIVE_CALL_BYTES, NATIVE_CALL_BYTES) or
            mapped(private_sp - 68 - 24, 16)):
        raise ValueError('Pause helper does not preserve protected CHECK scratch mapping')
    # The admitted native ABI preserves r8, allowing private restoration.
    registers['pr'] = pump + 16
    registers['sp'] = registers['r8']
    for register in ('r8', 'pr'):
        registers[register] = memory[registers['sp']]
        registers['sp'] += 4
    if registers != {'sp': private_sp, 'r8': saved_r8, 'pr': saved_pr}:
        raise ValueError('Pause helper fails private SP/r8/PR restoration')

    return {'helper': f'0x{pump:08x}', 'helper_bytes': pump_size,
            'bridge_anchor': f'0x{anchor:08x}', 'bridge_anchor_bytes': 16,
            'bridge_original_caller_saved_bytes': 36,
            'helper_private_saved_bytes': 8,
            'native_scratch_reviewed_bytes': NATIVE_CALL_BYTES,
            'native_check_scratch_bytes': 16,
            'unscoped_private_stack_rejected_as_gd_scratch': True,
            'borrowed_game_stack_maps_gd_scratch': True,
            'private_sp_r8_pr_restored': True,
            'native_target': '0x8c0b1704',
            'scope': 'Exact linked authored helper and bridge; original SDK reviewed separately'}


def mutation_test(worker):
    """Reject material compiled-helper and anchor regressions."""
    checked = 0
    begin = worker['symbols']['_kui_toy_pilot_native_pump']
    for offset in (0, 2, 4, 8, 12, 14, 16, 18, 20, 22, 24):
        broken = copy.copy(worker)
        blob = bytearray(worker['payload'])
        struct.pack_into('<H', blob, begin - WORKER_BASE + offset, 0x0009)
        if blob == worker['payload']:
            continue
        broken['payload'] = bytes(blob)
        try:
            audit_pause_stack(broken)
        except ValueError:
            checked += 1
        else:
            raise ValueError('Pause stack audit accepted a corrupted helper')
    for instruction_offset in (6, 10):
        load = begin + instruction_offset
        opcode = struct.unpack_from('<H', worker['payload'], load - WORKER_BASE)[0]
        address = ((load + 4) & ~3) + (opcode & 255) * 4
        broken = copy.copy(worker)
        blob = bytearray(worker['payload'])
        struct.pack_into('<I', blob, address - WORKER_BASE, 0)
        broken['payload'] = bytes(blob)
        try:
            audit_pause_stack(broken)
        except ValueError:
            checked += 1
        else:
            raise ValueError('Pause stack audit accepted a corrupted anchor/target')
    broken = copy.copy(worker)
    broken['symbols'] = dict(worker['symbols'])
    broken['symbols']['__toy_pilot_stack_top'] += 32
    try:
        audit_pause_stack(broken)
    except ValueError:
        checked += 1
    else:
        raise ValueError('Pause stack audit accepted a changed anchor')
    return checked


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('worker_elf', type=Path)
    parser.add_argument('--mutation-test', action='store_true')
    args = parser.parse_args()
    from package_cdda_toy_pilot import inspect_worker
    linked = inspect_worker(args.worker_elf.read_bytes())
    report = audit_pause_stack(linked)
    if args.mutation_test:
        report['corrupted_helper_cases_rejected'] = mutation_test(linked)
    print(json.dumps(report, indent=2))
