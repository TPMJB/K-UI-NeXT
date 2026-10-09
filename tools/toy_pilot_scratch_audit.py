#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check authored native entry capture and scoped scratch in the linked image."""
import argparse
import copy
import json
from pathlib import Path
import struct

LOW = 0x8C004000
LIMIT = 0x8C007800


def audit_native_scratch(resident, worker):
    """Execute only authored integer code; original SDK calls are opaque.

    The small fail-closed decoder supports the instructions emitted by these
    two functions. An unknown instruction or memory access needs review.
    This checks compiled capability lifetime and map gates, not IRQ delivery
    or the original SDK's termination. Host tests cover the GD protocol.
    """
    payload, symbols = resident['payload'], resident['symbols']

    def half(address):
        offset = address - LOW
        if offset < 0 or offset + 2 > len(payload):
            raise ValueError('Scratch audit instruction outside linked resident')
        return struct.unpack_from('<H', payload, offset)[0]

    def word(address):
        return half(address) | half(address + 2) << 16

    def literal(address, register):
        opcode = half(address)
        if opcode & 0xFF00 != 0xD000 | register << 8:
            raise ValueError('Scratch audit expected reviewed literal load')
        return word(((address + 4) & ~3) + (opcode & 255) * 4)

    def find(begin, end, sequence):
        matches = [address for address in range(begin, end - 2 * len(sequence) + 1, 2)
                   if all(expected is None or half(address + index * 2) == expected
                          for index, expected in enumerate(sequence))]
        if len(matches) != 1:
            raise ValueError('Scratch audit lacks unique reviewed native frame capture')
        return matches[0]

    required = ('_kui_retail_gd_10f0_hook', '_kui_retail_menu_hook',
                '_kui_retail_hook_active', '_kui_retail_native_caller',
                '_kui_retail_hook_sr',
                '_kui_retail_resident_dispatch', '_kui_toy_pilot_base_dispatch',
                '_kui_toy_pilot_boot_control', '_kui_retail_hook_source',
                '_toy_scratch', '_toy_map', '_map_guest', '_service')
    if any(name not in symbols for name in required):
        raise ValueError('Scratch audit missing linked authored state or entry')
    start, end = symbols['_kui_retail_gd_10f0_hook'] + 2, symbols['_kui_retail_menu_hook']
    frame = (0x0102, 0x2F16, None, 0x212B, 0x410E, 0x4F22,
             0x2F86, 0x2F96, 0x2FA6, 0x2FB6, 0x2FC6, 0x2FD6, 0x2FE6)
    entry = find(start, end, frame)
    if entry != start or literal(entry + 4, 2) != 0x100000F0:
        raise ValueError('Native frame no longer masks before caller saves')
    claim = find(start, end, (0xE101, 0x2012, None, 0x2092, 0x5188, None, 0x2012))
    # Resolve the active pointer load before its read/claim, allowing only
    # the assembler's reviewed move of r15->r8 around that PC-relative load.
    active_loads = [address for address in range(entry + len(frame) * 2, claim, 2)
                    if half(address) & 0xFF00 == 0xD000 and
                    literal(address, 0) == symbols['_kui_retail_hook_active']]
    saved_frames = [address for address in range(entry + len(frame) * 2, claim, 2)
                    if half(address) == 0x68F3]
    if (len(active_loads) != 1 or len(saved_frames) != 1 or
            literal(claim + 4, 0) != symbols['_kui_retail_hook_source'] or
            literal(claim + 10, 0) != symbols['_kui_retail_hook_sr']):
        raise ValueError('Native caller capture is not under the resident claim')
    capture = find(start, end, (0x5187, None, 0x2012, 0x6183, 0x7124, 0x1011,
                                None, None, 0x400B, 0x0009))
    if (capture <= claim or literal(capture + 2, 0) != symbols['_kui_retail_native_caller'] or
            literal(capture + 12, 15) != 0x8C007CF0 or
            literal(capture + 14, 0) != symbols['_kui_retail_resident_dispatch']):
        raise ValueError('Original PR/SP capture must precede the low stack switch')
    bottom, top = (worker['symbols'].get(name, 0) for name in
                   ('__toy_pilot_stack_bottom', '__toy_pilot_stack_top'))
    if top - bottom != 8192 or top != worker['symbols'].get('__toy_pilot_worker_end'):
        raise ValueError('Scratch bound no longer equals actual linked stack top')
    bss_begin, bss_end = (symbols[name] for name in
                         ('__retail_resident_bss_begin', '__retail_resident_bss_end'))
    scratch = symbols['_toy_scratch']
    if scratch & 3 or not bss_begin <= scratch <= bss_end - 4 or bss_end > LIMIT:
        raise ValueError('Scratch capability must remain protected aligned low BSS')

    def execute(name, arguments, globals_, expect_call=None, capability=0):
        registers = [0] * 16
        registers[4:4 + len(arguments)] = arguments
        registers[8:15] = [0x13579000 + index for index in range(7)]
        registers[15] = 0x8C007CF0
        preserved = registers[8:16].copy()
        memory = dict(globals_)
        memory[scratch] = capability
        pc, pr, flag = symbols[name], 0xFFFF0000, 0
        calls, stores = [], []
        adapter, result = 0x8CFDF000, 0x89ABCDEF
        base = symbols['_kui_toy_pilot_base_dispatch']
        local_calls = {symbols[key] for key in ('_toy_scratch_clear',
                       '_kui_toy_pilot_scratch_capability') if key in symbols}

        def read(address):
            if address in memory:
                return memory[address]
            if LOW <= address <= LOW + len(payload) - 4:
                return word(address)
            raise ValueError('Scratch audit unreviewed data read: ' + hex(address))

        def write(address, value):
            if address != scratch and not 0x8C007840 <= address < 0x8C007D00:
                raise ValueError('Scratch audit unexpected memory write: ' + hex(address))
            memory[address] = value & 0xFFFFFFFF
            if address == scratch:
                stores.append(memory[address])

        def signed(value, bits):
            return value - (1 << bits) if value & (1 << (bits - 1)) else value

        def instruction(address, delay=False):
            nonlocal flag, pr
            opcode = half(address)
            n, m, low = opcode >> 8 & 15, opcode >> 4 & 15, opcode & 15
            next_pc = address + 2
            if opcode == 0x0009:
                pass
            elif opcode >> 12 == 0xE:
                registers[n] = signed(opcode & 255, 8) & 0xFFFFFFFF
            elif opcode >> 12 == 0xD:
                registers[n] = read(((address + 4) & ~3) + (opcode & 255) * 4)
            elif opcode >> 12 == 9:
                registers[n] = signed(half(address + 4 + (opcode & 255) * 2), 16) & 0xFFFFFFFF
            elif opcode >> 12 == 7:
                registers[n] = (registers[n] + signed(opcode & 255, 8)) & 0xFFFFFFFF
            elif opcode & 0xF0FF == 0x4001:
                flag = registers[n] & 1
                registers[n] >>= 1
            elif opcode >> 12 == 6 and low == 3:
                registers[n] = registers[m]
            elif opcode >> 12 == 6 and low in (2, 6):
                registers[n] = read(registers[m])
                if low == 6:
                    registers[m] = (registers[m] + 4) & 0xFFFFFFFF
            elif opcode >> 12 == 5:
                registers[n] = read(registers[m] + low * 4)
            elif opcode >> 12 == 1:
                write(registers[n] + low * 4, registers[m])
            elif opcode >> 12 == 2 and low in (2, 6):
                if low == 6:
                    registers[n] = (registers[n] - 4) & 0xFFFFFFFF
                write(registers[n], registers[m])
            elif opcode >> 12 == 2 and low in (8, 9, 11):
                if low == 8:
                    flag = int(not registers[n] & registers[m])
                elif low == 9:
                    registers[n] &= registers[m]
                else:
                    registers[n] |= registers[m]
            elif opcode >> 12 == 3 and low in (0, 2, 6, 8, 12):
                if low == 0:
                    flag = int(registers[n] == registers[m])
                elif low in (2, 6):
                    flag = int(registers[n] >= registers[m] if low == 2 else registers[n] > registers[m])
                else:
                    registers[n] = (registers[n] + registers[m] * (1 if low == 12 else -1)) & 0xFFFFFFFF
            elif opcode >> 12 == 6 and low == 10:
                value = registers[m] + flag
                registers[n], flag = -value & 0xFFFFFFFF, int(value != 0)
            elif opcode & 0xFF00 in (0x8800, 0xC800, 0xC900, 0xCB00):
                if opcode & 0xFF00 == 0x8800:
                    flag = int(registers[0] == signed(opcode & 255, 8) & 0xFFFFFFFF)
                elif opcode & 0xFF00 == 0xC800:
                    flag = int(not registers[0] & (opcode & 255))
                elif opcode & 0xFF00 == 0xC900:
                    registers[0] &= opcode & 255
                else:
                    registers[0] |= opcode & 255
            elif opcode == 0x4F22:
                registers[15] -= 4
                write(registers[15], pr)
            elif opcode == 0x4F26:
                pr = read(registers[15])
                registers[15] += 4
            elif opcode & 0xFF00 in (0x8900, 0x8B00, 0x8D00, 0x8F00):
                if delay:
                    raise ValueError('Scratch audit branch in delay slot')
                taken = bool(flag) == bool(opcode & 0x0200 == 0)
                if opcode & 0x0400:
                    instruction(address + 2, True)
                    next_pc += 2
                if taken:
                    next_pc = address + 4 + signed(opcode & 255, 8) * 2
            elif opcode >> 12 == 0xA or opcode & 0xF0FF in (0x400B, 0x402B) or opcode == 0x000B:
                if delay:
                    raise ValueError('Scratch audit transfer in delay slot')
                if opcode >> 12 == 0xA:
                    target = address + 4 + signed(opcode & 4095, 12) * 2
                elif opcode == 0x000B:
                    target = pr
                else:
                    target = registers[n]
                    if opcode & 0xF0FF == 0x400B:
                        pr = address + 4
                instruction(address + 2, True)
                if target in (adapter, base):
                    calls.append((target, memory[scratch], tuple(registers[4:8])))
                    if expect_call is None or memory[scratch] != expect_call:
                        raise ValueError('Compiled dispatcher exposes an incorrect scratch capability')
                    forwarded = ((symbols['_service'], arguments[0], arguments[1], arguments[3])
                                 if target == adapter else tuple(arguments))
                    if tuple(registers[4:8]) != forwarded:
                        raise ValueError('Scratch dispatcher changed forwarded GD arguments')
                    if target == adapter and read(registers[15]) != base:
                        raise ValueError('Scratch dispatcher changed its protected base callback')
                    registers[0] = result
                    registers[1:8] = [0xABCDEF00 + index for index in range(7)]
                    target = pr
                elif target == symbols['_map_guest']:
                    return None, registers[5]
                elif (target != 0xFFFF0000 and opcode & 0xF0FF == 0x400B and
                      target not in local_calls):
                    raise ValueError('Scratch dispatcher gained an unreviewed callee')
                if target == 0xFFFF0000:
                    return None, registers[0]
                next_pc = target
            else:
                raise ValueError('Scratch audit unsupported authored opcode: ' + hex(opcode))
            return next_pc, None

        for _ in range(1000):
            pc, returned = instruction(pc)
            if pc is None:
                if registers[8:16] != preserved:
                    raise ValueError('Scratch audit lost preserved integer ABI or stack')
                return returned, memory[scratch], calls, stores
        raise ValueError('Scratch audit exceeded bounded instruction count')

    control = symbols['_kui_toy_pilot_boot_control']
    caller = symbols['_kui_retail_native_caller']
    source = symbols['_kui_retail_hook_source']
    sp = top - 520
    globals_ = {source: 0, control + 8: 4, control + 40: top,
                control + 48: 0x8CFDF000, caller: 0x8C0BD374, caller + 4: sp}
    cases = []
    for function, command, pr, writing in ((1, 129, 0x8C0BD374, 1), (0, 36, 0x8C0BD57E, 0)):
        valid = dict(globals_)
        valid[caller] = pr
        variants = [('accepted', {}, function, command, sp, sp | writing),
                    ('lowest-owned-span', {caller + 4: bottom + 100}, function, command,
                     bottom + 100, (bottom + 100) | writing),
                    ('highest-owned-span', {caller + 4: top - 32}, function, command,
                     top - 32, (top - 32) | writing),
                    ('wrong-function', {}, 2, command, sp, 0),
                    ('wrong-pr', {caller: pr + 2}, function, command, sp, 0),
                    ('wrong-sp', {caller + 4: sp + 4}, function, command, sp, 0),
                    ('unaligned', {caller + 4: sp + 2}, function, command, sp + 2, 0),
                    ('guard-save-overlap', {caller + 4: bottom + 96}, function, command, bottom + 96, 0),
                    ('bridge-anchor', {caller + 4: top - 28}, function, command, top - 28, 0),
                    ('fallback', {control + 8: 0}, function, command, sp, 0),
                    ('nonreal', {}, function, command, sp, 0)]
        variants.append(('wrong-token' if function else 'wrong-command', {}, function,
                         command + 1, sp, sp | 1 if function else 0))
        for label, changes, current_function, current_command, param, expected in variants:
            memory = valid | changes
            returned, final, calls, stores = execute('_kui_retail_resident_dispatch',
                [current_command, param, 0xFFFFFFFF if label == 'nonreal' else 0,
                 current_function], memory,
                expect_call=expected, capability=0xDEADBEEF)
            if returned != 0x89ABCDEF or final or len(calls) != 1 or not stores or stores[0]:
                raise ValueError('Compiled scratch dispatch failed its set/call/clear lifetime')
            cases.append(label)
    map_cases = 0
    for capability in (0, sp, sp | 1):
        for address in (sp, sp - 4, sp + 4, bottom, top - 16):
            for length in (4, 16, 20):
                for writing in (0, 1):
                    expected = address if capability and length == 16 and address == sp and writing == capability & 1 else 0
                    actual, final, calls, _ = execute('_toy_map', [0, address, length, writing],
                        {}, capability=capability)
                    if actual != expected or final != capability or calls:
                        raise ValueError('Compiled scratch mapper changed exact span/direction admission')
                    map_cases += 1
    return {'original_caller_capture_before_low_stack': True, 'caller_saved_bytes': 36,
            'resident_claim_before_capture': True, 'capability_low_bss': f'0x{scratch:08x}',
            'compiled_dispatch_cases': len(cases), 'compiled_protected_map_cases': map_cases,
            'capability_zero_before_dispatch_and_after_return': True,
            'wrong_token_check_remains_mapped': True,
            'native_check_bytes': 16, 'native_req_stat_parameter_bytes': 16,
            'worker_guard_bytes': 64, 'bridge_anchor_bytes': 16,
            'scope': 'Authored linked entry/dispatch/map only; SDK callee opaque, IRQ delivery unproved'}


def mutation_test(resident, worker):
    """Reject removal of the capture, map gates, clear or trusted bindings."""
    audit_native_scratch(resident, worker)
    symbols, payload = resident['symbols'], resident['payload']
    begin, end = symbols['_kui_retail_gd_10f0_hook'] + 2, symbols['_kui_retail_menu_hook']
    changes = []
    for address in range(begin, end, 2):
        opcode = struct.unpack_from('<H', payload, address - LOW)[0]
        if opcode in (0x5187, 0x6183, 0x7124, 0x1011, 0x68F3):
            changes.append((address, 0x0009))
    map_begin = symbols['_toy_map']
    # These reviewed comparison constants belong to the small compiled map;
    # no broad scan of unrelated resident/game instructions is used.
    for address in range(map_begin, min(map_begin + 96, LOW + len(payload)), 2):
        if struct.unpack_from('<H', payload, address - LOW)[0] == 0x8810:
            changes.append((address, 0x8808))
            break
    if '_toy_scratch_clear' in symbols:
        clear = symbols['_toy_scratch_clear']
        for address in range(clear, clear + 12, 2):
            if struct.unpack_from('<H', payload, address - LOW)[0] == 0xE200:
                changes.append((address, 0xE201))
                break
    dispatch = symbols['_kui_retail_resident_dispatch']
    for address in range(dispatch, min(dispatch + 224, LOW + len(payload)), 2):
        if struct.unpack_from('<H', payload, address - LOW)[0] == 0x2812:
            changes.append((address, 0x0009))
    checked = 0
    for address, replacement in changes:
        broken = copy.copy(resident)
        blob = bytearray(payload)
        struct.pack_into('<H', blob, address - LOW, replacement)
        broken['payload'] = bytes(blob)
        try:
            audit_native_scratch(broken, worker)
        except ValueError:
            checked += 1
        else:
            raise ValueError('Scratch audit accepted a removed capture/map/lifetime gate')
    for name in ('_kui_retail_native_caller', '_toy_scratch'):
        broken = copy.copy(resident)
        broken['symbols'] = symbols | {name: symbols[name] + 4}
        try:
            audit_native_scratch(broken, worker)
        except ValueError:
            checked += 1
        else:
            raise ValueError('Scratch audit accepted a stale protected-state binding')
    return checked


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('resident_elf', type=Path)
    parser.add_argument('worker_elf', type=Path)
    parser.add_argument('--mutation-test', action='store_true')
    args = parser.parse_args()
    from check_loader_layout import inspect_elf
    from package_cdda_toy_pilot import inspect_worker
    linked = inspect_elf(args.resident_elf.read_bytes(), LOW, LIMIT)
    worker = inspect_worker(args.worker_elf.read_bytes())
    report = audit_native_scratch(linked, worker)
    if args.mutation_test:
        report['material_mutations_rejected'] = mutation_test(linked, worker)
    print(json.dumps(report, indent=2))
