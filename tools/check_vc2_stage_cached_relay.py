#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked VC2 relay's bounded SH cache/owner-state interface.

The original C relay is an adversarial ABI boundary. The complete RAM purge
loop is accelerated only after checking its actual four controlling opcodes
and both bounds. This is an instruction/ABI model, not a cache-capacity,
storage, gameplay, cycle-count or hardware-speed claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct

from check_disc_vmu_handoff import Image
from check_vmu_native_worker_gateway import read_elf

MASK = 0xffffffff
CCR = 0xff00001c
ENTRY = '_kui_retail_game_resume'
RELAY = '_kui_retail_stage_relay'
MARKER = '__kui_vc2_stage_cached_relay'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def canonical(address):
    return address & ~0x20000000 if address >> 24 in (0x8c, 0xac) else address


def trampoline(image):
    pc = image.symbol('__retail_trampoline_start')
    expected = [0x2f06+(register<<4) for register in range(15)]
    expected += [0x4f22, 0x4f03, 0x4f23, 0x4f13, 0x4f02, 0x4f12,
                 0x0002, 0xcbf0, 0x400e]
    actual = struct.unpack('<24H', image.read(pc, 48))
    require(actual == tuple(expected), 'Owner trampoline frame/masking changed')
    load = struct.unpack('<H', image.read(pc+48, 2))[0]
    require(load & 0xff00 == 0xd000 and image.read(pc+50, 4) == bytes.fromhex('2b400900'),
            'Owner trampoline relay load/jump/delay slot changed')
    literal = ((pc+52) & ~3) + (load & 255)*4
    target = struct.unpack('<I', image.read(literal, 4))[0]
    require(target == image.symbol(ENTRY) | 0x20000000, 'Owner trampoline must enter relay through P2')
    return target, literal


class Relay:
    def __init__(self, image, owner_ccr, seed, enabled):
        self.image, self.owner_ccr, self.enabled = image, owner_ccr, enabled
        rng = random.Random(seed)
        self.owner = [rng.getrandbits(32) for _ in range(15)]
        self.r = [rng.getrandbits(32) for _ in range(16)]
        self.frame = 0xac00f3ac
        self.r[15] = self.frame
        self.pc, entry_literal = trampoline(image)
        self.macl, self.mach, self.gbr, self.vbr = [rng.getrandbits(32) for _ in range(4)]
        self.sr, self.pr = 0x700000f1, 0xac123456
        self.expected_special = (self.macl, self.mach, self.gbr, self.vbr, self.sr, self.pr)
        words = list(self.expected_special) + list(reversed(self.owner))
        self.mem = {canonical(self.frame) + i*4: value for i, value in enumerate(words)}
        self.ccr = owner_ccr
        self.writes, self.calls, self.purges, self.literal_reads, self.nop_sites = [], [], [], [], []
        self.literal_reads.append(entry_literal)
        self.nops_pending = 0
        self.owner_sp = self.frame + 84
        self.code_fetches = 0

    def code(self, address, size):
        return self.image.read(canonical(address), size)

    def read(self, address):
        require(address & 3 == 0, 'Unaligned relay read')
        if address == CCR:
            return self.ccr
        key = canonical(address)
        if key in self.mem:
            return self.mem[key]
        self.literal_reads.append(key)
        return struct.unpack('<I', self.code(key, 4))[0]

    def write(self, address, value):
        require(address == CCR, 'Unexpected relay store outside CCR')
        require(self.pc >> 24 == 0xac, 'CCR change must execute through P2')
        require(not self.nops_pending, 'Unsynchronized previous CCR write')
        if not self.writes:
            require(self.purges == [(0x8c000000, 0x8d000000, 32)], 'Incomplete pre-relay RAM publication')
            require(value == (0x908 if self.enabled else 0x808), 'Wrong relay cache-enable literal')
        else:
            require(len(self.writes) == 1 and len(self.calls) == 1, 'Wrong CCR restore ordering')
            require(value == (self.owner_ccr | 0x808), 'Owner CCR enable/mode bits changed')
        self.writes.append((self.pc, value))
        # OCI/ICI are write-triggered invalidations, not persistent mode bits.
        self.ccr = value & ~0x808
        self.nops_pending = 8

    def plain(self, op, pc):
        n, m = op >> 8 & 15, op >> 4 & 15
        if self.nops_pending:
            require(op == 9, 'CCR write lacks eight consecutive synchronization NOPs')
            self.nop_sites.append(canonical(pc))
            self.nops_pending -= 1
            return
        if op == 9:
            return
        if op & 0xf000 == 0xd000:
            self.r[n] = self.read(((pc+4) & ~3) + 4*(op & 255))
        elif op & 0xf00f == 0x6003:
            self.r[n] = self.r[m]
        elif op & 0xf00f == 0x6002:
            self.r[n] = self.read(self.r[m])
        elif op & 0xf00f == 0x6006:
            self.r[n] = self.read(self.r[m]); self.r[m] = (self.r[m]+4) & MASK
        elif op & 0xf00f == 0x2002:
            self.write(self.r[n], self.r[m])
        elif op & 0xf00f == 0x200b:
            self.r[n] |= self.r[m]
        elif op & 0xf000 == 0x7000:
            value = op & 255; value -= 256 if value & 128 else 0
            self.r[n] = (self.r[n]+value) & MASK
        elif op in (0x4f16, 0x4f06, 0x4f17, 0x4f27, 0x4f26):
            name = {0x4f16:'macl', 0x4f06:'mach', 0x4f17:'gbr', 0x4f27:'vbr', 0x4f26:'pr'}[op]
            setattr(self, name, self.read(self.r[15])); self.r[15] = (self.r[15]+4) & MASK
        elif op == 0x400e:
            self.sr = self.r[0]
        else:
            raise ValueError(f'Unsupported/mutated relay instruction {op:04x} at {pc:08x}')

    def run(self):
        for _ in range(160):
            pc = self.pc
            op = struct.unpack('<H', self.code(pc, 2))[0]
            self.code_fetches += 1
            if op == 0x00a3:
                loop = struct.unpack('<4H', self.code(pc, 8))
                require(loop == (0x00a3, 0x7020, 0x3012, 0x8bfb), 'RAM publication loop changed')
                require((self.r[0], self.r[1]) == (0x8c000000, 0x8d000000), 'RAM publication span changed')
                self.purges.append((self.r[0], self.r[1], 32))
                self.r[0] = self.r[1]; self.sr |= 1; self.pc += 8
                continue
            if op in (0x400b, 0x402b):
                target = self.r[0]
                if op == 0x400b:
                    self.pr = pc+4
                self.plain(struct.unpack('<H', self.code(pc+2, 2))[0], pc+2)
                require(not self.nops_pending, 'Branch before CCR synchronization')
                if op == 0x400b:
                    require(target == self.image.symbol(RELAY), 'Wrong C relay target/alias')
                    require(self.ccr == (0x100 if self.enabled else 0), 'Operand cache enabled or instruction policy missing')
                    require(self.r[4:6] == [self.frame, self.owner_ccr] and self.r[15] == 0x8cff0000,
                            'C relay frame/cache/high-stack arguments changed')
                    self.calls.append((target, self.ccr))
                    # C may clobber caller-saved state; the owner frame must
                    # still restore every word and exact final stack below.
                    for register in range(8): self.r[register] = (0x913579bd + register*0x13579bd) & MASK
                    self.macl ^= MASK; self.mach ^= MASK; self.gbr ^= MASK; self.vbr ^= MASK
                    self.sr ^= 1; self.pc = pc+4
                else:
                    require(target == 0xac010000, 'Wrong original owner entry/alias')
                    require(len(self.writes) == 2 and len(self.calls) == 1, 'Missing relay/cache transition')
                    require(self.r[:15] == self.owner and self.r[15] == self.owner_sp, 'Owner GPR/SP restoration failed')
                    require((self.macl, self.mach, self.gbr, self.vbr, self.sr, self.pr) == self.expected_special,
                            'Owner special-register restoration failed')
                    require(self.ccr == (self.owner_ccr & ~0x808), 'Owner CCR mode restoration failed')
                    return self
            else:
                self.plain(op, pc); self.pc += 2
        raise ValueError('Unbounded relay instruction path')


def audit(path, enabled=True, self_test=True, marker_name=MARKER):
    raw = Path(path).read_bytes(); image = Image(raw)
    marker = read_elf(Path(path))[4].get(marker_name)
    require(marker == (1, 0, 0, 0xfff1) if enabled else marker is None,
            'VC2 stage-only cache marker mismatch')
    cases = 0; first = None
    for owner in (0, 1, 0x100, 0x105, 0x121, 0x85):
        for seed in (0, 7, 31):
            run = Relay(image, owner, seed, enabled).run(); cases += 1
            if first is None: first = run
    mutants = 0
    if self_test:
        # Mutate controlling literals and instructions, not unrelated text.
        addresses = []
        for address in set(first.literal_reads):
            value = struct.unpack('<I', image.read(address, 4))[0]
            if value in (0x908 if enabled else 0x808, 0x808, image.symbol(RELAY),
                         image.symbol(ENTRY) | 0x20000000, 0x8d000000):
                addresses.append((address, 4))
        addresses.extend((address, 2) for address in first.nop_sites)
        for address, size in addresses:
            changed = bytearray(raw); at = image.offset(address, size); changed[at] ^= 1
            try:
                Relay(Image(bytes(changed)), 0x100, 7, enabled).run()
            except (ValueError, struct.error):
                mutants += 1
            else:
                raise ValueError(f'Relay mutation survived at {address:08x}')
        relay_literal = next(address for address in first.literal_reads
                             if struct.unpack('<I', image.read(address, 4))[0] == image.symbol(RELAY))
        explicit = ((relay_literal, struct.pack('<I', image.symbol(RELAY) | 0x20000000)),
                    (canonical(first.writes[1][0])-4, struct.pack('<H', 9)))
        for address, replacement in explicit:
            changed = bytearray(raw); at = image.offset(address, len(replacement))
            changed[at:at+len(replacement)] = replacement
            try: Relay(Image(bytes(changed)), 0x100, 7, enabled).run()
            except (ValueError, struct.error): mutants += 1
            else: raise ValueError(f'Relay alias/owner-OR mutation survived at {address:08x}')
        # Correct literal with a cached executing alias is still invalid.
        wrong_alias = Relay(image, 0x105, 7, enabled); wrong_alias.pc &= ~0x20000000
        try: wrong_alias.run()
        except ValueError: mutants += 1
        else: raise ValueError('Cached CCR-writing alias mutation survived')
    return dict(status='PASS', linked_elf_sha256=hashlib.sha256(raw).hexdigest(),
        owner_state_cases=cases, rejected_controlling_mutations=mutants,
        relay_instruction_cache_enabled=enabled, relay_operand_cache_enabled=False,
        complete_pre_relay_ram_publication=True, ccr_writes_through_p2=True,
        original_trampoline_frame_and_p2_target_checked=True,
        eight_nops_after_each_ccr_write=True, exact_owner_frame_and_ccr_restored=True,
        c_relay_body_executed=False, real_cpu_cache_or_timing_measured=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path); parser.add_argument('--output', type=Path)
    parser.add_argument('--expect-off', action='store_true')
    args = parser.parse_args(); result = audit(args.elf, not args.expect_off)
    text = json.dumps(result, indent=2)+'\n'
    if args.output: args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_text(text)
    print(text, end='')


if __name__ == '__main__': main()
