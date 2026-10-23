#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked optional exception gateway, including its low veneer.

This is a closed SH integer instruction model, not a console fault-reproduction
claim. The only substituted boundary is the existing nonreturning terminal
gateway. Nonfatal events must reach the original vector with the exact entry
register state, and every store before terminal must belong to private state.
"""
import argparse
import json
import random
import struct
from pathlib import Path

from check_vmu_native_stage_stack import Machine, canonical
from check_vmu_native_worker_gateway import MASK, read_elf, symbol_bytes

VECTOR = 0x8C00F500
VBR = VECTOR - 0x100
CONTINUE = VECTOR + 8
DEFAULT = 0x8C1D9F50
EVENTS = (0xE0, 0x100, 0x180, 0x1A0)
GATEWAY = '_kui_vmu_native_fault_capture_gateway'
STATE = '_kui_vmu_native_fault_capture_state'
TERMINAL = '_kui_vmu_native_runtime_fault_hook'
PUBLISH = '_kui_vmu_native_fault_capture_publish'


class FaultGateway(Machine):
    def __init__(self, image, event, seed=0, owner_sp=0, owner_bank=0,
                 custom=False, *, body=None, veneer=None):
        super().__init__(image, GATEWAY, seed, 15, owner_sp, body=body)
        self.gateway_base = self.base
        self.vector = bytearray(0x3C)
        struct.pack_into('<4H', self.vector, 0, 0x6F03, 0xD00D, 0x402B, 9)
        struct.pack_into('<I', self.vector, 0x38, self.gateway_base)
        if veneer is not None:
            self.vector[:] = veneer
        self.pc = VECTOR
        self.event = event
        self.state_address, self.state_bytes, kind, _ = image[4][STATE]
        assert kind == 1 and self.state_bytes >= 32
        self.terminal = image[4][TERMINAL][0]
        self.stop_pc = -1
        self.sgr = owner_sp
        self.spc = (0x8C12ABCE + seed * 6) & MASK
        self.ssr = (0x40000000 | (owner_bank << 29) | ((seed & 15) << 4) |
                    (seed & 1) | ((seed & 2) << 7))
        # Hardware enters bank 1 with BL, MD, RB set, retaining flag bits.
        self.sr = 0x70000000 | (self.ssr & 0x000083F3)
        self.tea = (0x01020305 + seed * 0x10204) & MASK
        self.vbr = VBR
        self.dbr = (0x8C001000 + seed * 32) & MASK
        self.bank = [random.Random(seed + 177 + i).getrandbits(32) for i in range(8)]
        self.r[15] = owner_sp
        self.r[0] = (0x0147ACE1 + seed * 0x12479) & MASK
        self.r[1] = (0xF13579BE - seed * 0x1271) & MASK
        self.mem = {canonical(self.state_address) + 4*i: 0 for i in range(8)}
        self.mem[canonical(self.state_address) + 16] = CONTINUE
        self.mem[canonical(self.state_address) + 20] = DEFAULT
        self.mem[canonical(self.state_address) + 28] = 1
        if event in EVENTS:
            slot = VBR + 0x200 + ((event - 0x200) >> 3)
            self.mem[canonical(slot)] = DEFAULT + (4 if custom else 0)
        self.mem[0xFF000024] = event
        self.mem[0xFF00000C] = self.tea
        self.before = self.snapshot()
        self.writes = []
        self.reads = []
        self.executed = set()
        self.literal_reads = set()
        self.captured = event in EVENTS and not custom

    def snapshot(self):
        return (self.r.copy(), self.bank.copy(), self.pr, self.mach, self.macl,
                self.gbr, self.sr, self.fpscr, self.fr.copy(), self.spc,
                self.ssr, self.sgr, self.vbr, self.dbr)

    def code(self, address, size):
        if VECTOR <= address and address + size <= VECTOR + len(self.vector):
            return bytes(self.vector[address - VECTOR:address - VECTOR + size])
        return super().code(address, size)

    def read(self, address):
        assert address & 3 == 0, hex(address)
        self.reads.append(address)
        key = canonical(address)
        if key in self.mem:
            return self.mem[key]
        self.literal_reads.add(address)
        return struct.unpack('<I', self.code(address, 4))[0]

    def write(self, address, value):
        assert address & 3 == 0, hex(address)
        key = canonical(address)
        assert canonical(self.state_address) <= key < canonical(self.state_address) + self.state_bytes, (
            'exception gateway wrote outside owned private state', hex(address))
        self.writes.append(address)
        self.mem[key] = value & MASK

    def plain(self, word, pc):
        self.executed.add(pc)
        n = word >> 8 & 15
        control = {0x003A: self.sgr, 0x0042: self.spc, 0x0032: self.ssr,
                   0x0022: self.vbr, 0x00FA: self.dbr}
        if word & 0xF0FF in control:
            self.r[n] = control[word & 0xF0FF]
            return
        if word & 0xF0FF == 0x400E:
            target = self.r[n]
            if (target ^ self.sr) & 0x20000000:
                self.r[:8], self.bank = self.bank, self.r[:8]
            self.sr = target
            return
        if word & 0xF0FF == 0x4021:  # SHAR, including T = bit shifted out.
            value = self.r[n]
            self.t(value & 1)
            self.r[n] = ((value >> 1) | (value & 0x80000000)) & MASK
            return
        if word & 0xF0FF == 0x4001:  # SHLR
            self.t(self.r[n] & 1)
            self.r[n] >>= 1
            return
        if word & 0xF0FF == 0x4028:  # SHLL16
            self.r[n] = self.r[n] << 16 & MASK
            return
        if word & 0xF0FF == 0x4018:  # SHLL8
            self.r[n] = self.r[n] << 8 & MASK
            return
        if word >> 8 == 0xCB:
            self.r[0] |= word & 255
            return
        return super().plain(word, pc)

    def run(self):
        # Machine executes branch delay slots before changing PC. Stop before
        # original SDK stack access, or before the separately audited terminal.
        for _ in range(160):
            if self.pc in (CONTINUE, self.terminal):
                return self
            pc = self.pc
            word = struct.unpack('<H', self.code(pc, 2))[0]
            self.executed.add(pc)
            if word >> 12 in (0xA, 0xB) or word & 0xF0FF in (0x400B, 0x402B) or word == 0x000B:
                if word >> 12 in (0xA, 0xB):
                    d = word & 4095
                    d -= 4096 if d & 2048 else 0
                    target = pc + 4 + 2*d
                    if word >> 12 == 0xB:
                        self.pr = pc + 4
                elif word == 0x000B:
                    target = self.pr
                else:
                    target = self.r[word >> 8 & 15]
                    if word & 0xF0FF == 0x400B:
                        self.pr = pc + 4
                self.plain(struct.unpack('<H', self.code(pc + 2, 2))[0], pc + 2)
                self.pc = target
            elif word >> 8 in (0x89, 0x8B, 0x8D, 0x8F):
                d = word & 255
                d -= 256 if d & 128 else 0
                taken = bool(self.sr & 1) == (word >> 8 in (0x89, 0x8D))
                delayed = word >> 8 in (0x8D, 0x8F)
                if delayed:
                    self.plain(struct.unpack('<H', self.code(pc + 2, 2))[0], pc + 2)
                self.pc = pc + 4 + 2*d if taken else pc + (4 if delayed else 2)
            else:
                self.plain(word, pc)
                self.pc += 2
        raise AssertionError('exception gateway did not reach original vector or terminal')

    def check(self):
        self.run()
        assert self.vbr == VBR
        assert self.bank == self.before[1]
        assert (self.pr, self.mach, self.macl, self.gbr, self.fpscr, self.fr,
                self.spc, self.ssr, self.sgr, self.vbr, self.dbr) == (
                self.before[2:6] + self.before[7:])
        if self.captured:
            assert self.pc == self.terminal
            assert self.r[4:8] == [0x10000 | self.event, self.spc, self.ssr, self.tea]
        else:
            assert self.pc == CONTINUE
            assert self.snapshot() == self.before
        return self


class FaultPublish(Machine):
    def __init__(self, image, ccr):
        super().__init__(image, PUBLISH, 5, 15, 0x8C11FFE0)
        self.r[4:6] = [VECTOR, VECTOR + 0x3C]
        self.mem = {0xFF00001C: ccr}
        self.before = self.r.copy(), self.pr, self.mach, self.macl, self.gbr, self.sr, self.fpscr, self.fr.copy()
        self.events = []
        self.ici_pc = None
        self.ici_nops = 0

    def code(self, address, size):
        return super().code(canonical(address), size)

    def write(self, address, value):
        assert address == 0xFF00001C
        assert self.current_pc & 0xE0000000 == 0xA0000000, 'CCR.ICI write must execute from P2'
        assert self.events == [('ocbp', VECTOR), ('ocbp', VECTOR + 32)]
        self.events.append(('ccr', value))
        self.mem[address] = value
        self.ici_pc = self.current_pc

    def plain(self, word, pc):
        self.current_pc = pc
        if self.ici_pc is not None and self.ici_pc < pc <= self.ici_pc + 16:
            assert word == 9 and pc & 0xE0000000 == 0xA0000000
            self.ici_nops += 1
        if word & 0xF0FF == 0x00A3:
            assert pc & 0xE0000000 == 0xA0000000, ('cache publication body must execute from P2', hex(pc))
            self.events.append(('ocbp', self.r[word >> 8 & 15]))
            return
        super().plain(word, pc)

    def check(self):
        self.run()
        initial = self.before
        assert self.r[8:] == initial[0][8:]
        assert (self.pr, self.mach, self.macl, self.gbr, self.sr, self.fpscr, self.fr) == initial[1:]
        assert self.events == [('ocbp', VECTOR), ('ocbp', VECTOR + 32), ('ccr', initial_ccr := self.mem[0xFF00001C])]
        assert initial_ccr & 0x800
        assert self.ici_nops == 8
        return self


def cases(image, *, body=None, veneer=None, compact=False):
    checked = []
    seeds = (0, 1, 7) if compact else range(12)
    # Include unrelated/legitimate events, both register-bank ownership states,
    # and completely unusable interrupted stacks. No owner-SP access is legal.
    events = EVENTS + (0x40, 0x60, 0x80, 0xA0, 0xC0, 0x120, 0x160, 0x1E0, 0x800, 0x820)
    for event in events:
        for custom in ((False, True) if event in EVENTS else (False,)):
            for bank in (0, 1):
                for seed in seeds:
                    sp = (0, 0xFFFFFFF0, 0x8C2F0000)[seed % 3]
                    checked.append(FaultGateway(image, event, seed, sp, bank, custom,
                                               body=body, veneer=veneer).check())
    return checked


def audit(image, self_test=False):
    checked = cases(image)
    publication_cases = 0
    for ccr in (0, 1, 0x105, 0x121, 0x90D):
        published = FaultPublish(image, ccr).check()
        assert published.mem[0xFF00001C] == ccr | 0x800
        publication_cases += 1
    mutations = 0
    redundant = []
    if self_test:
        base, body, _ = symbol_bytes(image, GATEWAY, 2)
        executed = set().union(*(c.executed for c in checked))
        # NOP each actually executed instruction. A mutation is meaningful
        # only if at least one behavioral case detects its removal.
        for address in sorted(executed):
            if not base <= address < base + len(body):
                continue
            at = address - base
            if body[at:at+2] == b'\x09\x00':
                continue
            changed = body[:at] + b'\x09\x00' + body[at+2:]
            try:
                cases(image, body=changed, compact=True)
            except (AssertionError, ValueError, KeyError, struct.error):
                mutations += 1
            else:
                # Restoring SGR immediately after saving scratch is harmless
                # but redundant: no instruction uses R15 before either exit
                # replaces it. The final delay-slot restore is essential.
                assert struct.unpack_from('<H', body, at)[0] == 0x0F3A and at < 16, (
                    'undetected gateway instruction mutation', hex(address))
                redundant.append(hex(address))
        # Explicitly prove veneer scratch preservation and PC-relative literal
        # location rather than replacing the low-vector gateway in the model.
        good = bytes(checked[0].vector)
        for at in (0, 2, 4, 0x38, 0x39, 0x3A, 0x3B):
            changed = bytearray(good)
            changed[at] ^= 1
            try:
                cases(image, veneer=changed, compact=True)
            except (AssertionError, ValueError, KeyError, struct.error):
                mutations += 1
            else:
                raise AssertionError('undetected low veneer mutation at ' + hex(at))
    return {
        'schema': 'kui.native.fault-capture-gateway.v1',
        'instruction_cases': len(checked), 'mutation_rejections': mutations,
        'redundant_early_stack_restores': redundant,
        'publication_cases': publication_cases,
        'publication_from_p2_after_two_operand_cache_purges': True,
        'selected_expevt': [hex(e) for e in EVENTS],
        'nonfatal_exact_gpr_and_sr_preservation': True,
        'changed_sdk_callback_chained': True,
        'interrupted_stack_accesses': 0,
        'new_timer_or_irq_handler': False,
        'vbr_replaced': False,
        'hardware_freeze_reproduced': False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('worker', type=Path)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = audit(read_elf(args.worker), args.self_test)
    wire = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(wire)
    print(wire, end='')


if __name__ == '__main__':
    main()
