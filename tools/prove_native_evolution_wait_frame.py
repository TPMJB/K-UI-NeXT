#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay original Evolution 1 instructions supporting the wait-frame capture.

The owner supplies the original executable; no executable bytes are bundled.
This proves frame offsets and polling semantics in bounded modeled execution,
not console operands, IRQ timing, cache behavior, or an Evolution fix.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from profile_vmu_native_release_sh import ReleaseSH
from sh4_delay_slots import validate_delay_slot
from test_vmu_native_heap_execution import MASK, SH, signed

SCHEMA = 'kui.native.evolution1.original-wait-frame.v1'
IMAGE_SHA = 'ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35'
LOAD, STATUS = 0x8c010000, 0xa05f810c
ALIASES, SEEDS = (0, 0x80000000, 0xa0000000), (0, 1)
OFFSETS = dict(R0=140, R4=156, R12=188, R13=192, R14=196,
               ISR_SR=136, VBR=208, PR=216, SPC=220, SSR=224)
SOURCE_FILES = ('tools/prove_native_evolution_wait_frame.py',
    'tools/profile_vmu_native_release_sh.py',
    'tests/test_vmu_native_heap_execution.py', 'tools/sh4_delay_slots.py')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


class OriginalSH(ReleaseSH):
    """Existing integer decoder plus explicit SH control/FPU-word saves."""
    def __init__(self, image, seed=0):
        SH.__init__(self, image)
        self.active = False
        rng = random.Random(seed)
        self.r = [rng.getrandbits(32) for _ in range(16)]
        self.fr = [rng.getrandbits(32) for _ in range(32)]
        self.bank = 0
        self.fpscr, self.fpul = 0x40000, rng.getrandbits(32)
        self.mach, self.macl = rng.getrandbits(32), rng.getrandbits(32)
        self.gbr, self.vbr = 0x8c100000, 0x8c00f400
        self.pr, self.spc = 0x8c1f362c, 0x8c1f362c
        self.ssr, self.sr = 0x60000000 | seed, 0x700000f0 | seed
        self.status_values, self.mmio_reads, self.steps = None, [], []

    def read(self, address, count=4):
        if address == STATUS:
            require(count == 4 and self.status_values is not None,
                    'Unexpected PVR read')
            try:
                value = next(self.status_values)
            except StopIteration as error:
                raise ValueError('Original wait consumed its bounded PVR fixture') from error
            self.mmio_reads.append(value)
            return value
        return super().read(address, count)

    def plain(self, op, pc):
        n = op >> 8 & 15
        # All are architectural instruction decodes, not substituted routines.
        push = {0x4f33: 'ssr', 0x4f43: 'spc', 0x4f22: 'pr',
                0x4f13: 'gbr', 0x4f23: 'vbr', 0x4f02: 'mach',
                0x4f12: 'macl', 0x4f03: 'sr', 0x4f52: 'fpul',
                0x4f62: 'fpscr'}
        if op in push:
            self.r[15] -= 4
            self.write(self.r[15], getattr(self, push[op]))
        elif op & 0xf0ff == 0x406a:  # LDS Rn,FPSCR
            self.fpscr = self.r[n]
            self.bank = self.fpscr >> 21 & 1
        elif op & 0xf0ff == 0x405a:  # LDS Rn,FPUL
            self.fpul = self.r[n]
        elif op & 0xf00f == 0xf00b:  # FMOV.S FRm,@-Rn
            require(not self.fpscr & (1 << 20), 'Unexpected wide FPU save')
            m = op >> 4 & 15
            self.r[n] -= 4
            self.write(self.r[n], self.fr[self.bank * 16 + m])
        elif op == 0xfbfd:  # FRCHG
            self.bank ^= 1
            self.fpscr ^= 1 << 21
        elif op & 0xff00 == 0xc700:  # MOVA @(disp,PC),R0
            self.r[0] = ((pc + 4) & ~3) + (op & 255) * 4
        else:
            return super().plain(op, pc)
        self.executing = pc
        self.instructions += 1

    def step(self):
        pc = self.pc
        self.steps.append(pc)
        op = self.read(pc, 2)
        target, delay, n = None, False, op >> 8 & 15
        if op >> 12 in (10, 11):
            target, delay = (pc + 4 + 2 * signed(op & 4095, 12)) & MASK, True
            if op >> 12 == 11:
                self.pr = pc + 4
        elif op & 0xf0ff in (0x400b, 0x402b):
            target, delay = self.r[n], True
            if op & 0xf0ff == 0x400b:
                self.pr = pc + 4
        elif op == 11:
            target, delay = self.pr, True
        elif op >> 8 in (0x89, 0x8b, 0x8d, 0x8f):
            delay = op >> 8 in (0x8d, 0x8f)
            taken = bool(self.sr & 1) == (op >> 8 in (0x89, 0x8d))
            target = pc + 4 + 2 * signed(op & 255, 8) if taken else pc + (4 if delay else 2)
        if target is None:
            self.plain(op, pc)
            self.pc = pc + 2
        else:
            self.instructions += 1
            if delay:
                slot = self.read(pc + 2, 2)
                validate_delay_slot(slot, pc)
                self.steps.append(pc + 2)
                self.plain(slot, pc + 2)
            self.pc = target


def prologue(image, alias, seed, shared=False):
    cpu = OriginalSH(image, seed)
    cpu.r[15] = alias | 0x0c2ff000
    before, stack, live_sr = cpu.r.copy(), cpu.r[15], cpu.sr
    special = {128: cpu.fpscr, 132: cpu.fpul, 136: live_sr,
               200: cpu.macl, 204: cpu.mach, 208: cpu.vbr,
               212: cpu.gbr, 216: cpu.pr, 220: cpu.spc, 224: cpu.ssr}
    start, end = (0x8c1da29a, 0x8c1da324) if shared else (0x8c1da10e, 0x8c1da198)
    for pc in range(start, end, 2):
        cpu.plain(cpu.read(pc, 2), pc)
    base = cpu.r[15]
    require(stack - base == 228, 'Original common frame size changed')
    for reg in range(15):
        require(cpu.read(base + 140 + 4 * reg) == before[reg],
                'Wrong saved integer register offset: R' + str(reg))
    require(all(cpu.read(base + at) == value for at, value in special.items()),
            'Wrong saved control register offset')
    require(all(cpu.read(base + 4*i) == cpu.fr[(i+16) % 32] for i in range(32)),
            'Wrong saved FPU-word bank/layout')
    require(cpu.sr & 0x100000f0 == 0xf0,
            'Original dispatcher did not clear BL and establish IMASK15')
    return dict(alias=hex(alias), seed=seed, shared=shared, instructions=cpu.instructions,
                frame_bytes=stack-base, integer_registers_checked=15,
                control_words_checked=10, fpu_words_checked=32)


def shared_prefix(image, alias, seed, depth):
    cpu = OriginalSH(image, seed)
    owner, irq = alias | 0x0c00ed00, alias | 0x0c2fe000
    r0, r1 = cpu.r[0], cpu.r[1]
    cpu.r[1], cpu.r[15] = 0x8c1da274, owner - 4
    cpu.write(owner - 4, r1)
    cpu.write(0x8c1da41c, depth)
    cpu.write(0x8c1da410, 0x8c00dead)
    cpu.write(0x8c1da414, irq)
    cpu.pc = 0x8c1da274
    for _ in range(40):
        if cpu.pc == 0x8c1da29a:
            break
        require(0x8c1da274 <= cpu.pc < 0x8c1da29a, 'Shared prefix escaped its code window')
        cpu.step()
    else:
        raise ValueError('Shared prefix exceeded bound')
    require(cpu.r[:2] == [r0, r1], 'Shared prefix lost original R0/R1')
    require(cpu.read(0x8c1da41c) == depth + 1, 'Shared nesting update differs')
    require(cpu.r[15] == (irq if depth == 0 else owner), 'Shared stack switch differs')
    require(cpu.read(0x8c1da410) == (owner if depth == 0 else 0x8c00dead),
            'Shared interrupted-SP publication differs')
    return dict(alias=hex(alias), seed=seed, prior_depth=depth,
                interrupted_sp_published=depth == 0, instructions=cpu.instructions)


def polling(image, alias, seed, high=False):
    cpu = OriginalSH(image, seed)
    cpu.r[15] = alias | 0x0c00ed00
    # Execute the original mask load as well as both wait phases/getter.
    cpu.plain(cpu.read(0x8c1f3518, 2), 0x8c1f3518)
    require(cpu.r[13] == 1023, 'Original scanline mask differs')
    values = [0x3c00, 0x3c00, 0x3e0a, 0x3e0c, 0x3c00]
    if high:
        values = [value | 0xffffc000 for value in values]
    cpu.status_values, cpu.pc = iter(values), 0x8c1f35b0
    phases = []
    allowed = set(range(0x8c1f35b0, 0x8c1f35b8, 2)) | set(range(0x8c1f3620, 0x8c1f3630, 2)) | set(range(0x8c1f1f36, 0x8c1f1f3c, 2))
    for _ in range(120):
        if cpu.pc == 0x8c1f3630:
            break
        require(cpu.pc in allowed, 'Original poll escaped its bounded code/getter windows')
        if cpu.pc in (0x8c1f3626, 0x8c1f362e):
            require(cpu.sr & 1 == int(not (cpu.r[0] & 1023)),
                    'Post-TST saved T disagrees with original getter value')
            phases.append(dict(pc=hex(cpu.pc), R0=hex(cpu.r[0]), T=cpu.sr & 1))
        cpu.step()
    else:
        raise ValueError('Original polling loop exceeded bound')
    require(cpu.mmio_reads == values and cpu.r[4] == cpu.r[14] == 0x10c and
            cpu.r[12] == 0x8c1f1f36 and cpu.r[13] == 1023 and
            cpu.pr == 0x8c1f362c and cpu.sr & 1,
            'Original poll/getter contract changed')
    require([x['T'] for x in phases] == [1, 1, 0, 0, 1], 'Both polling phases were not exercised')
    return dict(alias=hex(alias), seed=seed, high_status_bits=high,
                instructions=cpu.instructions, reads=len(cpu.mmio_reads), phases=phases)


def mutated(image, address, value):
    copy = bytearray(image)
    copy[address-LOAD:address-LOAD+len(value)] = value
    return bytes(copy)


def negative(name, run):
    try:
        run()
    except (ValueError, AssertionError) as error:
        return dict(mutation=name, refused=True, reason=str(error))
    raise ValueError('Instruction mutation was not detected: ' + name)


def prove(startup):
    image = startup.read_bytes()
    require(len(image) == 2143652 and sha(image) == IMAGE_SHA, 'Requires exact original Evolution 1 input')
    positives = [prologue(image, alias, seed, shared) for alias in ALIASES
                 for seed in SEEDS for shared in (False, True)]
    shared = [shared_prefix(image, alias, seed, depth) for alias in ALIASES
              for seed in SEEDS for depth in (0, 1, 2)]
    polls = [polling(image, alias, seed, high) for alias in ALIASES
             for seed in SEEDS for high in (False, True)]
    word = lambda address: image[address-LOAD:address-LOAD+2]
    altered = [
        ('normal-R13-save-omitted', lambda: prologue(mutated(image, 0x8c1da11e, b'\x09\x00'), 0x80000000, 0)),
        ('normal-R13-saved-as-R12', lambda: prologue(mutated(image, 0x8c1da11e, word(0x8c1da120)), 0x80000000, 0)),
        ('shared-R13-save-omitted', lambda: prologue(mutated(image, 0x8c1da2aa, b'\x09\x00'), 0x80000000, 0, True)),
        ('normal-FPU-bank-switch-omitted', lambda: prologue(mutated(image, 0x8c1da16a, b'\x09\x00'), 0x80000000, 0)),
        ('shared-depth-branch-inverted', lambda: shared_prefix(mutated(image, 0x8c1da288,
            (int.from_bytes(word(0x8c1da288), 'little') ^ 0x200).to_bytes(2, 'little')), 0x80000000, 0, 0)),
        ('shared-saved-SP-cell-shifted', lambda: shared_prefix(mutated(image, 0x8c1da28e,
            (int.from_bytes(word(0x8c1da28e), 'little') ^ 1).to_bytes(2, 'little')), 0x80000000, 0, 0)),
        ('scanline-mask-changed', lambda: polling(mutated(image, 0x8c1f35c4, (2047).to_bytes(2, 'little')), 0x80000000, 0)),
        ('getter-MMIO-base-changed', lambda: polling(mutated(image, 0x8c1f1f80, (0xa05f8004).to_bytes(4, 'little')), 0x80000000, 0)),
        ('getter-return-omitted', lambda: polling(mutated(image, 0x8c1f1f38, b'\x09\x00'), 0x80000000, 0)),
        ('second-phase-TST-omitted', lambda: polling(mutated(image, 0x8c1f362c, b'\x09\x00'), 0x80000000, 0)),
    ]
    negatives = [negative(name, run) for name, run in altered]
    windows = ((0x8c1da10e, 0x8a), (0x8c1da274, 0xb0),
               (0x8c1f3518, 2), (0x8c1f35b0, 8), (0x8c1f3620, 16),
               (0x8c1f1f36, 6), (0x8c1f1f80, 4))
    return dict(schema=SCHEMA, scope=__doc__.strip(),
        input=dict(path=str(startup.resolve()), bytes=len(image), sha256=sha(image)),
        source_sha256={name: sha((ROOT/name).read_bytes()) for name in SOURCE_FILES},
        original_windows=[dict(address=hex(a), bytes=n, sha256=sha(image[a-LOAD:a-LOAD+n])) for a,n in windows],
        generic_frame_bytes=228, generic_offsets=OFFSETS,
        complete_offsets={name: 172+value for name,value in OFFSETS.items()},
        submit_offsets={name: 232+value for name,value in OFFSETS.items()},
        banked_integer_interpretation='R0/R4 require matching saved SSR and ISR_SR MD/RB bits 0x60000000; R12/R13/R14 are unbanked',
        getter=dict(address='0x8c1f1f36', halfwords=[hex(int.from_bytes(word(a), 'little')) for a in range(0x8c1f1f36, 0x8c1f1f3c, 2)],
                    literal_address='0x8c1f1f80', literal=hex(int.from_bytes(image[0x1e1f80:0x1e1f84], 'little')), read_address=hex(STATUS)),
        shared_outer_pointer_gate='Shared return witness 0x8c1da348, depth cell 0x8c1da41c==1, bounded/aligned savedSP from 0x8c1da410; not implemented in the wait collector',
        prologue_cases=positives, shared_prefix_cases=shared, polling_cases=polls, mutation_refusals=negatives,
        case_counts=dict(prologues=len(positives), shared_prefix=len(shared), polling=len(polls), refusals=len(negatives)),
        limits=['Instruction and bounded modeled-MMIO proof only; no timing/cache/hardware IRQ scheduling.',
                'Does not prove console operand values, mainline availability at scanline zero, or data integrity of demand reads.',
                'The production wait collector uses existing exact unwind witnesses; this tool independently proves original save-layout offsets and poll semantics.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = prove(args.startup)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(schema=SCHEMA, output=str(args.output), **report['case_counts'])))


if __name__ == '__main__':
    main()
