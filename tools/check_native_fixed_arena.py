#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked fixed-arena route of a dual native stage.

A reviewed fixed arena (a kind 2 resolution record) has no initializer
pointer. The stage borrows the C start's call into main instead:
native_arm_fixed_main gives the borrowed literal the trampoline, and the
trampoline gives it the selected reader's low heap hook, calls that hook
with the arena as the initializer's arguments, then enters main with the
C start's own arguments and return. This audit runs the actual compiled
code of the relay's dispatch, the arm and the trampoline. It proves writes
and register flow only; console compatibility and the RAM-map evidence that
selected the arena are separate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from check_vmu_native_sdk_sci_route import assert_c_abi
from check_native_dual_callbacks import (CallbackCPU, Refused, SELECTOR, expected_entries,
                                         linked_table, require)
import check_vmu_native_stage_stack as stage_stack

SCHEMA = 'kui.native.fixed-arena-route.audit.v1'
ARM = '_native_arm_fixed_main'
HOOK = '_kui_native_fixed_main_hook'
RETURN_LABEL = '_kui_native_fixed_main_return'
FLAG, PLAN_HOOK, MAIN_DATA = '_native_fixed', '_native_fixed_hook', '_native_fixed_main'
MASK = 0xffffffff
# Synthetic owner values: a borrowed literal mid-line, main, the allocator
# initializer and an arena. No retail bytes are read.
SLOT, MAIN, INITIALIZER = 0x8c145c30, 0x0c01b214, 0x8c14a674
BEGIN, BYTES, C_START_RETURN = 0x8cf00000, 0x00080000, 0x8c145b04
OWNER_SP = 0x8c00f3f8


def trampoline_words(stage):
    """The trampoline is hand-written; pin every word and its one literal."""
    address, body, section = symbol_bytes(stage, HOOK, 2)
    back = stage[4][RETURN_LABEL][0]
    require(section[2]&7 == 6 and not address&3 and back == address+0x1c,
            'Fixed-arena trampoline is not its exact immutable code')
    data = stage[4][MAIN_DATA]
    require(data[1] == 20 and not data[0]&3, 'Fixed-arena main record is not five words')
    require(len(body) == 0x34, 'Fixed-arena trampoline is not its exact extent')
    code = body
    expected = (0x2f46, 0x2f56, 0x2f66, 0x2f76, 0x4f22, 0xd009, 0x6102, 0x5204,
                0x2212, 0x02a3, 0x5401, 0x5502, 0x410b, 0x0009,
                0x4f26, 0x67f6, 0x66f6, 0x65f6, 0x64f6, 0xd002, 0x5003, 0x402b, 0x0009, 0x0009)
    require(struct.unpack_from('<24H', code) == expected,
            'Fixed-arena trampoline instructions changed')
    literal = struct.unpack_from('<I', code, 0x30)[0]
    require(literal == ((data[0]&0x1fffffff)|0xa0000000),
            'Fixed-arena trampoline does not read its record through P2')
    return dict(address=hex(address), return_address=hex(back), bytes=0x34,
                sha256=hashlib.sha256(code).hexdigest())


class FixedCPU(CallbackCPU):
    """The callback CPU, bounded to the fixed arm or the trampoline."""
    def __init__(self, resident, stage, reader, names, **options):
        super().__init__(resident, stage, reader, 1, **options)
        self.code_bounds = []
        for name in names:
            address, body, _ = symbol_bytes(stage, name, 2)
            self.code_bounds.append((address, address+len(body)))
        self.display_calls = 0
        self.display = stage[4]['_retail_display_line'][0]
        self.targets |= {self.display}
        self.line_flushes = []

    def plain(self, op, pc):
        if op&0xf0ff == 0x00a3:  # OCBP @Rn purges the whole line of Rn.
            self.line_flushes.append(self.r[op>>8&15]&~31)
            if self.initializer_slot is not None and self.r[op>>8&15]&~31 == self.initializer_slot&~31:
                self.flushes.append(self.initializer_slot&~31)
                return
            require(False, 'Fixed-arena code published an unrelated cache line')
        super().plain(op, pc)

    def callee(self):
        if self.pc == self.display:
            self.display_calls += 1
            continuation = self.pr
            for i in range(8):
                self.r[i] = (0x2468ace1+i*0x1111111+self.display_calls)&MASK
            self.mach ^= 0x5a5a5a5a
            self.sr ^= 1
            self.pc = continuation
            return
        super().callee()


def setup_arm(cpu, stage, low, *, flag=1, slot_value=MAIN):
    control, original = low[2]|0x20000000, low[3]|0x20000000
    for i in range(14):
        cpu.write(control+4*i, 0)
    for i in range(8):
        cpu.write(original+4*i, 0)
    heap, call = stage[4]['_native_heap'][0], stage[4]['_native_call'][0]
    for i in range(18):
        cpu.write(heap+4*i, 0)
    cpu.write(heap+24, INITIALIZER)
    cpu.write(heap+28, SLOT)
    cpu.write(heap+32, INITIALIZER)
    cpu.write(call, C_START_RETURN-6)
    cpu.write(call+4, C_START_RETURN-4)
    cpu.write(call+8, C_START_RETURN)
    cpu.write(call+12, BEGIN)
    cpu.write(call+16, BYTES)
    cpu.write(stage[4][FLAG][0], flag)
    cpu.write(stage[4][PLAN_HOOK][0], SLOT)
    cpu.write(stage[4][PLAN_HOOK][0]+4, MAIN)
    for i in range(5):
        cpu.write(stage[4][MAIN_DATA][0]+4*i, 0)
    cpu.write(SLOT, slot_value)
    return control, original


def arm_case(resident, stage, expected, reader, *, seed=0, bank=1, variant='ok', body=None):
    if body is not None:
        stage = (body, *stage[1:])
    cpu = FixedCPU(resident, stage, reader, (SELECTOR, ARM), seed=seed, bank=bank)
    low = expected[reader-1]
    failure = None
    if variant == 'flag':
        control, original = setup_arm(cpu, stage, low, flag=0)
        failure = SLOT
    elif variant == 'slot':
        control, original = setup_arm(cpu, stage, low, slot_value=INITIALIZER)
        failure = SLOT
    else:
        control, original = setup_arm(cpu, stage, low)
    if isinstance(variant, tuple) and variant[0] == 'control':
        cpu.write(control+4*variant[1], 0x12345678)
        failure = low[2]+4*variant[1]
    elif isinstance(variant, tuple) and variant[0] == 'original':
        cpu.write(original+4*variant[1], 0x12345678)
        failure = low[3]+4*variant[1]
    elif variant == 'readback':
        cpu.published_readback_bad = True
        failure = SLOT
    frame = (0x0c00e400, 0x8c00e400, 0xac00e400)[seed%3]
    pattern = [0x13579bc0+i for i in range(8)]
    for i, value in enumerate(pattern):
        cpu.write(frame+4*i, value)
    cpu.initializer_slot = SLOT
    cpu.r[4] = frame
    before = cpu.snapshot()
    cpu.writes = []
    trampoline, back = stage[4][HOOK][0], stage[4][RETURN_LABEL][0]
    record = stage[4][MAIN_DATA][0]
    try:
        cpu.run_symbol(ARM)
    except Refused:
        require(failure is not None and cpu.refusal == [2, failure],
                'Fixed arm refused with the wrong ownership witness')
        require([cpu.read(frame+4*i) for i in range(8)] == pattern,
                'Fixed arm published an owner-return hook after refusal')
        if variant != 'readback':
            require(not cpu.flushes and all(cpu.stack_top-256 <= at < cpu.stack_top
                    for _, at, _, _ in cpu.writes), 'Fixed arm changed ownership after a refusal')
        return 'refused'
    require(failure is None, 'Fixed arm accepted a changed control, original, flag, slot or readback')
    assert_c_abi(cpu, before)
    installer = stage[4]['_kui_vmu_observe_stage_install'][0]
    stack = stage[4]['_native_installer_stack'][0]+0x10000-32
    require(tuple(cpu.read(control+4*i) for i in range(3)) == (installer, stack, 1),
            'Fixed arm did not publish the selected control object and installer')
    require(tuple(cpu.read(original+4*i) for i in range(8)) ==
            (INITIALIZER, BEGIN, BYTES, back&0x1fffffff, 0, 0, 0, 0),
            'Fixed arm did not name the initializer, arena and trampoline return')
    require(tuple(cpu.read(record+4*i) for i in range(5)) == (low[0], BEGIN, BYTES, MAIN, SLOT),
            'Fixed arm did not give the trampoline its reader hook, arena, main and literal')
    wanted = pattern.copy()
    wanted[5] = low[1]
    require(cpu.read(SLOT) == trampoline and [cpu.read(frame+4*i) for i in range(8)] == wanted,
            'Fixed arm did not install the trampoline and this reader\'s owner return')
    require(cpu.flushes == [SLOT&~31] and cpu.display_calls == 1,
            'Fixed arm did not publish the literal once')
    allowed = {control, control+4, control+8, original, original+4, original+8, original+12,
               SLOT, frame+20, *(record+4*i for i in range(5))}
    require(all((at in allowed and count == 4) or cpu.stack_top-256 <= at < cpu.stack_top
                for _, at, _, count in cpu.writes),
            'Fixed arm wrote outside its exact owned objects, literal and frame')
    require(not cpu.vbr_writes and not cpu.iprb_writes, 'Fixed arm changed interrupt ownership')
    return 'armed'


class TrampolineCPU(FixedCPU):
    def __init__(self, resident, stage, reader, low, seed):
        super().__init__(resident, stage, reader, (HOOK,), seed=seed, bank=1)
        self.low = low
        self.heap_hook = low[0]
        self.hook_calls = 0
        self.entered_main = None
        self.targets = {self.heap_hook, MAIN|0x80000000, MAIN}
        self.back = stage[4][RETURN_LABEL][0]

    def callee(self):
        if self.pc == self.heap_hook:
            self.hook_calls += 1
            require(self.hook_calls == 1, 'Trampoline called the low heap hook twice')
            require(self.read(SLOT) == self.heap_hook and self.flushes == [SLOT&~31],
                    'Literal did not name the low heap hook, published, before the hook ran')
            require(self.r[4:6] == [BEGIN, BYTES] and self.pr == self.back and
                    self.r[15] == self.entry_sp-20,
                    'Low heap hook did not receive the arena and the trampoline return')
            self.hook_sp = self.r[15]
            continuation = self.pr
            for i in range(8):
                self.r[i] = (0x7654321f+i*0x2222223)&MASK
            self.mach ^= 0x33333333
            self.macl ^= 0xcccccccc
            self.sr ^= 1
            self.pr ^= 0x0f0f0f0e
            self.pc = continuation
            return
        require(self.pc in (MAIN, MAIN|0x80000000), 'Unexpected trampoline target')
        self.entered_main = (self.pc, self.r.copy(), self.pr)
        self.pc = self.stop

    def run(self):
        self.stop = 0x8c00a000
        self.run_to(self.symbols[HOOK][0], self.stop)
        return self


def trampoline_case(resident, stage, expected, reader, *, seed=0, body=None):
    if body is not None:
        stage = (body, *stage[1:])
    low = expected[reader-1]
    cpu = TrampolineCPU(resident, stage, reader, low, seed)
    record = stage[4][MAIN_DATA][0]
    for i, value in enumerate((low[0], BEGIN, BYTES, MAIN, SLOT)):
        cpu.write(record+4*i, value)
    cpu.write(SLOT, stage[4][HOOK][0])
    cpu.initializer_slot = SLOT
    cpu.r[15] = cpu.entry_sp = OWNER_SP
    cpu.pr = C_START_RETURN
    arguments = cpu.r[4:8].copy()
    callee_saved = cpu.r[8:15].copy()
    cpu.writes = []
    cpu.run()
    require(cpu.hook_calls == 1 and cpu.entered_main is not None,
            'Trampoline did not run the hook once and then enter main')
    target, registers, pr = cpu.entered_main
    require(target == MAIN and registers[4:8] == arguments and registers[8:15] == callee_saved and
            registers[15] == OWNER_SP and pr == C_START_RETURN,
            'main did not receive the C start\'s arguments, stack and return')
    require(cpu.line_flushes == [SLOT&~31], 'Trampoline published another cache line')
    frame = set(range(OWNER_SP-20, OWNER_SP, 4))
    require(all((at == SLOT and count == 4) or at in frame for _, at, _, count in cpu.writes),
            'Trampoline wrote outside the borrowed literal and its own frame')
    require(cpu.read(SLOT) == low[0], 'Borrowed literal does not keep the low heap hook')
    return 'entered'


def dispatch_case(stage, resident, stack, *, fixed):
    """The relay calls exactly one arm: the fixed one only for a fixed arena."""
    route = stage_stack.ArmRoute(stage, resident, stack, 3, 15, cache_hit=True, mode=0)
    route.mem[stage[4][FLAG][0]] = int(fixed)
    target = stage[4][ARM][0]
    route.targets[target] = 'fixed-arm'
    frames = []
    original = route.call

    def call(address):
        if address == target:
            frames.append(route.r[4])
        return original(address)
    route.call = call
    route.run()
    if fixed:
        # The modeled extent helper's own result is the relay's only store.
        require(frames == [route.frame] and route.calls[-1] == 'fixed-arm' and
                route.calls.count('fixed-arm') == 1 and not route.callback_helper_calls and
                not route.armed and route.nonstack_writes == [(route.exec_address, route.program_bytes)],
                'Relay did not hand a fixed arena only to the fixed arm')
    else:
        route.check()
        require(not frames and 'fixed-arm' not in route.calls, 'Relay ran the fixed arm for an initializer call')
    return 'fixed' if fixed else 'initializer'


def mutants(resident, stage, expected):
    rejected = 0
    address, _, section = symbol_bytes(stage, HOOK, 2)
    start = section[4]+address-section[3]
    for offset, replacement, label in ((0x12, 0x0009, 'publication'), (0x10, 0x0009, 'retire'),
                                       (0x14, 0x5402, 'arena begin')):
        data = bytearray(stage[0])
        struct.pack_into('<H', data, start+offset, replacement)
        try:
            trampoline_case(resident, stage, expected, 1, body=bytes(data))
        except (ValueError, AssertionError) as error:
            require(not str(error).startswith('Unsupported'), 'Mutant left the model: '+str(error))
            rejected += 1
        else:
            raise ValueError('Trampoline accepted a missing '+label)
    # Force each comparison or zero test in the fixed arm to T=1 (CMP/EQ
    # R0,R0) and to T=0 (CMP/HI R0,R0). For every test, at least one forced
    # outcome must change a case's result; a forced outcome equal to the
    # only reachable one (native_fixed is set whenever this arm runs) is
    # reported as equivalent.
    address, body, section = symbol_bytes(stage, ARM, 2)
    start = section[4]+address-section[3]
    words = [struct.unpack_from('<H', body, at)[0] for at in range(0, len(body)-1, 2)]
    pool = min(((address+2*i+4)&~3)+4*(op&255)-address for i, op in enumerate(words) if op&0xf000 == 0xd000)
    tests = [2*i for i, op in enumerate(words[:pool//2]) if op&0xf00f in (0x3000, 0x2008)]
    require(len(tests) >= 5, 'Fixed arm lacks its ownership comparisons')
    variants = ('ok', 'flag', 'slot', 'readback', ('control', 0), ('control', 13),
                ('original', 0), ('original', 7))
    equivalent = []
    for at in tests:
        detected = 0
        for forced in (0x3000, 0x3006):
            data = bytearray(stage[0])
            struct.pack_into('<H', data, start+at, forced)
            for variant in variants:
                try:
                    arm_case(resident, stage, expected, 1, variant=variant, body=bytes(data))
                except (ValueError, AssertionError) as error:
                    require(not str(error).startswith('Unsupported'), 'Mutant left the model: '+str(error))
                    detected += 1
                    rejected += 1
                    break
            else:
                equivalent.append(dict(offset=hex(at), forced_t=int(forced == 0x3000)))
        require(detected, 'Fixed arm test at +'+hex(at)+' changes no case under either outcome')
    mutants.equivalent = equivalent
    return rejected


def check(directory, *, self_test=False):
    directory = Path(directory)
    stage = read_elf(directory/'stage.elf')
    if ARM not in stage[4]:
        return dict(schema=SCHEMA, enabled=False)
    resident = read_elf(directory/'resident-sci.elf')
    legacy = read_elf(directory/'resident-legacy.elf')
    expected = (expected_entries(resident), expected_entries(legacy))
    linked_table(stage, expected)
    stack = stage_stack.typed_stack(stage)
    require(stack is not None, 'Fixed arena requires the typed installer stack')
    pin = trampoline_words(stage)
    cases = dict(armed=0, refused=0, entered=0)
    variants = ['ok', 'flag', 'slot', 'readback', *(('control', i) for i in range(14)),
                *(('original', i) for i in range(8))]
    # As in the callback audit, the SCI image backs memory and the reader
    # selects which linked low reader's roles must appear.
    for reader in (1, 2):
        for seed in range(3):
            for variant in variants:
                cases[arm_case(resident, stage, expected, reader, seed=seed, variant=variant)] += 1
            cases[trampoline_case(resident, stage, expected, reader, seed=seed)] += 1
    routes = [dispatch_case(stage, resident, stack, fixed=fixed) for fixed in (False, True)]
    proof = dict(schema=SCHEMA, enabled=True,
                 artifact_sha256={name: hashlib.sha256(image[0]).hexdigest()
                                  for name, image in (('stage', stage), ('resident', resident), ('legacy', legacy))},
                 trampoline=pin, cases=cases, relay_dispatch=routes,
                 arm_bytes=len(symbol_bytes(stage, ARM, 2)[1]),
                 limits=['Synthetic owner addresses; the reviewed plan and its RAM-map evidence are checked elsewhere.',
                         'The low heap hook, the installer and main are modeled as calls.'])
    if self_test:
        proof['mutants_rejected'] = mutants(resident, stage, expected)
        proof['equivalent_forced_outcomes'] = mutants.equivalent
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    try:
        print(json.dumps(check(args.directory, self_test=args.self_test), indent=2))
    except (OSError, ValueError, KeyError, AssertionError) as error:
        parser.exit(2, 'Native fixed-arena audit refused: '+str(error)+'\n')


if __name__ == '__main__':
    main()
