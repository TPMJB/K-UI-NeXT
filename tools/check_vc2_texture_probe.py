#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the texture probe's emitted stack switch to its owned C entry.

This checks the actual final ELF, not an extern-name waiver. The closed SH
model substitutes an adversarial ABI callee only after the literal is bound
to the helper's real typed C function; its complete C stack is audited by the
package checker. No console copy or texture correctness is established here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_loader_layout import region
from check_vmu_native_worker_gateway import read_elf, symbol_bytes

ENTRY = 0xACFCE100
STACK_BOTTOM, STACK_TOP = 0xACFCE800, 0xACFCF800
ENTRY_NAME = '_kui_vc2_texture_probe_entry'
RUN_NAME = '_kui_vc2_texture_probe_run'
FOLD_NAME = '_kui_vc2_texture_probe_fold'
PRE_NAME = '_kui_vc2_texture_probe_precapture'
LATE_NAME = '_kui_vc2_texture_probe_late'
MARKER = '__kui_vc2_texture_probe'
SELECTOR = '_kui_armada_ce_maple_diagnostic_mode'
FRAME_BYTES, STACK_BYTES = 40, 4096
# Twenty-two authored instructions, then stack-top and actual-C literals.
# All stores use the already-owned private stack. There is no FPU, cache,
# SR/MMU write or arbitrary indirect destination in this exact sequence.
WORDS = (0xD00A, 0x20F6, 0x2086, 0x4022, 0x4013, 0x4002, 0x4012,
         0x6F03, 0x7FF0, 0xD007, 0x400B, 0x0009, 0x7F10, 0x4F16,
         0x4F06, 0x4F17, 0x4F26, 0x68F6, 0x60F2, 0x6F03, 0x000B, 0x0009)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def payload_at(image, address, size):
    matches = [p for p in image[2] if p[0] == 1 and
               p[2] <= address < address + size <= p[2] + p[4]]
    require(len(matches) == 1, 'Texture probe bytes lack one initialized load')
    p = matches[0]
    return region(image[0], p[1] + address - p[2], size, 'texture probe bytes')


def owned_p1_code(image, address, size):
    loads = [p for p in image[2] if p[0] == 1 and
             p[2] <= address < address + size <= p[2] + p[4]]
    require(len(loads) == 1 and loads[0][2] == loads[0][3] and loads[0][6] == 5,
            'Texture C code or selector lacks its exact immutable physical alias')


def owned_p2_code(image, address, size):
    loads = [p for p in image[2] if p[0] == 1 and
             p[2] <= address < address + size <= p[2] + p[4]]
    require(len(loads) == 1 and loads[0][2] - loads[0][3] == 0x20000000 and
            loads[0][6] == 5,
            'Texture early code lacks its exact uncached immutable physical alias')


def _entry_contract(image):
    symbols, rows = image[4], image[3]
    require(symbols.get(MARKER) == (1, 0, 0, 0xFFF1),
            'Texture probe marker must be its exact absolute one')
    require(ENTRY_NAME in symbols and RUN_NAME in symbols and SELECTOR in symbols,
            'Texture probe lacks its entry, actual C target or selector')
    address, body, section = symbol_bytes(image, ENTRY_NAME, 2)
    require(address == ENTRY and len(body) == 52 and section[2] & 7 == 6,
            'Texture probe must retain the fixed P2 52-byte immutable entry')
    loads = [p for p in image[2] if p[0] == 1 and
             p[2] <= ENTRY < ENTRY + len(body) <= p[2] + p[4]]
    require(len(loads) == 1 and loads[0][6] == 5 and
            loads[0][3] + ENTRY - loads[0][2] == ENTRY - 0x20000000 and
            payload_at(image, ENTRY, len(body)) == body,
            'Texture probe P2 entry lacks its exact RX main-RAM load alias')
    end = symbols.get('__kui_maple_hook_binary_end', (0,))[0]
    require(ENTRY + len(body) <= end <= 0xACFCE200 and not end & 3,
            'Texture probe escapes the CRC-covered immutable helper end')
    run, run_body, run_section = symbol_bytes(image, RUN_NAME, 2)
    uncached_end = symbols.get('__kui_maple_uncached_end', (0,))[0]
    require(0xACFC4000 <= run < run + len(run_body) <= uncached_end <= 0xACFCE000 and
            not run & 1 and run_section[2] & 7 == 6 and
            run_section[3] == 0xACFC4000 and
            payload_at(image, run, len(run_body)) == run_body,
            'Texture probe C literal must name its actual owned uncached function')
    owned_p2_code(image, run, len(run_body))
    require(body == struct.pack('<22H2I', *WORDS, STACK_TOP, run),
            'Texture probe stack switch instructions, homes or literals differ')
    selector, selector_body, selector_section = symbol_bytes(image, SELECTOR, 1)
    require(len(selector_body) == 4 and not selector & 3 and
            0x8CFC4000 <= selector < selector + 4 <= 0x8CFCE000 and
            selector_section[2] & 7 == 6 and
            payload_at(image, selector, 4) == selector_body and
            struct.unpack('<I', selector_body)[0] == 4,
            'Texture probe must carry the actual immutable reserve-only mode four')
    owned_p1_code(image, selector, 4)
    require(symbols.get('__kui_vmu_worker_stack_bottom', (0,))[0] == STACK_BOTTOM and
            symbols.get('__kui_vmu_worker_stack_top', (0,))[0] == STACK_TOP,
            'Texture probe private worker stack bounds moved')
    stacks = [s for s in rows if s[3] == STACK_BOTTOM and s[5] == STACK_BYTES]
    require(len(stacks) == 1 and stacks[0][1] == 8 and stacks[0][2] & 7 == 3,
            'Texture probe must own the existing complete 4KiB zero-fill stack')
    private = [p for p in image[2] if p[0] == 1 and
               p[2] <= STACK_BOTTOM < STACK_TOP <= p[2] + p[5]]
    require(len(private) == 1 and private[0][6] == 6 and
            private[0][2] + private[0][4] <= STACK_BOTTOM and
            private[0][3] + STACK_BOTTOM - private[0][2] == STACK_BOTTOM - 0x20000000,
            'Texture probe worker stack lacks its writable zero-fill physical owner')
    return run, len(run_body), body, end


def _execute_entry(image, run):
    # Local import avoids a package-checker import cycle.
    from check_vmu_native_stage_stack import Machine

    class Probe(Machine):
        def __init__(self, seed, mask):
            super().__init__(image, ENTRY_NAME, seed, mask, 0x8C007D80)
            self.arguments = self.r[4:8].copy()
            self.private_writes = []

        def write(self, address, value):
            require(not address & 3 and STACK_TOP - FRAME_BYTES <= address < STACK_TOP,
                    'Texture wrapper writes outside its 40-byte private frame/homes')
            self.mem[address & 0x1FFFFFFF | 0x80000000] = value & 0xFFFFFFFF
            self.private_writes.append(address)

        def plain(self, word, pc):
            saved = {0x4022: 'pr', 0x4013: 'gbr', 0x4002: 'mach', 0x4012: 'macl'}
            key, n = word & 0xF0FF, word >> 8 & 15
            if key in saved:
                self.r[n] -= 4
                self.write(self.r[n], getattr(self, saved[key]))
                return
            super().plain(word, pc)

        def call(self, target):
            if target == self.stop_pc:
                return False
            require(target == run and not self.calls,
                    'Texture wrapper reaches another or repeated indirect target')
            require(self.r[4:8] == self.arguments and self.r[15] == STACK_TOP - FRAME_BYTES,
                    'Texture wrapper changes arguments or omits the C home area')
            self.calls.append(target)
            # All four caller argument-home words may be overwritten by C.
            for offset in range(0, 16, 4):
                self.write(self.r[15] + offset, 0xD15CA000 + offset)
            return_pc = self.pr
            for i in range(9):
                self.r[i] = (0x917253B0 + i * 0x1234567) & 0xFFFFFFFF
            self.pr ^= 0x13579BDF
            self.gbr ^= 0xC0DEC0DE
            self.mach ^= 0x10203040
            self.macl ^= 0x50607080
            self.sr ^= 1  # T is volatile; no control-state change is allowed.
            self.pc = return_pc
            return True

        def check(self):
            before, pr, mach, macl, gbr, sr, fpscr, fr = self.before
            require(self.calls == [run] and self.r[8:] == before[8:] and
                    (self.pr, self.mach, self.macl, self.gbr) == (pr, mach, macl, gbr) and
                    self.sr & ~1 == sr & ~1 and self.fpscr == fpscr and self.fr == fr and
                    not self.published and len(self.private_writes) == 10,
                    'Texture wrapper loses its caller or touches FPU/cache/control state')

    cases = 0
    for seed in range(8):
        for mask in range(16):
            Probe(seed, mask).run().check()
            cases += 1
    return cases


def _mutations(image, body):
    p = next(p for p in image[2] if p[0] == 1 and
             p[2] <= ENTRY < ENTRY + len(body) <= p[2] + p[4])
    at = p[1] + ENTRY - p[2]
    rejected = 0
    for offset in range(len(body)):
        changed = bytearray(image[0])
        changed[at + offset] ^= 1
        try:
            _entry_contract((bytes(changed), *image[1:]))
        except ValueError:
            rejected += 1
        else:
            raise ValueError('Texture probe accepted an emitted instruction/literal mutant')
    return rejected


def _p2_closure(image):
    """Reject cached static dependencies in the emitted PRE/early closure.

    Runtime source/destination pointers are deliberately not replaced by a
    fabricated physical alias. This is a static dependency/call audit; host
    execution checks the PRE tuple and early/late sequencing separately.
    """
    from sh4_delay_slots import delayed_branch, validate_delay_slot

    names = (RUN_NAME, FOLD_NAME, PRE_NAME)
    functions = {}
    uncached_end = image[4]['__kui_maple_uncached_end'][0]
    for name in names:
        address, body, section = symbol_bytes(image, name, 2)
        require(0xACFC4000 <= address < address + len(body) <= uncached_end and
                section[3] == 0xACFC4000 and section[2] & 7 == 6,
                'Texture PRE/early closure escaped its uncached code: ' + name)
        owned_p2_code(image, address, len(body))
        functions[name] = (address, body)
    late, late_body, late_section = symbol_bytes(image, LATE_NAME, 2)
    require(0x8CFC4000 <= late < late + len(late_body) <= 0x8CFCE000 and
            late_section[2] & 7 == 6,
            'Texture cached transition lacks its actual owned function')
    owned_p1_code(image, late, len(late_body))
    entries = {value[0]: key for key, value in functions.items()}
    entries[late] = LATE_NAME
    reports = []
    all_calls = []

    def written_registers(word):
        n, m, low, top = word >> 8 & 15, word >> 4 & 15, word & 15, word >> 12
        if top in (5, 6, 7, 9, 13, 14):
            return {n, m} if top == 6 and low in (4, 5, 6) else {n}
        if top == 3:
            return set() if low in (0, 2, 3, 6, 7) else {n}
        if top == 2:
            return {n} if low in (4, 5, 6, 9, 10, 11, 13) else set()
        if top == 4:
            return set() if word & 0xF0FF in (0x4011, 0x4015, 0x400B, 0x402B) else {n}
        return {n} if top == 0 and low in (2, 10, 12, 13, 14) else set()

    def literal_controls(words, literal_words, load_pc, register):
        """Follow one exact function literal through branches/register moves.

        GCC may tail-merge PRE and cached-late into one JMP. A linear scan
        loses the branch that skips the PRE literal; trace each literal's
        actual reaching paths instead. Values changed by arithmetic/loads
        are killed, and normal C calls kill volatile registers.
        """
        found, seen, todo = set(), set(), [(load_pc + 2, 1 << register)]

        def update(pc, live):
            op = words[pc]
            n, m = op >> 8 & 15, op >> 4 & 15
            copied = bool(live & (1 << m)) if op & 0xF00F == 0x6003 else False
            for reg in written_registers(op):
                live &= ~(1 << reg)
            if copied:
                live |= 1 << n
            return live

        while todo:
            pc, live = todo.pop()
            if not live or (pc, live) in seen:
                continue
            require(pc in words and pc not in literal_words,
                    'Texture literal-control trace leaves owned instructions')
            seen.add((pc, live))
            op, top = words[pc], words[pc] >> 12
            if op & 0xF0FF in (0x400B, 0x402B):
                if live & (1 << (op >> 8 & 15)):
                    found.add(pc)
                live = update(pc + 2, live)
                if op & 0xF0FF == 0x400B:
                    todo.append((pc + 4, live & ~255))
                continue
            if op == 0x000B:
                continue
            if top in (10, 11):
                delta = op & 4095
                delta -= 4096 if delta & 2048 else 0
                target = pc + 4 + 2 * delta
                live = update(pc + 2, live)
                if top == 11:
                    todo.append((pc + 4, live & ~255))
                elif target in words and target not in literal_words:
                    todo.append((target, live))
                continue
            if op >> 8 in (0x89, 0x8B, 0x8D, 0x8F):
                delta = op & 255
                delta -= 256 if delta & 128 else 0
                delayed = op >> 8 in (0x8D, 0x8F)
                if delayed:
                    live = update(pc + 2, live)
                todo.extend(((pc + 4 + 2 * delta, live),
                             (pc + (4 if delayed else 2), live)))
                continue
            todo.append((pc + 2, update(pc, live)))
        return found

    for name, (address, body) in functions.items():
        literals, literal_rows, calls, registers = set(), [], [], [None] * 16
        indirect_pcs = set()
        words = {address + off: struct.unpack_from('<H', body, off)[0]
                 for off in range(0, len(body) - 1, 2)}

        def call(pc, target):
            require(target in entries, 'Texture early closure calls an unbound leaf: ' + hex(pc))
            require(target != late or name == RUN_NAME,
                    'Only the P2 dispatcher may cross the cached observation boundary')
            calls.append(dict(pc=hex(pc), target=hex(target), symbol=entries[target]))

        def memory_store(base):
            require(base is None or not 0xE0000000 <= base <= 0xFFFFFFFF,
                    'Texture early closure writes a statically resolved MMIO address')

        for pc, word in sorted(words.items()):
            if pc in literals:
                continue
            n, m, low, top = word >> 8 & 15, word >> 4 & 15, word & 15, word >> 12
            require(top != 15 and word & 0xF0FF not in
                    (0x0083, 0x0093, 0x00A3, 0x00B3, 0x00C3, 0x400E, 0x4007,
                     0x405A, 0x406A, 0x4056, 0x4066),
                    'Texture PRE/early closure contains cache/FPU/control-state work')
            if delayed_branch(word):
                require(pc + 2 in words, 'Texture early branch lacks an owned delay slot')
                validate_delay_slot(words[pc + 2], pc)
            if top in (9, 13):
                size = 4 if top == 13 else 2
                target = ((pc + 4) & ~3) + 4 * (word & 255) if size == 4 else pc + 4 + 2 * (word & 255)
                require(address <= target < target + size <= address + len(body),
                        'Texture early literal escapes its exact uncached function')
                raw = payload_at(image, target, size)
                value = struct.unpack('<I' if size == 4 else '<h', raw)[0] & 0xFFFFFFFF
                literals.update(range(target, target + size, 2))
                if size == 4 and 0x8C000000 <= value < 0x8D000000:
                    require(value == late, 'Texture early literal names cached P1 data/code')
                if size == 4 and 0xAC000000 <= value < 0xAD000000:
                    state = [s for s in image[3] if s[1] == 8 and s[2] & 7 == 3 and
                             s[3] <= value < s[3] + s[5]]
                    require(value in entries or len(state) == 1,
                            'Texture early literal names unowned P2 static state')
                literal_rows.append(dict(pc=hex(pc), address=hex(target), bytes=size, value=hex(value)))
                registers[n] = value
            elif top == 14:
                value = word & 255
                registers[n] = (value - 256 if value & 128 else value) & 0xFFFFFFFF
            elif top == 7:
                value = word & 255
                registers[n] = None if registers[n] is None else (registers[n] + (value - 256 if value & 128 else value)) & 0xFFFFFFFF
            elif word & 0xF00F == 0x6003:
                registers[n] = registers[m]
            elif word & 0xF0FF in (0x400B, 0x402B):
                indirect_pcs.add(pc)
                for i in range(8):
                    registers[i] = None
            elif top in (10, 11):
                delta = word & 4095
                delta -= 4096 if delta & 2048 else 0
                target = pc + 4 + 2 * delta
                if top == 11 or not address <= target < address + len(body):
                    call(pc, target)
                    if top == 11:
                        for i in range(8):
                            registers[i] = None
            elif top == 1 or top == 2 and low in (0, 1, 2, 6):
                memory_store(registers[n])
                if top == 2 and low == 6:
                    registers[n] = None if registers[n] is None else registers[n] - 4
            elif top == 5 or top == 6:
                registers[n] = None
                if top == 6 and low in (4, 5, 6):
                    registers[m] = None
            elif top == 3 and low in (8, 12):
                a, b = registers[n], registers[m]
                registers[n] = None if a is None or b is None else (a - b if low == 8 else a + b) & 0xFFFFFFFF
            elif top == 2 and low in (9, 10, 11):
                a, b = registers[n], registers[m]
                registers[n] = None if a is None or b is None else (a & b if low == 9 else a ^ b if low == 10 else a | b)
            elif top == 4:
                # Conservatively forget results of shifts/special pops.
                registers[n] = None
            elif top == 0 and low in (2, 10, 12, 13, 14):
                registers[n] = None
        resolved = set()
        for literal in literal_rows:
            target, load_pc = int(literal['value'], 16), int(literal['pc'], 16)
            if target not in entries:
                continue
            register = words[load_pc] >> 8 & 15
            for control in sorted(literal_controls(words, literals, load_pc, register)):
                call(control, target)
                resolved.add(control)
        require(resolved == indirect_pcs,
                'Texture early closure has an indirect call without an actual reaching owned literal')
        reports.append(dict(symbol=name, address=hex(address), bytes=len(body),
                            sha256=hashlib.sha256(body).hexdigest(), literals=literal_rows, calls=calls))
        all_calls.extend((name, row['symbol']) for row in calls)
    require((RUN_NAME, PRE_NAME) in all_calls and (RUN_NAME, FOLD_NAME) in all_calls and
            (RUN_NAME, LATE_NAME) in all_calls,
            'Texture P2 dispatcher lacks its actual PRE/fold/cached-boundary edges')
    return dict(functions=reports, cached_transition_symbol=LATE_NAME,
                cached_transition_address=hex(late), cached_static_data_references=0,
                static_MMIO_writes=0, cache_operations=0,
                limits=['Emitted instructions/literals/call targets are checked; this is not a CPU-cache model.',
                        'Runtime source/destination reads remain intentional and may be cacheable; no claim of zero data-cache activity is made.',
                        'PRE tuple validation and early/late execution ordering require the separate compiled host tests.',
                        'The frozen redisplay branch may enter the terminal cached path without a new comparison.'])


def audit_entry(image):
    """Audit the typed high wrapper; never grant an arbitrary extern leaf."""
    run, run_size, body, end = _entry_contract(image)
    return dict(entry=hex(ENTRY), wrapper_bytes=len(body),
                wrapper_sha256=hashlib.sha256(body).hexdigest(),
                shim_low_stack_bytes=0, high_bytes=FRAME_BYTES,
                high_frame_bytes=FRAME_BYTES, private_stack_bytes=STACK_BYTES,
                C_target_symbol=RUN_NAME, C_target_address=hex(run), C_target_bytes=run_size,
                immutable_end=hex(end), immutable_mode=4,
                instruction_cases=_execute_entry(image, run),
                emitted_byte_mutation_rejections=_mutations(image, body),
                P2_early_closure=_p2_closure(image),
                guest_stack_writes=0, cache_operations=0, MMIO_writes=0,
                FPU_state_unchanged=True,
                scope='Exact linked stack switch and adversarial C-home ABI; complete C stack and hardware copy behavior are separate checks')


def audit_low_entry(image):
    """Require the fixed low ABI symbol, not an arbitrary missing function."""
    require(image[4].get(ENTRY_NAME) == (ENTRY, 0, 0, 0xFFF1),
            'Low texture call must name the exact absolute fixed P2 entry')
    candidates = []
    seen = set()

    def writes(word):
        """GPR writes relevant to the bounded low caller's count proof."""
        n, m, low, top = word >> 8 & 15, word >> 4 & 15, word & 15, word >> 12
        if top in (5, 6, 7, 9, 13, 14):
            return {n, m} if top == 6 and low in (4, 5, 6) else {n}
        if top == 3:
            return set() if low in (0, 2, 3, 6, 7) else {n}
        if top == 2:
            return {n} if low in (4, 5, 6, 9, 10, 11, 13) else set()
        if top == 4:
            return set() if word & 0xF0FF in (0x4011, 0x4015, 0x400B, 0x402B) else {n}
        return {n} if top == 0 and low in (2, 10, 12, 13, 14) else set()

    def original_count(body, call_offset):
        # Both admitted emissions keep incoming ABI r6 in callee-saved r11.
        # Bind that prologue assignment and reject every intervening r11
        # rewrite. This does not infer provenance from a count-move pattern
        # alone. Function literals are after the calls, outside this span.
        saved = None
        for at in range(0, min(32, call_offset), 2):
            op = struct.unpack_from('<H', body, at)[0]
            if op == 0x6B63:  # MOV incoming r6,r11
                saved = at
                break
            require(6 not in writes(op) and op & 0xF0FF != 0x400B and
                    op >> 12 not in (10, 11) and
                    op >> 8 not in (0x89, 0x8B, 0x8D, 0x8F),
                    'Low count must be saved before an incoming-count rewrite/control transfer')
        require(saved is not None, 'Low caller must preserve incoming count in r11')
        require(all(11 not in writes(struct.unpack_from('<H', body, at)[0])
                    for at in range(saved + 2, call_offset, 2)),
                'Low caller rewrites the preserved incoming count before the probe')
        return saved

    for name, (address, size, kind, owner) in image[4].items():
        if kind != 2 or not size or owner >= len(image[3]) or not 0x8C004000 <= address < address + size <= 0x8C007EA0:
            continue
        if (address, size) in seen:
            continue
        seen.add((address, size))
        _, body, section = symbol_bytes(image, name, 2)
        require(section[2] & 7 == 6, 'Low texture caller is not immutable executable code')
        for offset in range(0, len(body) - 1, 2):
            word = struct.unpack_from('<H', body, offset)[0]
            if word >> 12 != 13:
                continue
            pc = address + offset
            literal = ((pc + 4) & ~3) + 4 * (word & 255)
            # A function's final pool is data, and may happen to decode as
            # MOV.L. Only initialized literal-backed candidates participate.
            try:
                value = struct.unpack('<I', payload_at(image, literal, 4))[0]
            except ValueError:
                continue
            if value != ENTRY:
                continue
            register = word >> 8 & 15
            # The new noinline halfword sink leaves POST's count move
            # immediately BEFORE its fixed entry literal. The older emission
            # loads the literal first. Both require r11's incoming-r6 proof.
            count_value = ('original' if offset >= 2 and
                           struct.unpack_from('<H', body, offset - 2)[0] == 0x66B3 else None)
            # The actual C compiler loads the fixed literal then JSRs through
            # that register. Permit intervening NOP/argument moves only, with
            # no target-register rewrite or another control transfer.
            for after in range(offset + 2, min(offset + 34, len(body) - 1), 2):
                op = struct.unpack_from('<H', body, after)[0]
                if op == (0x400B | register << 8):
                    slot = struct.unpack_from('<H', body, after + 2)[0]
                    pre_flag = count_value == 'marker' and slot == 0x26BB
                    post_count = (count_value == 'original' and 6 not in writes(slot))
                    require(pre_flag or post_count,
                            'Low PRE must OR count bit31; POST must pass the original count')
                    require(pre_flag or slot == 9 or slot & 0xF00F in (0x6003, 0x6002),
                            'Low POST delay slot must remain an argument move/load or NOP')
                    saved_count = original_count(body, after)
                    candidates.append(dict(function=name, literal_load=hex(pc),
                                           call=hex(address + after), target=hex(ENTRY),
                                           phase='PRE' if pre_flag else 'POST',
                                           incoming_count_save=hex(address + saved_count),
                                           preserved_count_register='r11'))
                    break
                n = op >> 8 & 15
                if op >> 12 == 13 and n == 6 and n != register:
                    pool = ((address + after + 4) & ~3) + 4 * (op & 255)
                    require(struct.unpack('<I', payload_at(image, pool, 4))[0] == 0x80000000,
                            'Low PRE count marker differs from bit31')
                    count_value = 'marker'
                    continue
                argument_move = (op & 0xF00F == 0x6003 and n != register)
                if argument_move and n == 6:
                    count_value = ('original' if op == 0x66B3 else
                                   count_value if op == 0x6663 else None)
                stack_argument = (op & 0xF00F == 0x6002 and
                                  op >> 4 & 15 == 15 and n != register)
                if stack_argument and n == 6:
                    count_value = None
                if not (op == 9 or argument_move or stack_argument):
                    break
    require(len(candidates) == 2, 'Low texture probe requires exact PRE and POST literal-bound JSR calls')
    require(all(c['function'] == '_native_payload_write' for c in candidates),
            'Low texture probe calls must remain in the actual payload writer')
    require([c['phase'] for c in candidates] == ['PRE', 'POST'],
            'Low texture probe requires the exact PRE/POST call pair')
    return dict(absolute_entry=hex(ENTRY), shim_low_stack_bytes=0, calls=candidates,
                PRE_count_marker='0x80000000',
                limit='Exact linked call bindings and count arguments; execution order around the copy/progress is checked by the separate actual-function host replay')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('helper', type=Path)
    parser.add_argument('--resident', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        result = dict(schema='kui.vc2.texture-probe-entry.v1',
                      helper=audit_entry(read_elf(args.helper)))
        if args.resident:
            result['resident'] = audit_low_entry(read_elf(args.resident))
        text = json.dumps(result, indent=2) + '\n'
        if args.output:
            args.output.write_text(text)
        else:
            print(text, end='')
    except (OSError, KeyError, ValueError, AssertionError, struct.error) as error:
        parser.exit(1, f'Texture probe entry audit failed: {error}\n')


if __name__ == '__main__':
    main()
