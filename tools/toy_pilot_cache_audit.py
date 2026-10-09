#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fail-closed linked audit for the isolated Toy P2/cache comparison.

This certifies authored layout, bounded frame publication and scratch gates;
it does not certify native SDK internals, interrupt delivery or audio output.
"""
import argparse
import ast
import copy
import inspect
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

from check_loader_layout import inspect_elf
from check_retail_instructions import audit as instruction_audit
from package_cdda_toy_pilot import linked_halfwords, linked_symbol_sizes

ROOT = Path(__file__).resolve().parents[1]
LOW, LOW_LIMIT = 0x8C004000, 0x8C007800
HIGH, HIGH_LIMIT = 0x8CFD0000, 0x8CFE0000
OFFSET = 0x20000000
BEGIN = (0x0102, 0xE20F, 0x4208, 0x4208, 0x221B, 0x420E)


def physical(address):
    return address - OFFSET if 0xAC000000 <= address < 0xAD000000 else address


class Linked:
    def __init__(self, image, base, disassembly=''):
        self.image, self.base = image, base
        self.payload, self.symbols = image['payload'], image['symbols']
        self.rows, self.literals = linked_halfwords(disassembly)

    def data(self, address, count):
        at = physical(address) - self.base
        if at < 0 or count < 0 or at + count > len(self.payload):
            raise ValueError('Cache audit reads outside linked initialized bytes')
        return self.payload[at:at + count]

    def half(self, address):
        return struct.unpack('<H', self.data(address, 2))[0]

    def word(self, address):
        return struct.unpack('<I', self.data(address, 4))[0]

    def literal(self, address, register, width=4):
        opcode = self.half(address)
        if opcode & 0xFF00 != (0xD000 if width == 4 else 0x9000) | register << 8:
            raise ValueError('Cache audit expected exact PC-relative load')
        target = (((address + 4) & ~3) + (opcode & 255) * 4 if width == 4
                  else address + 4 + (opcode & 255) * 2)
        return target, int.from_bytes(self.data(target, width), 'little')

    def bounds(self, name):
        begin = self.symbols.get(name, 0)
        size = self.image.get('symbol_sizes', {}).get(name, 0)
        if not size:
            following = [value for key, value in self.symbols.items()
                         if begin < value < self.base + len(self.payload) and
                         not key.startswith('__toy_') and not key.startswith('__retail_')]
            size = min(following, default=self.base + len(self.payload)) - begin
        if not self.base <= begin < begin + size <= self.base + len(self.payload):
            raise ValueError('Cache audit missing bounded linked function: ' + name)
        return begin, begin + size

    def sequence(self, address, expected):
        if any(value is not None and self.half(address + index * 2) != value
               for index, value in enumerate(expected)):
            raise ValueError('Cache audit changed reviewed sequence at ' + hex(address))

    def find(self, name, expected):
        begin, end = self.bounds(name)
        hits = [at for at in range(begin, end - 2 * len(expected) + 1, 2)
                if all(opcode is None or self.half(at + i * 2) == opcode
                       for i, opcode in enumerate(expected))]
        if len(hits) != 1:
            raise ValueError('Cache audit requires unique reviewed sequence in ' + name)
        return hits[0]


def publication_lines(start, size):
    """Reference coverage, including a final cache line at every SP residue."""
    if start & 3 or not 0 < size <= 64 or size & 3:
        raise ValueError('Unreviewed publication span/alignment')
    begin, end = physical(start), physical(start + size)
    if not 0x8C000000 <= begin < end <= 0x8D000000:
        raise ValueError('Publication outside admitted main RAM')
    return tuple(range(begin & ~31, end, 32))


def audit_frame_helper(image, base, name, disassembly=''):
    code = Linked(image, base, disassembly)
    begin, end = code.bounds(name)
    code.sequence(begin, (None, 0x2139, 0x2239, 0xE3E0, 0x2139,
                          0x01A3, 0x7120, 0x3122, 0x8BFB, 0x000B, 0x0009))
    target, mask = code.literal(begin, 3)
    if mask != 0xDFFFFFFF or target != (begin + 22 + 3) & ~3 or end != target + 4:
        raise ValueError('Frame helper must canonicalize P2 and end after its alias literal')
    # The exact reviewed machine code below has no memory writes, SR/PR
    # changes or r0/argument/callee-saved clobbers. Execute the actual opcodes
    # for all word-aligned residues; an OCBP access itself is recorded.
    cases = 0
    for area in (0x8C010000, 0xAC010000):
        for residue in range(0, 32, 4):
            for size in (4, 8, 12, 16, 20, 24, 36, 52):
                r = [0x12340000 + i for i in range(16)]
                start = area + residue
                r[1], r[2] = start, start + size
                saved = r.copy()
                pc, flag, observed = begin, 0, []
                for _ in range(64):
                    opcode = code.half(pc)
                    if opcode & 0xFF00 == 0xD300:
                        r[3] = code.literal(pc, 3)[1]
                    elif opcode in (0x2139, 0x2239):
                        r[opcode >> 8 & 15] &= r[3]
                    elif opcode == 0xE3E0:
                        r[3] = 0xFFFFFFE0
                    elif opcode == 0x01A3:
                        if r[1] & 31 or not 0x8C000000 <= r[1] < 0x8D000000:
                            raise ValueError('Frame helper issued noncanonical OCBP')
                        observed.append(r[1])
                    elif opcode == 0x7120:
                        r[1] += 32
                    elif opcode == 0x3122:
                        flag = int(r[1] >= r[2])
                    elif opcode == 0x8BFB:
                        if not flag:
                            pc = pc + 4 - 10
                            continue
                    elif opcode == 0x000B:
                        if code.half(pc + 2) != 9:
                            raise ValueError('Frame helper return delay changed')
                        break
                    else:
                        raise ValueError('Frame helper unreviewed opcode')
                    pc += 2
                else:
                    raise ValueError('Frame helper lost bounded termination')
                if (tuple(observed) != publication_lines(start, size) or
                        any(r[i] != saved[i] for i in (0, *range(4, 16)))):
                    raise ValueError('Frame helper misses live bytes or clobbers saved ABI')
                cases += 1
    return {'helper': hex(begin), 'compiled_alignment_cases': cases,
            'canonical_P1_ocbp': True, 'r0_arguments_and_callee_saved_preserved': True,
            'scope': 'Exact linked authored helper; cache hardware behavior is not simulated'}


def audit_hook_publications(image, base, disassembly, helper, expected):
    code = Linked(image, base, disassembly)
    destination = code.symbols.get(helper, 0)
    if not destination:
        raise ValueError('Missing frame publication helper')
    result = {}
    for name, spans in expected.items():
        begin, end = code.bounds(name)
        calls = []
        for at in range(begin, end, 2):
            opcode = code.half(at)
            indirect = opcode == 0x430B and code.half(at - 4) & 0xFF00 == 0xD300
            if indirect:
                if code.literal(at - 4, 3)[1] != destination:
                    continue
                prefix = at - 8
            elif opcode >> 12 == 0xB:
                delta = opcode & 4095
                delta -= 4096 if delta & 2048 else 0
                if at + 4 + delta * 2 != destination:
                    continue
                prefix = at - 6
            else:
                continue
            code.sequence(prefix, (0x61F3, 0x62F3, None))
            if code.half(at + 2) != 9:
                raise ValueError('Frame publication call delay changed')
            size = code.half(prefix + (6 if indirect else 4))
            if size & 0xFF00 != 0x7200 or not 0 < size & 255 <= 64:
                raise ValueError('Frame publication lost its exact live-SP span')
            calls.append((at, size & 255))
        if tuple(size for _, size in calls) != tuple(spans):
            raise ValueError('Missing/changed frame publications in ' + name)
        result[name] = [hex(at) for at, _ in calls]
        # All new frame writes in ordinary wrappers must begin with an exact
        # SR/IMASK guard. The bridge and heap have separately reviewed guards.
        if name not in ('_kui_toy_pilot_worker_bridge', '_kui_toy_pilot_heap_hook',
                        '_kui_retail_gd_10f0_hook'):
            code.sequence(begin, BEGIN)
            for at, _ in calls:
                # HOLD/END publish the temporary exact SR immediately below
                # the authored save; END pops it before native entry.
                if code.half(at - 8) != 0x2F16:
                    raise ValueError('Frame publication lacks its exact SR save')
    return result


def exact_BL_mask_prefix(code, begin):
    """mrelax may move its independent literal before STC; admit both orders."""
    if code.half(begin) & 0xFF00 == 0xD200:
        code.sequence(begin, (None, 0x0102, 0x221B, 0x420E, 0x2F16))
        load = begin
    else:
        code.sequence(begin, (0x0102, None, 0x221B, 0x420E, 0x2F16))
        load = begin + 2
    if code.literal(load, 2)[1] != 0x100000F0:
        raise ValueError('Inherited frame lacks exact BL+IMASK before first stack write')
    return load


def audit_masked_low_frames(resident, disassembly):
    """The only unpublished inherited frames are dead before exact SR restore."""
    code = Linked(resident, LOW, disassembly)
    name = '_kui_retail_gd_10f0_hook'
    begin = code.symbols[name] + 2
    exact_BL_mask_prefix(code, begin)
    code.sequence(begin + 10, (0x4F22, 0x2F86, 0x2F96, 0x2FA6,
                               0x2FB6, 0x2FC6, 0x2FD6, 0x2FE6))
    restored = code.find(name, (0x6F83, 0x6EF6, 0x6DF6, 0x6CF6, 0x6BF6,
                                0x6AF6, 0x69F6, 0x68F6, 0x4F26, 0x61F6,
                                0x410E, 0x000B, 0x0009))
    writes_sr = [at for at, op, _, _ in code.rows if begin <= at <= restored + 24
                 and at not in code.literals and (op & 0xF0FF == 0x400E or op & 0xF0FF == 0x4007)]
    if writes_sr != [begin + 6, restored + 20]:
        raise ValueError('Low GD frame restored/unmasked SR while saves remain live')
    heap = '_kui_toy_pilot_heap_hook'
    entry = code.symbols[heap]
    exact_BL_mask_prefix(code, entry)
    native = code.find(heap, (0x4F07, None, 0x430B, 0x0009, 0x0902,
                              None, 0x2A9B, 0x4A0E, 0x4F02, 0x4F12, 0x2F96))
    if (code.literal(native + 2, 3)[1] != 0x8C0B3E10 or
            code.literal(native + 10, 10)[1] != 0x100000F0):
        raise ValueError('Heap guard lost exact native target or post-native first-store mask')
    return {'GD_saved_bytes': 36, 'heap_initial_saved_bytes': 4,
            'BL_IMASK_held_until_frame_dead': True,
            'native_heap_SP_and_SR_restored_before_call': True,
            'scope': 'SHARED0 only; reachable dispatch callees separately constrained'}


def audit_heap_saved_state(resident, disassembly):
    """Exact original native result frame, protected before installer entry."""
    code = Linked(resident, LOW, disassembly)
    name = '_kui_toy_pilot_heap_hook'
    capture = code.find(name, (0x0902, None, 0x2A9B, 0x4A0E,
        0x4F02, 0x4F12, 0x2F96, 0x2F06, 0x2F16, 0x2F26,
        0x2F36, 0x2F46, 0x2F56, 0x2F66, 0x2F76, 0x2F86,
        None, 0x501B, 0x2F06, 0x61F3, 0x62F3, None, 0x7234,
        0x430B, 0x0009, None, 0x6912, 0x5A11, 0x68F3))
    saved = code.symbols.get('_kui_toy_pilot_heap_saved_r9', 0)
    if (not code.symbols['__retail_resident_bss_begin'] <= saved or
            saved + 8 > code.symbols['__retail_resident_bss_end'] or
            code.literal(capture + 2, 10)[1] != 0x100000F0 or
            code.literal(capture + 32, 1)[1] != code.symbols['_kui_toy_pilot_boot_control'] or
            code.literal(capture + 42, 3)[1] != code.symbols['_kui_toy_pilot_frame_publish_retail'] or
            code.literal(capture + 50, 1)[1] != saved):
        raise ValueError('Heap result frame lost exact P2 saved registers/control/publication')
    prior = code.find(name, (None, 0x2192, 0x11A1, 0x4F07, None, 0x430B, 0x0009))
    if (code.literal(prior, 1)[1] != saved or
            code.literal(prior + 8, 3)[1] != 0x8C0B3E10 or prior >= capture):
        raise ValueError('Heap original r9/r10 or native entry SP/SR preservation changed')
    restored = code.find(name, (0x6F83, 0x4F26, 0x68F6, 0x67F6, 0x66F6,
        0x65F6, 0x64F6, 0x63F6, 0x62F6, 0x61F6, 0x60F6,
        0x4F07, 0x4F16, 0x4F06, 0x000B, 0x0009))
    code.sequence(restored - 6, (0x5F21, 0x400B, 0x0009))
    if restored <= capture + 56:
        raise ValueError('Heap installer precedes published original result frame')
    return {'published_bytes': 52, 'exact_native_SR_including_T_saved': True,
            'original_PR_r0_to_r8_MAC_restoration': True,
            'original_r9_r10_restored_from_P2_before_installer': True,
            'saved_r9_r10': hex(saved)}


def audit_native_metadata(worker, disassembly):
    """Require actual OCBP after each native SH metadata commit."""
    code = Linked(worker, HIGH, disassembly)
    begin, _ = code.bounds('_begin')
    code.sequence(begin, (0x0002, 0x2402, 0xCBF0, 0x400E))
    bus = '_kui_toy_pilot_bus_publish'
    producer = code.find(bus, (None, 0x21A1, 0x71F4, 0x01A3,
        0x61C2, 0xE2FF, 0x2912, None, 0x2122, None,
        0x6122, 0x7101, 0x2212, None, 0x01A3))
    for offset, register, value in ((0, 1, 0x8C112B0C), (14, 1, 0xA080B400),
                                     (18, 2, 0x8C0A8924), (26, 1, 0x8C0A8920)):
        if code.literal(producer + offset, register)[1] != value:
            raise ValueError('Native queue metadata publication address/order changed')
    begin_load = code.find(bus, (None, 0x7FF0, 0x64F3, 0x400B, 0x7404))
    if code.literal(begin_load, 0)[1] != begin or begin_load >= producer:
        raise ValueError('Native queue publication is outside its exact masked transaction')
    code.find(bus, (0x51F1, 0x410E, 0x6083, 0x7F10, 0x4F26))
    lease = '_kui_toy_pilot_lease_allocate'
    active = code.find(lease, (None, 0x1204, 0x2382, 0xE701,
        0x1273, 0x071A, 0xE6E0, 0x2269, 0x7714, 0x3726,
        0x8903, 0x03A3, 0x2AC2, None, 0x0009, 0x02A3, None, 0x7220))
    if (code.literal(active, 3)[1] != 0x8C10E560 or
            code.half(active + 26) & 0xF000 != 0xA000 or
            code.half(active + 32) & 0xF000 != 0xA000):
        raise ValueError('Native heap metadata lost exact active-last bounded publication')
    restore = code.find(lease, (0x490E, 0x6013, 0x7F1C, 0x4F26))
    mask = code.find(lease, (None, 0x209B, 0x400E, None))
    if code.literal(mask, 0, 2)[1] != 0xF0 or mask >= active:
        raise ValueError('Native heap metadata commit lacks its IMASK transaction')
    # The loop must revisit CMP with r2 advanced exactly one cache line.
    delta = code.half(active + 32) & 4095
    delta -= 4096 if delta & 2048 else 0
    if active + 36 + delta * 2 != active + 18:
        raise ValueError('Native heap record publication skips bounded cache-line loop')
    delta = code.half(active + 26) & 4095
    delta -= 4096 if delta & 2048 else 0
    if code.half(active + 30 + delta * 2) != 0x490E:
        raise ValueError('Native heap metadata publication does not precede exact SR restore')
    return {'queue_producer_line': '0x8c112b00', 'queue_commands_line': '0x8c0a8920',
            'allocator_manager_line': '0x8c10e560', 'allocator_record_bytes': 20,
            'active_flag_last_before_bounded_record_OCBP': True,
            'metadata_published_under_IMASK_before_native_return': True,
            'scope': 'Authored SH-RAM writes only; original SDK internals are not modeled'}


def audit_masked_hold_scope(worker, disassembly):
    """All inherited-stack HOLD callees form this closed authored graph."""
    code = Linked(worker, HIGH, disassembly)
    graph = {
        '_kui_toy_pilot_worker_revoke': {'_data_blocked_close'},
        '_kui_toy_pilot_worker_shutdown': {'_kui_toy_pilot_worker_revoke'},
        '_kui_toy_pilot_worker_allstop': {'_kui_toy_pilot_worker_revoke'},
        '_data_blocked_close': {'_mask_begin', '_data_blocked_close_masked.part.0'},
        '_mask_begin': set(), '_data_blocked_close_masked.part.0': set()}
    addresses = {code.symbols[name]: name for name in graph}
    result = {}
    for name, allowed in graph.items():
        start, end = code.bounds(name)
        rows = [(at, code.half(at), mnemonic, operands) for at, _, mnemonic, operands in code.rows
                if start <= at < end and at not in code.literals]
        found = set()
        if not any(mnemonic == 'rts' for _, _, mnemonic, _ in rows):
            raise ValueError('Masked HOLD callee lacks its authored return')
        for index, (at, op, mnemonic, operands) in enumerate(rows):
            if op >> 12 == 0xD and code.literal(at, op >> 8 & 15)[1] == 0xFF00001C:
                raise ValueError('Masked HOLD graph gained a CCR escape')
            if mnemonic in ('bsr', 'bsrf', 'jmp', 'jmp/n', 'braf', 'rte', 'trapa', 'jsr/n'):
                raise ValueError('Masked HOLD graph gained unreviewed control transfer')
            if mnemonic in ('bra', 'bt', 'bf', 'bt.s', 'bf.s'):
                target = re.match(r'^([0-9a-f]+)\s', operands)
                if not target or not start <= int(target[1], 16) < end or int(target[1], 16) in code.literals:
                    raise ValueError('Masked HOLD graph gained external branch')
            if op & 0xF0FF != 0x400B:
                continue
            register = op >> 8 & 15
            origin = None
            for prior_at, prior_op, operation, arguments in reversed(rows[max(0, index - 8):index]):
                if operation in ('bra', 'bt', 'bf', 'bt.s', 'bf.s', 'rts', 'jsr'):
                    break
                arguments = arguments.split('!', 1)[0].strip()
                if not re.search(r'(?:^|,)\s*r' + str(register) + r'$', arguments):
                    continue
                if prior_op & 0xFF00 == 0xD000 | register << 8:
                    origin = addresses.get(code.literal(prior_at, register)[1])
                break
            if origin not in allowed:
                raise ValueError('Masked HOLD callee gained native/unknown call origin')
            found.add(origin)
        if found != allowed:
            raise ValueError('Masked HOLD graph lost its mandatory revoke/close callees')
        result[name] = sorted(found)
    for hook, target in (('_kui_toy_pilot_am_init_hook', '_kui_toy_pilot_worker_revoke'),
            ('_kui_toy_pilot_shutdown_hook', '_kui_toy_pilot_worker_shutdown'),
            ('_kui_toy_pilot_allstop_hook', '_kui_toy_pilot_worker_allstop')):
        hold = code.find(hook, (None, 0x400B, 0x0009, 0x4F07))
        if code.literal(hold, 0)[1] != code.symbols[target]:
            raise ValueError('HOLD wrapper lost exact masked authored target')
    mask = code.symbols['_mask_begin']
    code.sequence(mask, (0x0102, None, 0x201B, 0x400E, 0x000B, 0x6013))
    if code.literal(mask + 2, 0, 2)[1] != 0xF0:
        raise ValueError('Nested HOLD mask did not preserve inherited IMASK')
    close = code.symbols['_data_blocked_close']
    code.find('_data_blocked_close', (0x6803, None, 0x410B, 0x0009, 0x480E))
    return {'compiled_closed_callees': result, 'native_SDK_or_CCR_targets': False,
            'temporary_C_frames_die_before_exact_wrapper_SR_pop': True,
            'scope': 'Exact SHARED0 HOLD graph; caller frames are separately published'}


def audit_stage_alias_publication(stage, disassembly):
    """Retain whole-RAM handoff purge and pre-purge private worker aliases."""
    code = Linked(stage, 0x8CE00000, disassembly)
    resume = code.symbols['_kui_retail_game_resume']
    code.sequence(resume, (0x68F3, None, None, 0x6902, None, None,
        0x00A3, 0x7020, 0x3012, 0x8BFB, None, None, 0x2012))
    for offset, register, value in ((4, 0, 0xFF00001C), (8, 0, 0x8C000000),
                                     (10, 1, 0x8D000000), (20, 0, 0xFF00001C),
                                     (22, 1, 0x808)):
        if code.literal(resume + offset, register)[1] != value:
            raise ValueError('Stage handoff no longer purges complete physical RAM before CCR reset')
    install = '_kui_toy_pilot_stage_install'
    purge = code.find(install, (None, 0x558D, 0x5493, 0x4A0B, 0x0009,
        None, 0xE500, 0x5489, 0x568A, 0x400B, 0x3648,
        None, 0x66B3, 0x5493, None, None, 0x400B, 0x342C))
    for offset, register, target in ((0, 10, '_toy_publish'), (10, 0, '_memset'),
                                     (22, 0, '_memcmp'),
                                     (30, 5, '__toy_pilot_worker_blob_start')):
        if code.literal(purge + offset, register)[1] != code.symbols[target]:
            raise ValueError('Private worker pre-purge/uncached verification binding changed')
    if code.literal(purge + 28, 2)[1] != OFFSET:
        raise ValueError('Private worker verification refills its cached writable alias')
    # r8 is the admitted exports object; offset52 is its physical worker_end,
    # including all P2 BSS and stacks. The same object supplies BSS36..40.
    code.find(install, (0x68F3, None, 0x4C0B, 0x7834))
    return {'handoff_physical_RAM': ['0x8c000000', '0x8d000000'],
            'whole_RAM_OCBP_before_CCR_reset': True,
            'worker_pre_purge_end': 'admitted exports.worker_end (physical offset52)',
            'private_BSS_zero_after_pre_purge': True,
            'worker_byte_verification_uses_P2': True}


def normalized_physical_view(image, base, disassembly):
    """Only a compatibility view for old *scope* checks, never ELF admission."""
    code = Linked(image, base, disassembly)
    result = copy.copy(image)
    result['symbols'] = {name: physical(value) for name, value in code.symbols.items()}
    blob = bytearray(code.payload)
    for address, opcode, mnemonic, _ in code.rows:
        if address in code.literals or mnemonic != 'mov.l' or opcode >> 12 != 0xD:
            continue
        target, value = code.literal(address, opcode >> 8 & 15)
        if physical(value) != value:
            struct.pack_into('<I', blob, target - base, physical(value))
    result['payload'] = bytes(blob)
    return result


def audit_private_native_scratch(resident, worker, resident_disassembly=''):
    """Run old fail-closed SH interpreter on actual P2 dispatcher/map code.

    Only interpreter fixture addresses (the low private stack) change. No
    production opcode, pointer literal, alias gate or symbol is projected.
    CHECK/REQ_STAT enter as P2; the core supplies canonical P1 to toy_map.
    """
    import toy_pilot_scratch_audit as prior
    actual = Linked(resident, LOW, resident_disassembly)
    begin = actual.symbols['_kui_retail_gd_10f0_hook'] + 2
    exact_BL_mask_prefix(actual, begin)
    actual.sequence(begin + 10, (0x4F22, 0x2F86, 0x2F96, 0x2FA6,
                                 0x2FB6, 0x2FC6, 0x2FD6, 0x2FE6))
    symbols = actual.symbols
    required = ('_kui_retail_native_caller', '_kui_retail_hook_source',
                '_kui_toy_pilot_boot_control', '_toy_scratch', '_toy_map',
                '_kui_retail_resident_dispatch', '_kui_toy_pilot_base_dispatch', '_service')
    if any(name not in symbols for name in required):
        raise ValueError('Missing compiled P2 scratch state/entry')
    capture = actual.find('_kui_retail_gd_10f0_hook',
        (0x5187, None, 0x2012, 0x6183, 0x7124, 0x1011, None, None, 0x400B, 0x0009))
    if (actual.literal(capture + 2, 0)[1] != symbols['_kui_retail_native_caller'] or
            actual.literal(capture + 12, 15)[1] != 0xAC007CF0 or
            actual.literal(capture + 14, 0)[1] != symbols['_kui_retail_resident_dispatch']):
        raise ValueError('P2 GD entry lost original PR/SP capture before stack switch')
    scratch = symbols['_toy_scratch']
    if not symbols['__retail_resident_bss_begin'] <= scratch < symbols['__retail_resident_bss_end'] or scratch & 3:
        raise ValueError('Scratch capability is not protected P2 BSS')
    # Reuse the existing deliberately narrow interpreter implementation,
    # extracting only execute(). Transform only its three hosted stack
    # constants. This keeps decoder support/failures consistent with the
    # ordinary compiled scratch audit without touching that file.
    tree = ast.parse(inspect.getsource(prior.audit_native_scratch))
    nested = next(node for node in tree.body[0].body
                  if isinstance(node, ast.FunctionDef) and node.name == 'execute')
    replacements = {0x8C007CF0: 0xAC007CF0, 0x8C007840: 0xAC007840,
                    0x8C007D00: 0xAC007D00}
    class FixtureStack(ast.NodeTransformer):
        def visit_Constant(self, node):
            if isinstance(node.value, int) and node.value in replacements:
                return ast.copy_location(ast.Constant(replacements[node.value]), node)
            return node
        def visit_If(self, node):
            self.generic_visit(node)
            # GCC uses XOR #1,R0 for the new alias admission boolean. Add
            # that one integer opcode to the inherited decoder, leaving
            # every unknown instruction fail-closed.
            if (isinstance(node.test, ast.Compare) and
                    len(node.test.comparators) == 1 and
                    isinstance(node.test.comparators[0], ast.Tuple) and
                    [getattr(value, 'value', None) for value in node.test.comparators[0].elts]
                    == [0x8800, 0xC800, 0xC900, 0xCB00]):
                node.test.comparators[0].elts.append(ast.Constant(0xCA00))
                last = node.body[0]
                while last.orelse and isinstance(last.orelse[0], ast.If):
                    last = last.orelse[0]
                xor = ast.parse('if opcode & 0xFF00 == 0xCA00:\n    registers[0] ^= opcode & 255').body[0]
                xor.orelse = last.orelse
                last.orelse = [xor]
            return node
    module = ast.fix_missing_locations(ast.Module(body=[FixtureStack().visit(nested)], type_ignores=[]))
    namespace = {'symbols': symbols, 'scratch': scratch, 'LOW': LOW,
                 'payload': resident['payload'], 'half': actual.half, 'word': actual.word}
    exec(compile(module, str(Path(prior.__file__)), 'exec'), namespace)
    execute = namespace['execute']
    top = worker['symbols']['__toy_pilot_stack_top']
    bottom = worker['symbols']['__toy_pilot_stack_bottom']
    sp = top - 520
    control, caller, source = (symbols[name] for name in
        ('_kui_toy_pilot_boot_control', '_kui_retail_native_caller', '_kui_retail_hook_source'))
    globals_ = {source: 0, control + 8: 4, control + 40: physical(top),
                control + 48: 0x8CFDF000, caller: 0x8C0BD374, caller + 4: sp}
    cases = 0
    for function, command, pr, writing in ((1, 129, 0x8C0BD374, 1), (0, 36, 0x8C0BD57E, 0)):
        valid = globals_ | {caller: pr}
        variants = [('accepted-P2', {}, function, command, sp, physical(sp) | writing),
                    ('lowest-owned-span', {caller + 4: bottom + 100}, function, command,
                     bottom + 100, physical(bottom + 100) | writing),
                    ('highest-owned-span', {caller + 4: top - 32}, function, command,
                     top - 32, physical(top - 32) | writing),
                    ('forged-P1', {caller + 4: physical(sp)}, function, command, physical(sp), 0),
                    ('wrong-function', {}, 2, command, sp, 0),
                    ('wrong-pr', {caller: pr + 2}, function, command, sp, 0),
                    ('wrong-sp', {caller + 4: sp + 4}, function, command, sp, 0),
                    ('unaligned', {caller + 4: sp + 2}, function, command, sp + 2, 0),
                    ('guard-save-overlap', {caller + 4: bottom + 96}, function, command, bottom + 96, 0),
                    ('bridge-anchor', {caller + 4: top - 28}, function, command, top - 28, 0),
                    ('fallback', {control + 8: 0}, function, command, sp, 0)]
        for _, changes, current_function, current_command, param, expected in variants:
            returned, final, calls, stores = execute('_kui_retail_resident_dispatch',
                [current_command, param, 0, current_function], valid | changes,
                expect_call=expected, capability=0xDEADBEEF)
            if returned != 0x89ABCDEF or final or len(calls) != 1 or not stores or stores[0]:
                raise ValueError('Compiled P2 scratch dispatch lost capability set/call/clear lifetime')
            cases += 1
    mapped = 0
    for capability in (0, physical(sp), physical(sp) | 1):
        for address in (physical(sp), physical(sp) - 4, physical(sp) + 4,
                        physical(bottom), physical(top) - 16):
            for length in (4, 16, 20):
                for writing in (0, 1):
                    expected = address if capability and length == 16 and address == physical(sp) and writing == capability & 1 else 0
                    actual_map, final, calls, _ = execute('_toy_map', [0, address, length, writing],
                        {}, capability=capability)
                    if actual_map != expected or final != capability or calls:
                        raise ValueError('Compiled P2 scratch mapper changed exact physical span/direction')
                    mapped += 1
    return {'actual_mask_before_first_stack_save': True,
            'original_PR_SP_capture_before_P2_stack': True,
            'compiled_P2_dispatch_cases': cases, 'compiled_protected_map_cases': mapped,
            'P2_native_parameter_and_P1_map_callback_distinguished': True,
            'capability_zero_before_dispatch_and_after_return': True,
            'scope': 'Actual compiled authored C with opaque SDK/core callback; native IRQ delivery unproved'}


def audit_private_pause_stack(worker, worker_disassembly=''):
    """Check actual P2 helper, guarded bridge anchor and borrowed native SP."""
    from toy_pilot_pause_stack_audit import audit_pause_stack
    code = Linked(worker, HIGH, worker_disassembly)
    top, bottom = (code.symbols.get(name, 0) for name in
                   ('__toy_pilot_stack_top', '__toy_pilot_stack_bottom'))
    if not 0xACFD0000 <= bottom < top <= 0xACFE0000 or top - bottom != 8192:
        raise ValueError('Private pause stack must use exact guarded P2 reservation')
    pump, _ = code.bounds('_kui_toy_pilot_native_pump')
    if code.literal(pump + 6, 0)[1] != top - 16:
        raise ValueError('Pause native pump lost actual P2 saved-SP anchor')
    bridge = '_kui_toy_pilot_worker_bridge'
    entry, _ = code.bounds(bridge)
    code.sequence(entry, (0x0102, None, 0x221B, 0x420E, None, 0x6322,
        0x2338, 0x8907, None, 0x6322, 0x7301, 0x2232, 0x410E,
        0xE000, 0x000B, 0x0009, 0xE301, 0x2232))
    if (code.literal(entry + 2, 2)[1] != 0xF0 or
            code.literal(entry + 8, 2)[1] != code.symbols['_kui_toy_pilot_bridge_active'] or
            code.literal(entry + 16, 2)[1] != code.symbols['_kui_toy_pilot_bridge_skips']):
        raise ValueError('Private bridge no longer masks before its guarded P2 claim')
    saves = code.find(bridge, (0x2F16, 0x4F22, 0x2F86, 0x2F96, 0x2FA6,
                               0x2FB6, 0x2FC6, 0x2FD6, 0x2FE6))
    at = saves + 18
    code.sequence(at, (0x61F3, 0x62F3, 0x7224, None, 0x0009, 0x68F3,
                       None, 0x7FF0, 0x2F82, 0x5188, 0x410E, 0x400B,
                       0x0009, 0x68F2, 0x6F83, 0x6EF6, 0x6DF6, 0x6CF6,
                       0x6BF6, 0x6AF6, 0x69F6, 0x68F6, 0x4F26))
    top_target, loaded_top = code.literal(at + 12, 15)
    if loaded_top != top:
        raise ValueError('Worker bridge stack literal is not the actual P2 top')
    finish = saves + 64
    code.sequence(finish, (0x0102, None, 0x221B, 0x420E, None, 0xE300,
        0x2232, 0x61F6, 0x410E, 0x000B, 0x0009))
    if (code.literal(finish + 2, 2)[1] != 0xF0 or
            code.literal(finish + 8, 2)[1] != code.symbols['_kui_toy_pilot_bridge_active']):
        raise ValueError('Private bridge lost masked release followed by exact entry SR restore')
    # A narrowly marked projection permits the old native helper scratch
    # simulation to retain its physical ownership ranges. The actual new
    # bridge mask/publication/anchor sequence above must pass first.
    projected = normalized_physical_view(worker, HIGH, worker_disassembly)
    blob = bytearray(projected['payload'])
    tail = saves + 20
    displacement = (top_target - ((tail + 4) & ~3)) // 4
    if not 0 <= displacement <= 255:
        raise ValueError('Pause projection cannot preserve its stack binding')
    baseline = (0x68F3, 0xDF00 | displacement, 0x7FF0, 0x2F82,
                0x400B, 0x0009, 0x68F2, 0x6F83, 0x6EF6, 0x6DF6,
                0x6CF6, 0x6BF6, 0x6AF6, 0x69F6, 0x68F6, 0x4F26)
    struct.pack_into('<16H', blob, saves + 18 - HIGH, *baseline)
    projected['payload'] = bytes(blob)
    proof = audit_pause_stack(projected)
    proof.update(actual_stack_top=hex(top), actual_stack_bottom=hex(bottom),
                 actual_P2_saved_SP_anchor=True,
                 compatibility_view='Only the old physical native-scratch simulation uses a projected bridge')
    return proof


def _actual_cache_snapshot(worker, disassembly):
    code = Linked(worker, HIGH, disassembly)
    begin, end = code.bounds('_kui_toy_pilot_snapshot')
    candidates = []
    for at, opcode, mnemonic, _ in code.rows:
        if (begin <= at < end and at not in code.literals and
                opcode & 0xFF00 == 0xD200 and mnemonic == 'mov.l' and
                code.literal(at, 2)[1] == 0xFF00001C):
            code.sequence(at + 2, (0x6222, 0x2239, 0x1124))
            candidates.append(at)
    masks = [at for at, op, mnemonic, _ in code.rows if begin <= at < end
             and at not in code.literals and op & 0xFF00 == 0x9300
             and mnemonic == 'mov.w' and code.literal(at, 3, 2)[1] == 0x105]
    if len(candidates) != 1 or len(masks) != 1 or masks[0] >= candidates[0]:
        raise ValueError('Private profile lacks actual CCR & 0x105 telemetry')
    return {'probe': hex(candidates[0]), 'mask': '0x00000105'}


def _early_policy(stage, disassembly, native_cache):
    # The existing early policy audit includes the worker telemetry check;
    # its stage part is reused for R by the full layout audit below. C must
    # still bind the exact native word and publish before arming the heap.
    code = Linked(stage, 0x8CE00000, disassembly)
    table = code.symbols.get('_toy_patches', 0)
    record = struct.pack('<IIHH', 0x8C0C5BC4, 0x105, 4, 10)
    records = code.data(table, 20 * 12)
    if sum(records[at:at + 12] == record for at in range(0, len(records), 12)) != 1:
        raise ValueError('Native cache profile lost exact original cache-word admission')
    begin, end = code.bounds('_kui_retail_stage_relay')
    rows = [(at, op) for at, op, _, _ in code.rows if begin <= at < end
            and at not in code.literals]
    targets = [code.literal(at, op >> 8 & 15)[1] for at, op in rows if op >> 12 == 0xD]
    publish = code.symbols.get('_toy_publish', 0)
    if not {code.symbols.get('_toy_original_patches_check'), publish} <= set(targets):
        raise ValueError('Early cache profile lost original checks/publication binding')
    code.sequence(publish, (None, None, 0x324C, 0x3216, 0x8901,
                            None, 0x342C, None, 0x325C, 0x3216, 0x8901,
                            None, 0x351C, None, 0x412B, 0x0009))
    for at, register, expected in ((0, 2, 0x54000000), (2, 1, 0x00FFFFFF),
                                   (10, 2, 0xE0000000), (14, 2, 0x54000000),
                                   (22, 1, 0xE0000000),
                                   (26, 1, code.symbols['_kui_toy_pilot_stage_publish'])):
        if code.literal(publish + at, register)[1] != expected:
            raise ValueError('Private stage publication wrapper lost exact alias bounds/target')
    pool = [i for i, (at, op) in enumerate(rows) if op & 0xFF00 == 0xD100
            and code.literal(at, 1)[1] == 0x8C0C5BC4]
    if len(pool) != 1:
        raise ValueError('Early profile must compare its exact native cache word')
    at = pool[0]
    sequence = rows[at:at + 11]
    if len(sequence) != 11:
        raise ValueError('Truncated early cache admission')
    original_register = 3 if native_cache else 2
    if code.literal(sequence[1][0], original_register, 2)[1] != 0x105:
        raise ValueError('Early cache admission expected original native 0x105')
    if native_cache:
        # Preserve the exact loaded-and-compared native word, only on the
        # successful compare path. This is a distinct linked sequence from
        # R's deliberate replacement by a 0x101 literal.
        code.sequence(sequence[2][0], (0x6212, 0x3230, 0x8B00, 0x6923))
        selected = 0x105
    else:
        code.sequence(sequence[2][0], (0x6312, 0x3320, 0x8B00))
        selected = code.literal(sequence[5][0], 9, 2)[1]
        if selected != 0x101:
            raise ValueError('Early cache word does not match selected comparison profile')
    code.sequence(sequence[6][0], (0x2192, None, None, 0x480B, 0x0009))
    if code.literal(sequence[7][0], 5)[1] != 0x8C0C5BC8 or code.literal(sequence[8][0], 4)[1] != 0x8C0C5BC4:
        raise ValueError('Early cache-word publication span differs')
    following = rows[at + 11:at + 17]
    if len(following) != 6 or code.literal(following[0][0], 4)[1] != 0x8C04E9B4:
        raise ValueError('Cache admission must precede heap-hook installation')
    code.sequence(following[3][0], (0x2412, 0x480B, 0x0009))
    return {'original': '0x00000105', 'selected': hex(selected),
            'cache_word_published_before_heap_hook': True,
            'hardware_observed': False}


def audit_cache_layout(resident, worker, stage, resident_disassembly,
                       worker_disassembly, stage_disassembly, *, native_cache=False,
                       source_root=ROOT):
    """Audit strictly admitted PRIVATE_P2 image dictionaries and objdump text."""
    for name, image, dis in (('resident-sci', resident, resident_disassembly),
                             ('worker', worker, worker_disassembly),
                             ('stage', stage, stage_disassembly)):
        try:
            instruction_audit(name, dis)
        except SystemExit as exc:
            raise ValueError(str(exc)) from exc
    ws, rs = worker['symbols'], resident['symbols']
    for image, begin, end, names in ((worker, HIGH, HIGH_LIMIT,
            ('__toy_pilot_data_begin', '__toy_pilot_bss_begin', '__toy_pilot_bss_end',
             '__toy_pilot_stack_bottom', '__toy_pilot_stack_top')),
            (resident, LOW, LOW_LIMIT,
             ('__retail_resident_data_begin', '__retail_resident_bss_begin', '__retail_resident_bss_end'))):
        if any(not begin + OFFSET <= image['symbols'].get(name, 0) <= end + OFFSET
               for name in names):
            raise ValueError('Every private data/BSS/stack bound must retain its P2 VMA')
    if (ws['__toy_pilot_stack_top'] - ws['__toy_pilot_stack_bottom'] != 8192 or
            ws['__toy_pilot_worker_end'] != physical(ws['__toy_pilot_stack_top']) or
            ws['__toy_pilot_worker_end'] != worker['memory_end'] or
            any(ws[name] & 31 for name in ('__toy_pilot_bss_begin', '__toy_pilot_bss_end',
                                         '__toy_pilot_stack_bottom', '__toy_pilot_stack_top'))):
        raise ValueError('Worker physical lease and P2 stack bounds differ')
    if rs.get('__retail_hook_stack_bottom') != 0xAC007800 or rs.get('__retail_hook_stack') != 0xAC007D00:
        raise ValueError('Low private stack must retain P2 runtime and fixed physical lease')
    helpers = {}
    for image, base, dis, suffix in ((worker, HIGH, worker_disassembly, 'worker'),
                                    (worker, HIGH, worker_disassembly, 'pause'),
                                    (resident, LOW, resident_disassembly, 'retail')):
        helpers[suffix] = audit_frame_helper(image, base,
            '_kui_toy_pilot_frame_publish_' + suffix, dis)
    hooks = audit_hook_publications(worker, HIGH, worker_disassembly,
        '_kui_toy_pilot_frame_publish_worker', {
        '_kui_toy_pilot_service_hook': (24, 8), '_kui_toy_pilot_am_init_hook': (16, 8),
        '_kui_toy_pilot_driver_load_hook': (16, 8), '_kui_toy_pilot_shutdown_hook': (24,),
        '_kui_toy_pilot_allstop_hook': (16,), '_kui_toy_pilot_worker_bridge': (36,)})
    hooks.update(audit_hook_publications(worker, HIGH, worker_disassembly,
        '_kui_toy_pilot_frame_publish_pause', {'_kui_toy_pilot_pause_hook': (8, 8)}))
    hooks.update(audit_hook_publications(resident, LOW, resident_disassembly,
        '_kui_toy_pilot_frame_publish_retail', {'_kui_toy_pilot_heap_hook': (52,)}))
    proof = {'private_P2': True, 'native_cache': bool(native_cache),
             'worker_physical_end': hex(worker['memory_end']),
             'private_stack_bottom': hex(ws['__toy_pilot_stack_bottom']),
             'private_stack_top': hex(ws['__toy_pilot_stack_top']),
             'frame_helpers': helpers, 'published_hook_frames': hooks,
             'masked_low_frame_exceptions': audit_masked_low_frames(resident, resident_disassembly),
             'native_heap_saved_state': audit_heap_saved_state(resident, resident_disassembly),
             'native_metadata_publication': audit_native_metadata(worker, worker_disassembly),
             'masked_HOLD_scope': audit_masked_hold_scope(worker, worker_disassembly),
             'stage_alias_publication': audit_stage_alias_publication(stage, stage_disassembly),
             'early_policy': _early_policy(stage, stage_disassembly, native_cache),
             'actual_CCR_snapshot': _actual_cache_snapshot(worker, worker_disassembly),
             'pause_stack': audit_private_pause_stack(worker, worker_disassembly),
             'native_scratch': audit_private_native_scratch(resident, worker, resident_disassembly),
             'audio_or_video_improvement_proven': False}
    return proof


def _load_cache_profile(builddir, *, native_cache=None):
    directory = Path(builddir)
    config = dict(line.split('=', 1) for line in
                  (directory / 'build-config').read_text().splitlines())
    required = {'PRIVATE_P2': '1', 'SHARED_SCI': '0', 'ASYNC_CDDA': '0',
                'SCI_REUSE_TDRE': '0', 'GD_FIXED_STEP': '2'}
    if any(config.get(key) != value for key, value in required.items()) or config.get('NATIVE_CACHE') not in ('0', '1'):
        raise ValueError('Cache audit requires exact baseline P2 comparison configuration')
    selected = config['NATIVE_CACHE'] == '1'
    if native_cache is not None and bool(native_cache) != selected:
        raise ValueError('Requested cache audit profile differs from actual build')
    objdump = shutil.which('sh-elf-objdump')
    if objdump is None:
        candidate = ROOT.parent / 'launch-logo-fix/.deps/sh-elf/bin/sh-elf-objdump'
        if not candidate.is_file():
            raise ValueError('SH objdump is required for linked cache audit')
        objdump = str(candidate)
    images, disassemblies = {}, {}
    for name, base, limit in (('resident-sci', LOW, LOW_LIMIT),
                              ('worker', HIGH, HIGH_LIMIT), ('stage', 0x8CE00000, 0x8CF80000)):
        path = directory / (name + '.elf')
        raw = path.read_bytes()
        options = {'private_p2': True} if name != 'stage' else {}
        if name == 'worker':
            options.update(entry_symbol='_kui_toy_pilot_initialize', entry_at_base=False)
        images[name] = inspect_elf(raw, base, limit, **options)
        images[name]['symbol_sizes'] = linked_symbol_sizes(raw)
        disassemblies[name] = subprocess.check_output([objdump, '-d', str(path)], text=True)
    return selected, images, disassemblies


def audit_cache_profiles(builddir, *, native_cache=None):
    selected, images, disassemblies = _load_cache_profile(builddir, native_cache=native_cache)
    return audit_cache_layout(images['resident-sci'], images['worker'], images['stage'],
        disassemblies['resident-sci'], disassemblies['worker'], disassemblies['stage'],
        native_cache=selected)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('builddir', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit_cache_profiles(args.builddir), indent=2, sort_keys=True))
