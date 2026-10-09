#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fail-closed linked integer bridge/vector/stack audit for the isolated SCI pilot."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from package_cdda_toy_pilot import WORKER_BASE, WORKER_LIMIT, stack_rows, linked_halfwords


class Linked:
    def __init__(self, worker):
        self.worker, self.payload = worker, worker['payload']
        self.symbols, self.sizes = worker['symbols'], worker['symbol_sizes']

    def half(self, address):
        offset = address - WORKER_BASE
        if offset < 0 or offset + 2 > len(self.payload):
            raise ValueError('Shared audit instruction outside linked worker')
        return struct.unpack_from('<H', self.payload, offset)[0]

    def word(self, address):
        return self.half(address) | self.half(address + 2) << 16

    def code(self, symbol):
        address = self.symbols.get(symbol, 0)
        if not WORKER_BASE + 76 <= address < WORKER_BASE + len(self.payload) or address % 2:
            raise ValueError('Missing linked shared code symbol: ' + symbol)
        return address

    def pattern(self, begin, expected, name):
        for index, opcode in enumerate(expected):
            if opcode is not None and self.half(begin + index * 2) != opcode:
                raise ValueError(name + ' changed reviewed instruction at +' + hex(index * 2))

    def literal(self, address, register, expected, width=4):
        opcode = self.half(address)
        if opcode & 0xff00 != (0xd000 if width == 4 else 0x9000) | register << 8:
            raise ValueError('Shared audit expected exact PC-relative literal load')
        at = (((address + 4) & ~3) + (opcode & 255) * 4 if width == 4 else
              address + 4 + (opcode & 255) * 2)
        value = self.word(at) if width == 4 else self.half(at)
        if value != expected:
            raise ValueError('Shared audit literal does not target admitted state/code')
        return at


def gd_bridge(linked):
    begin = linked.code('_kui_toy_pilot_gd_bridge')
    # Source caller SP holds argument five. Three preserved words then move
    # the caller frame to r8; arguments r4..r7 are never touched by this code.
    sequence = (0x60f2, 0x4f22, 0x2f86, 0x2f96, 0x68f3, None, 0x6212, None,
                0x3320, 0x8b09, None, 0x2f06, None, 0x400b, 0x0009,
                None, 0x6212, None, 0x3320, 0x8900, 0xe0ff, 0x6f83,
                0x69f6, 0x68f6, 0x4f26, 0x000b, 0x0009)
    linked.pattern(begin, sequence, 'GD private-stack bridge')
    bottom, top = (linked.symbols.get(name, 0) for name in
                  ('__toy_pilot_gd_stack_bottom', '__toy_pilot_gd_stack_top'))
    for offset in (10, 30):
        linked.literal(begin + offset, 1, bottom)
    for offset in (14, 34):
        linked.literal(begin + offset, 3, 0xa55a4aa5)
    linked.literal(begin + 20, 15, top)
    last = linked.literal(begin + 24, 0, linked.code('_kui_toy_pilot_gd_dispatch'))
    if linked.sizes.get('_kui_toy_pilot_gd_bridge') != last + 4 - begin:
        raise ValueError('GD bridge gained unreviewed trailing code')
    # Interpret the emitted supported opcodes for good entry guard, bad
    # entry guard, and a guard corrupted by the opaque C adapter. The call
    # uses a deliberately distinct fifth argument and result.
    cases = []
    for initial_guard, corrupt_after in ((0xa55a4aa5, False), (0, False), (0xa55a4aa5, True)):
        r = [0x12340000 + i for i in range(16)]
        r[15] = 0x8c007cf0
        original = r.copy()
        pr, original_pr, t = 0x8c004abc, 0x8c004abc, False
        fifth, returned = 0x8c004dc0, 0x98765432
        memory = {r[15]: fifth, bottom: initial_guard}
        pc, calls = begin, []
        for _ in range(64):
            opcode = linked.half(pc)
            n, m, low = opcode >> 8 & 15, opcode >> 4 & 15, opcode & 15
            next_pc = pc + 2
            if opcode & 0xf000 == 0xd000:
                r[n] = linked.word(((pc + 4) & ~3) + (opcode & 255) * 4)
            elif opcode == 0x0009:
                pass
            elif opcode == 0x4f22:
                r[15] -= 4; memory[r[15]] = pr
            elif opcode == 0x4f26:
                pr = memory[r[15]]; r[15] += 4
            elif opcode >> 12 == 2 and low == 6:
                r[n] -= 4; memory[r[n]] = r[m]
            elif opcode >> 12 == 6 and low in (2, 3, 6):
                r[n] = r[m] if low == 3 else memory[r[m]]
                if low == 6:
                    r[m] += 4
            elif opcode >> 12 == 3 and low == 0:
                t = r[n] == r[m]
            elif opcode & 0xff00 in (0x8900, 0x8b00):
                branch = t if opcode & 0xff00 == 0x8900 else not t
                if branch:
                    displacement = opcode & 255
                    if displacement & 128:
                        displacement -= 256
                    next_pc = pc + 4 + displacement * 2
            elif opcode == 0xe0ff:
                r[0] = 0xffffffff
            elif opcode == 0x400b:
                if r[0] != linked.symbols['_kui_toy_pilot_gd_dispatch']:
                    raise ValueError('GD bridge model called an unreviewed function')
                if r[4:8] != original[4:8] or memory.get(r[15]) != fifth or r[15] != top - 4:
                    raise ValueError('GD bridge loses register/stack C arguments')
                calls.append({'sp': r[15], 'fifth': memory[r[15]]})
                # Opaque authored C function obeys its checked integer ABI.
                r[0] = returned
                if corrupt_after:
                    memory[bottom] = 0
                if linked.half(pc + 2) != 0x0009:
                    raise ValueError('GD bridge has an unreviewed call delay slot')
                next_pc = pc + 4
            elif opcode == 0x000b:
                if linked.half(pc + 2) != 0x0009:
                    raise ValueError('GD return has an unreviewed delay slot')
                break
            else:
                raise ValueError('GD bridge model encountered unreviewed opcode')
            pc = next_pc
        else:
            raise ValueError('GD bridge model did not return')
        expected = returned if initial_guard and not corrupt_after else 0xffffffff
        if r[0] != expected or r[4:8] != original[4:8] or r[8:16] != original[8:16] or pr != original_pr:
            raise ValueError('GD bridge fails result, preserved register, or caller SP restoration')
        if len(calls) != int(initial_guard != 0):
            raise ValueError('GD bad guard does not refuse the adapter')
        cases.append({'entry_guard_valid': bool(initial_guard), 'guard_corrupted_by_adapter': corrupt_after,
                      'adapter_calls': len(calls), 'result': f'0x{r[0]:08x}'})
    return {'entry': f'0x{begin:08x}', 'stack_argument_copy_bytes': 4,
            'saved_caller_bytes': 12, 'preserved': 'PR, r8, r9, caller SP; r4..r7 untouched',
            'linked_opcode_cases': cases, 'scope': 'Exact emitted authored bridge; opaque C callee uses integer ABI'}


def irq_vectors(linked):
    syms = linked.symbols
    begin = linked.code('_kui_toy_pilot_sci_irq_entry')
    sequence = (None, 0x2f06, 0x2f16, 0x2f26, 0x2f36, 0x2f46, 0x2f56, 0x2f66, 0x2f76,
        0x4f22, 0x4f02, 0x4f12, None, 0x400b, 0x0009, None, 0x6212, None, 0x3320,
        0x8902, None, 0xe201, 0x2122, 0x2008, 0x4f16, 0x4f06, 0x4f26,
        0x67f6, 0x66f6, 0x65f6, 0x64f6, 0x63f6, 0x62f6, 0x61f6, 0x60f6,
        0x0f3a, 0x8b01, 0x002b, 0x0009, 0x40fa, None, 0x402b, 0x0009)
    linked.pattern(begin, sequence, 'SCI own-stack IRQ entry')
    irq_bottom, irq_top = (syms.get(name, 0) for name in
                          ('__toy_sci_irq_stack_bottom', '__toy_sci_irq_stack_top'))
    linked.literal(begin, 15, irq_top - 16)
    linked.literal(begin + 24, 0, linked.code('_kui_toy_pilot_sci_irq'))
    linked.literal(begin + 30, 1, irq_bottom)
    linked.literal(begin + 34, 3, 0xa55a5aa5)
    linked.literal(begin + 40, 1, syms.get('_kui_toy_pilot_sci_irq_fault', 0))
    linked.literal(begin + 80, 0, linked.code('_kui_toy_pilot_sci_release_600'))
    # Model both owned return and declined event: all eight banked integer
    # registers, PR/MACH/MACL and interrupted SP survive the C clobbers.
    registers = {f'r{i}': 0x12345000 + i for i in range(8)}
    registers.update(pr=0x8c012340, mach=0xa1234567, macl=0xb9876543, sp=0x8c012000)
    original = registers.copy()
    for forward in (False, True):
        stack, sp = {}, irq_top - 16
        for name in (*[f'r{i}' for i in range(8)], 'pr', 'mach', 'macl'):
            sp -= 4; stack[sp] = registers[name]
        for name in registers:
            registers[name] = 0xdddddddd
        for name in ('macl', 'mach', 'pr', *[f'r{i}' for i in reversed(range(8))]):
            registers[name] = stack[sp]; sp += 4
        registers['sp'] = original['sp']  # STC SGR,R15, after restoring the frame.
        if registers != original or sp != irq_top - 16:
            raise ValueError('SCI IRQ frame model fails integer/SP restoration')
    forward = linked.code('_kui_toy_pilot_sci_forward')
    linked.pattern(forward, (0x40fa, None, 0x402b, 0x0009, 0, 0), 'Forward-vector template')
    linked.literal(forward + 2, 0, 0)
    interrupt = linked.code('_kui_toy_pilot_sci_interrupt')
    linked.pattern(interrupt, (0x40fa, None, 0x6002, 0x4009, 0x4009, 0x4001,
        0x8827, 0x8904, 0x8828, 0x8902, None, 0x402b, 0x0009, None, 0x402b, 0x00fa),
        'SCI event-filter template')
    linked.literal(interrupt + 2, 0, 0xff000028)
    linked.literal(interrupt + 20, 0, syms['_kui_toy_pilot_sci_release_600'])
    linked.literal(interrupt + 26, 0, begin)
    # Releasing entries keep the caller's integer values, give the game its
    # VBR/IPRB before entry, and restore r0 from DBR in the jump delay slot.
    release = linked.code('_kui_toy_pilot_sci_release_100')
    expected = (0x2f16, 0x2f26, None, 0xa009, 0xe12c,
        0x2f16, 0x2f26, None, 0xa004, 0xe130,
        0x2f16, 0x2f26, None, 0xe134, 0x2f36, None,
        0x031e, 0x7301, 0x0136, 0xe334, 0x3130, 0x8b15, 0x5302, 0x2338, 0x8912,
        0x0332, 0x4300, 0x4300, 0x8b0e, 0x5303, 0xe103, 0x3312, 0x890a,
        0x7301, 0x1033, 0x4308, 0x4300, 0x330c, 0x0142, 0x1312, 0x013a, 0x1313,
        None, 0x414e, 0x6302, 0x432e, 0x323c, 0x5101, None,
        0x6031, 0x201a, 0xc9f0, 0x6131, 0x210a, 0x2311, 0x6131,
        0x63f6, 0x6023, 0x62f6, 0x61f6, 0x402b, 0x00fa)
    linked.pattern(release, expected, 'SCI native-vector release')
    if syms['_kui_toy_pilot_sci_release_400'] != release + 10 or syms['_kui_toy_pilot_sci_release_600'] != release + 20:
        raise ValueError('SCI release entry spans changed')
    for offset, value in ((4, 0x100), (14, 0x400), (24, 0x600)):
        linked.literal(release + offset, 2, value, 2)
    linked.literal(release + 30, 0, syms['_kui_toy_pilot_sci_release'])
    linked.literal(release + 84, 1, syms['_kui_toy_pilot_sci_rehook'])
    linked.literal(release + 96, 3, 0xffd00008)
    rehook = linked.code('_kui_toy_pilot_sci_rehook')
    linked.pattern(rehook, (0x2f06, 0x2f16, 0x2f26, 0x2f36, 0x0302, None,
        0x203b, 0x400e, 0x433e, None, 0x5103, 0x6213, 0x4208, 0x4200, 0x320c,
        0x5322, 0x434e, 0x5223, 0x63f3, 0x7310, 0x3230, 0x8bfe,
        0x71ff, 0x1013, 0x5102, 0x2118, 0x8910, 0x0122, 0x6202, 0x3120,
        0x8b0c, 0x510a, 0x7101, 0x101a, None, 0x412e, None,
        0x6011, 0xca10, 0xc9f0, 0x6211, 0x220a, 0x2121, 0x6211,
        0x63f6, 0x62f6, 0x61f6, 0x002b, 0x60f6), 'Native IRQ return rehook')
    linked.literal(rehook + 10, 0, 0x100000f0)
    linked.literal(rehook + 18, 0, syms['_kui_toy_pilot_sci_release'])
    linked.literal(rehook + 68, 1, syms['_kui_toy_pilot_sci_vectors'] - 0x100)
    linked.literal(rehook + 72, 1, 0xffd00008)
    vectors = syms.get('_kui_toy_pilot_sci_vectors', 0)
    if linked.sizes.get('_kui_toy_pilot_sci_vectors') != 0x540 or vectors % 32:
        raise ValueError('High vector BSS region has wrong size/alignment')
    if linked.sizes.get('_kui_toy_pilot_sci_release') != 56:
        raise ValueError('SCI release frame differs from its assembly offsets')
    return {'irq_entry': f'0x{begin:08x}', 'private_frame_bytes': 44,
        'stack_top_reserved_bytes': 16, 'preserved_integer_state': 'bank1 r0..r7, PR, MACH, MACL; r8..r14 by integer C ABI; R15 from SGR',
        'owned_return': 'RTE with untouched hardware SSR/SPC',
        'declined_event': 'fully restored integer frame then native VBR/IPRB release',
        'vector_region': f'0x{vectors:08x}', 'vector_region_bytes': 0x540,
        'SCI_events': ['0x4e0', '0x500'], 'native_release_entries': ['0x100', '0x400', '0x600'],
        'rehook_pending_slots': 3, 'scope': 'Exact emitted authored template, IRQ entry, release and rehook code'}


def vector_publisher(linked):
    begin = linked.code('_toy_sci_publish')
    linked.pattern(begin, (None, 0x402b, 0x0009, 0xe0e0, 0x2409,
        0x3452, 0x8903, 0x04a3, 0x7420, 0xaffa, 0x0009,
        None, 0x6102, None, 0x212b, 0x2012,
        *([0x0009] * 8), 0x000b, 0x0009), 'P2 vector cache publisher')
    linked.literal(begin, 0, begin + 6 + 0x20000000)
    linked.literal(begin + 22, 0, 0xff00001c)
    linked.literal(begin + 26, 2, 0x800)
    return {'entry': f'0x{begin:08x}', 'P2_body': f'0x{begin + 6 + 0x20000000:08x}',
        'operand_cache_lines': 'OCBP over supplied vector interval, 32-byte steps',
        'CCR': 'read existing word, OR 0x800 ICI, write once; existing policy bits preserved'}


def audit_shared_bridges(worker, disassembly=None):
    linked = Linked(worker)
    return {'GD': gd_bridge(linked), 'SCI_IRQ': irq_vectors(linked), 'vector_publication': vector_publisher(linked)}


def audit_shared_schedule(root, worker, disassembly):
    """Bind the service policy and its compiled caller without matching C loops.

    Protocol behavior and budget exhaustion are checked by the production C
    fixtures recorded in the host log. This independent packaging check reads
    the public preprocessed limits and the actual linked service references.
    It does not assert a hardware deadline or infer game interrupt masks.
    """
    root = Path(root)
    header = root / 'src/loader/toy_pilot_sci.h'
    macros = subprocess.check_output(['cc', '-E', '-dM', '-x', 'c',
        '-DKUI_TOY_PILOT_SHARED_SCI=1', '-DKUI_TOY_PILOT_ASYNC_CDDA=1',
        '-I', str(root / 'include'), str(header)], text=True)
    limits = {}
    for name, expected in (('KUI_TOY_SCI_SERVICE_BLOCKS', 4),
            ('KUI_TOY_SCI_SERVICE_STEPS', 1024), ('KUI_TOY_SCI_SERVICE_TICKS', 1500),
            ('KUI_SCI_STREAM_TOKEN_SLICE', 256)):
        # sci_stream.h is intentionally a separate interface. Ask its own
        # preprocessor rather than scanning an implementation's loop syntax.
        output = macros
        if name == 'KUI_SCI_STREAM_TOKEN_SLICE':
            output = subprocess.check_output(['cc', '-E', '-dM', '-x', 'c',
                '-DKUI_TOY_PILOT_SHARED_SCI=1', '-DKUI_TOY_PILOT_ASYNC_CDDA=1',
                '-I', str(root / 'include'),
                str(root / 'src/loader/sci_stream.h')], text=True)
        values = re.findall(r'^#define\s+' + re.escape(name) + r'\s+([^\n]+)$', output, re.M)
        if len(values) != 1:
            raise ValueError('Missing unique published shared service limit: ' + name)
        raw = values[0].strip().strip('()')
        if not re.fullmatch(r'(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*', raw):
            raise ValueError('Service limit is not a reviewed integer constant: ' + name)
        value = int(re.sub(r'[uUlL]+$', '', raw), 0)
        if value != expected:
            raise ValueError('Published shared service policy changed: ' + name)
        limits[name] = value

    linked = Linked(worker)
    service = linked.code('_kui_toy_pilot_sci_service')
    adapter = linked.code('_kui_toy_pilot_gd_dispatch')
    adapter_end = adapter + linked.sizes.get('_kui_toy_pilot_gd_dispatch', 0)
    if adapter_end <= adapter:
        raise ValueError('GD adapter lacks actual linked function extent')
    wait = linked.symbols.get('_kui_sci_stream_wait')
    stop = linked.symbols.get('_kui_sci_stream_stop', 0)
    stop_end = stop + linked.sizes.get('_kui_sci_stream_stop', 0)
    rows, literals = linked_halfwords(disassembly)
    references, cancellation_waits = [], []
    for index, row in enumerate(rows):
        address, opcode, mnemonic, _ = row
        if address in literals:
            continue
        target, call = None, None
        if opcode & 0xf000 == 0xd000 and mnemonic == 'mov.l':
            at = ((address + 4) & ~3) + (opcode & 255) * 4
            target = linked.word(at)
            if wait and target == wait:
                if not stop <= address < stop_end:
                    raise ValueError('Generic DMA wait referenced outside the cancellation stop fence')
                cancellation_waits.append(f'0x{address:08x}')
            if target != service:
                continue
            register = opcode >> 8 & 15
            # The compiler loads the direct C target into a register. Require
            # its actual JSR nearby; a changed pointer-routing contract needs
            # review rather than silently treating data as a checked call.
            for following in rows[index + 1:index + 8]:
                if following[0] in literals:
                    continue
                if following[0] - address > 16:
                    break
                if following[1] == 0x400b | register << 8:
                    call = following[0]
                    break
            if call is None:
                raise ValueError('Service address has an unreviewed indirect routing use')
        elif opcode & 0xf000 in (0xa000, 0xb000):
            displacement = opcode & 0xfff
            if displacement & 0x800:
                displacement -= 0x1000
            target = address + 4 + displacement * 2
            if wait and target == wait:
                if not stop <= address < stop_end:
                    raise ValueError('Generic DMA wait called outside the cancellation stop fence')
                cancellation_waits.append(f'0x{address:08x}')
            if target != service:
                continue
            call = address
        else:
            continue
        if not adapter <= address < adapter_end or not adapter <= call < adapter_end:
            raise ValueError('Foreground shared service has a caller outside the GD adapter')
        references.append({'target_load': f'0x{address:08x}', 'call': f'0x{call:08x}'})
    if len(references) != 1:
        raise ValueError('Foreground service must have one reviewed linked GD adapter call site')
    source_files = ('src/loader/toy_pilot_sci.h', 'src/loader/toy_pilot_sci.c',
        'src/loader/toy_pilot_gd.c', 'src/loader/sci_stream.h', 'src/loader/sci_stream.c',
        'tests/test_toy_pilot_gd_async.c', 'tests/test_toy_pilot_sci.c')
    hashes = {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in source_files}
    return {'published_limits': limits, 'physical_block_bytes': 512,
        'timer_tick_microseconds': 1.28, 'between_step_admission_ms': 1.92,
        'hard_deadline': False, 'service_entry': f'0x{service:08x}',
        'only_linked_caller': '_kui_toy_pilot_gd_dispatch', 'linked_call_sites': references,
        'generic_DMA_wait_references': {'only_from': '_kui_sci_stream_stop',
            'linked_sites': cancellation_waits,
            'purpose': 'Existing bounded cancellation fence, never the normal foreground pump'},
        'protocol_context_checked_by': 'Production GD adapter fixtures: every CHECK/EXEC services data/raw audio, including absent data handle; accepted data REQUEST uses nonblocking pump',
        'quota_accounting': 'All verified data/raw blocks since prior CHECK/EXEC reduce four-block allowance; reset for new request/cancel',
        'audio_wait_exception': 'One nonblocking checked data-boundary opportunity can arm raw audio; no second payload is consumed in that stream step',
        'token_budget_scope': 'One allowance for the complete external entry',
        'source_sha256': hashes,
        'scope': 'Public policy plus emitted direct caller binding; host fixtures verify behavior; no hardware scheduling guarantee'}


def audit_async_cdda(root, worker, disassembly):
    """Bind actual worker raw delivery to the enabled asynchronous source path.

    This combines the emitted direct API call with the compiler-preprocessed
    refill function. Behavioral fixtures independently poison the retained
    synchronous callback and check pending output/SR/generation behavior.
    """
    root = Path(root)
    linked = Linked(worker)
    entry = linked.code('_kui_toy_pilot_sci_audio_read')
    fill_names = [name for name in linked.symbols
                  if name == '_fill_step' or name.startswith('_fill_step.')]
    fill_ranges = [(linked.code(name), linked.code(name) + linked.sizes.get(name, 0))
                   for name in fill_names]
    if not fill_ranges or any(end <= begin for begin, end in fill_ranges):
        raise ValueError('Asynchronous audio lacks actual linked worker refill extent')
    for name in ('_kui_toy_pilot_sci_audio_acquire', '_kui_toy_pilot_sci_audio_release'):
        if name in linked.symbols:
            raise ValueError('Retired synchronous audio lease entry remains in async worker')
    rows, literals = linked_halfwords(disassembly)
    calls = []
    for index, row in enumerate(rows):
        address, opcode, mnemonic, _ = row
        if address in literals:
            continue
        target, call = None, None
        if opcode & 0xf000 == 0xd000 and mnemonic == 'mov.l':
            target = linked.word(((address + 4) & ~3) + (opcode & 255) * 4)
            if target != entry:
                continue
            register = opcode >> 8 & 15
            for following in rows[index + 1:index + 8]:
                if following[0] in literals:
                    continue
                if following[0] - address > 16:
                    break
                if following[1] == 0x400b | register << 8:
                    call = following[0]
                    break
            if call is None:
                raise ValueError('Async raw API address has unreviewed indirect routing')
        elif opcode & 0xf000 in (0xa000, 0xb000):
            displacement = opcode & 0xfff
            if displacement & 0x800:
                displacement -= 0x1000
            target = address + 4 + displacement * 2
            if target != entry:
                continue
            call = address
        else:
            continue
        if not any(begin <= address < end and begin <= call < end for begin, end in fill_ranges):
            raise ValueError('Async raw delivery API is referenced outside worker refill')
        calls.append({'target_load': f'0x{address:08x}', 'call': f'0x{call:08x}'})
    if len(calls) != 1:
        raise ValueError('Async worker must have one reviewed complete-sector API call')

    definitions = ['KUI_ON_CONSOLE=1', 'KUI_RETAIL_TOY_PILOT=1',
        'KUI_RETAIL_LOW_RESIDENT=1', 'KUI_RETAIL_GD_REJECTION_DETAILS=1',
        'KUI_TOY_PILOT_SHARED_SCI=1', 'KUI_TOY_PILOT_ASYNC_CDDA=1']
    enabled = subprocess.check_output(['sh-elf-gcc', '-E', '-P', '-std=c11',
        '-I', str(root / 'include'), '-I', str(root / 'src/loader'),
        *['-D' + item for item in definitions],
        str(root / 'src/loader/toy_pilot_worker.c')], text=True)
    begin = enabled.find('static void fill_step(void)')
    if begin < 0:
        raise ValueError('Missing preprocessed enabled refill function')
    position = enabled.find('{', begin) + 1
    depth = 1
    while depth:
        if position >= len(enabled):
            raise ValueError('Truncated preprocessed refill function')
        depth += (enabled[position] == '{') - (enabled[position] == '}')
        position += 1
    body = enabled[begin:position]
    if len(re.findall(r'\bkui_toy_pilot_sci_audio_read\s*\(', body)) != 1:
        raise ValueError('Enabled refill source does not select exactly one async raw API')
    if re.search(r'\braw_fn\b|owner\s*\.\s*config\s*\.\s*read_raw\b|'
            r'\bkui_toy_pilot_sci_audio_(?:acquire|release)\s*\(', body):
        raise ValueError('Enabled refill still invokes the synchronous raw callback/lease path')
    return {'API_entry': f'0x{entry:08x}', 'API': 'kui_toy_pilot_sci_audio_read(lba,generation,stable_output)',
        'worker_refill_functions': fill_names, 'linked_calls': calls,
        'enabled_refill_source_sha256': hashlib.sha256(body.encode()).hexdigest(),
        'compiler_preprocessor_definitions': definitions,
        'normal_synchronous_raw_callback_in_enabled_refill': False,
        'retained_low_callback': 'Validated config/bootstrap compatibility; not invoked by enabled refill',
        'timing_scope': 'First worker request through complete verified delivery; includes game execution between visits, not CPU blocking time',
        'worker_cadence_changed': False,
        'behavior_checked_by': 'Production async worker fixture with forbidden legacy raw callback, pending PCM preservation, exact SR and cancellation epochs',
        'scope': 'Actual direct linked API plus enabled compiler-preprocessed refill source; host fixtures test delivery semantics; no hardware continuity guarantee'}


def audit_shared_stacks(directory, resident, worker):
    s = worker['symbols']
    bss_begin, bss_end = s['__toy_pilot_bss_begin'], s['__toy_pilot_bss_end']
    irq_bottom, irq_top = s['__toy_sci_irq_stack_bottom'], s['__toy_sci_irq_stack_top']
    gd_bottom, gd_top = s['__toy_pilot_gd_stack_bottom'], s['__toy_pilot_gd_stack_top']
    audio_bottom, audio_top = s['__toy_pilot_stack_bottom'], s['__toy_pilot_stack_top']
    if (not WORKER_BASE <= bss_begin <= irq_bottom < irq_top <= bss_end <= gd_bottom < gd_top <=
            audio_bottom < audio_top == s['__toy_pilot_worker_end'] <= WORKER_LIMIT or
            irq_top - irq_bottom != 2048 or gd_top - gd_bottom != 4096 or audio_top - audio_bottom != 8192 or
            any(value % 32 for value in (irq_bottom, irq_top, gd_bottom, gd_top, audio_bottom, audio_top))):
        raise ValueError('Independent guarded SCI/GD/audio stack bounds changed or overlap')
    low = stack_rows(list((directory / 'sci/lto').glob('*.ltrans*.su')))
    high = stack_rows(list((directory / 'worker').rglob('*.su')))
    low_common = [row for row in low if row['function'] not in ('kui_retail_resident_init', 'kui_toy_pilot_read_raw')]
    # IRQ/GD paths use shared engine/mapper/core and low acquisition callbacks.
    # Counting complete participating source-unit frames intentionally overcounts
    # mutually exclusive functions instead of guessing at an indirect call graph.
    engine_units = ('toy_pilot_sci.su', 'sci_stream.su', 'retail_cursor.su', 'retail_gd.su', 'minic.su')
    engine = [row for row in high if Path(row['report']).name in engine_units]
    if not any(row['function'] == 'kui_toy_pilot_sci_irq' for row in engine):
        raise ValueError('Missing actual SCI handler compiler stack evidence')
    scalar_names = ('kui_toy_pilot_request', 'kui_toy_pilot_snapshot', 'single_audio',
                    'manifest', 'publish', 'kui_toy_pilot_gd_project')
    scalar = [row for row in high if row['function'].split('.', 1)[0] in scalar_names or
              Path(row['report']).name == 'toy_pilot_gd.su']
    snapshot_names = ('kui_toy_pilot_snapshot', 'kui_toy_pilot_sci_snapshot')
    snapshots = [row for row in high if row['function'].split('.', 1)[0] in snapshot_names or
                 Path(row['report']).name == 'minic.su']
    if not all(any(row['function'] == name for row in snapshots) for name in snapshot_names):
        raise ValueError('Missing low-callable scalar snapshot compiler evidence')
    groups = {'low_hook': (0x8c007d00 - 0x8c007800 - 16 - 32, low_common + snapshots, 256),
              'SCI_IRQ': (irq_top - irq_bottom - 64 - 16, engine + low_common, 44),
              'GD': (gd_top - gd_bottom - 64 - 4, engine + low_common + scalar, 128),
              'audio': (audio_top - audio_bottom - 96, high +
                        [row for row in low if row['function'] != 'kui_retail_resident_init'], 256)}
    result = {}
    for name, (available, rows, assembly) in groups.items():
        total = sum(row['bytes'] for row in rows) + assembly
        if total > available:
            raise ValueError(f'{name} emitted conservative stack sum {total} exceeds {available}')
        result[name] = {'conservative_bytes': total, 'available_bytes': available,
            'margin_bytes': available - total, 'assembly_allowance': assembly,
            'emitted_c_frames': rows, 'call_graph': False,
            'scope': 'All emitted frames of participating authored source units/shared low callbacks; game SDK callee frames excluded'}
    result['reservations'] = {'SCI_IRQ': [f'0x{irq_bottom:08x}', f'0x{irq_top:08x}'],
        'GD': [f'0x{gd_bottom:08x}', f'0x{gd_top:08x}'], 'audio': [f'0x{audio_bottom:08x}', f'0x{audio_top:08x}']}
    return result


def mutation_test(worker):
    audit_shared_bridges(worker)
    linked = Linked(worker)
    cases = 0
    # Every reviewed GD/IRQ integer instruction must fail closed when changed.
    locations = [linked.code('_kui_toy_pilot_gd_bridge') + offset for offset in (0, 2, 4, 6, 8, 22, 26, 28, 40, 42, 44, 46, 48, 50)]
    locations += [linked.code('_kui_toy_pilot_sci_irq_entry') + offset for offset in
        (2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 26, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 74, 78, 82)]
    for address in locations:
        broken = copy.copy(worker)
        blob = bytearray(worker['payload'])
        original = struct.unpack_from('<H', blob, address - WORKER_BASE)[0]
        struct.pack_into('<H', blob, address - WORKER_BASE, original ^ 1)
        broken['payload'] = bytes(blob)
        try:
            audit_shared_bridges(broken)
        except ValueError:
            cases += 1
        else:
            raise ValueError('Shared bridge audit accepted changed register/control instruction')
    for symbol, offset in (('_kui_toy_pilot_gd_bridge', 20), ('_kui_toy_pilot_sci_irq_entry', 0),
                           ('_toy_sci_publish', 0), ('_kui_toy_pilot_sci_rehook', 68)):
        address = linked.code(symbol) + offset
        opcode = linked.half(address)
        at = ((address + 4) & ~3) + (opcode & 255) * 4
        broken = copy.copy(worker)
        blob = bytearray(worker['payload'])
        struct.pack_into('<I', blob, at - WORKER_BASE, 0)
        broken['payload'] = bytes(blob)
        try:
            audit_shared_bridges(broken)
        except ValueError:
            cases += 1
        else:
            raise ValueError('Shared audit accepted changed stack/vector/P2 target')
    return cases


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('worker_elf', type=Path)
    parser.add_argument('--mutation-test', action='store_true')
    args = parser.parse_args()
    from package_toy_shared_sci import inspect_shared_worker
    linked = inspect_shared_worker(args.worker_elf.read_bytes())
    report = audit_shared_bridges(linked)
    if args.mutation_test:
        report['corrupted_cases_rejected'] = mutation_test(linked)
    print(json.dumps(report, indent=2))
