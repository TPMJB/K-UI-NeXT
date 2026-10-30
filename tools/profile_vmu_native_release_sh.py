#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Count actual linked SH release-validation work, never estimate wall time.

The bounded integer interpreter executes the linked C functions and counts
instructions and uncached P2 data accesses. The generic coherent guest reader
is folded into an equivalent counted RAM read/output store. Worker mode also
folds the common stack checker and installed-hook checker to successful returns,
isolating lifetime validation at its actual inline worker call site. The
worker-with-hooks mode executes the complete hook checker and folds only the
common stack checker and generic guest reader. Hooks mode executes its entire
checker. Diagnostic sampling is disabled, as in normal installations. The stub
bodies' own instructions are omitted, so these are comparable core-work counts,
not SH-4 cycle counts, cache simulation, or measured console performance.
No game executable bytes are needed or embedded.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from test_vmu_native_heap_execution import SH, MASK, signed
from check_vmu_native_worker_gateway import read_elf

READER = 0x8CFF0000
STACK = 0xAC7F0000
LEASE = 0xAC500000
RANGES = 0xAC500200
PLAN = 0xAC500800
FRAME = 0xAC500500


class ReleaseSH(SH):
    def __init__(self, path):
        super().__init__(b"")
        self.elf = read_elf(path)
        data, _, segments, _, symbols = self.elf
        self.symbols = symbols
        for kind, offset, address, _, stored, memory, _, _ in segments:
            if kind == 1:
                self.store_block(address, data[offset:offset + stored] + bytes(memory - stored))
        self.active = False
        self.guest_words = set()
        self.stubs = {}
        self.macl = 0
        self.private_reads = self.private_writes = self.guest_reads = 0

    def read(self, address, count=4):
        if self.active:
            if (address & 0x1FFFFFFF) & ~3 in self.guest_words:
                self.guest_reads += 1
            elif address & 0xE0000000 == 0xA0000000:
                self.private_reads += 1
        return super().read(address, count)

    def write(self, address, value, count=4):
        if self.active and address & 0xE0000000 == 0xA0000000:
            self.private_writes += 1
        super().write(address, value, count)

    def plain(self, op, pc):
        n, m = (op >> 8) & 15, (op >> 4) & 15
        if op & 0xFF00 == 0x8000:  # mov.b R0,@(disp,Rm)
            self.write(self.r[m] + (op & 15), self.r[0], 1)
        elif op & 0xFF00 == 0x8100:  # mov.w R0,@(disp,Rm)
            self.write(self.r[m] + (op & 15) * 2, self.r[0], 2)
        elif op & 0xFF00 == 0x8500:  # mov.w @(disp,Rm),R0
            self.r[0] = signed(self.read(self.r[m] + (op & 15) * 2, 2), 16) & MASK
        elif op & 0xF00F == 0x2000:  # mov.b Rm,@Rn
            self.write(self.r[n], self.r[m], 1)
        elif op & 0xF00F == 0x6000:  # mov.b @Rm,Rn
            self.r[n] = signed(self.read(self.r[m], 1), 8) & MASK
        elif op & 0xF00F == 0x6004:  # mov.b @Rm+,Rn
            value = signed(self.read(self.r[m], 1), 8) & MASK
            if n != m:
                self.r[m] = self.r[m] + 1 & MASK
            self.r[n] = value
        elif op & 0xF00F == 0x6005:  # mov.w @Rm+,Rn
            value = signed(self.read(self.r[m], 2), 16) & MASK
            if n != m:
                self.r[m] = self.r[m] + 2 & MASK
            self.r[n] = value
        elif op & 0xF00F == 0x6007:  # not Rm,Rn
            self.r[n] = ~self.r[m] & MASK
        elif op & 0xF00F == 0x600A:  # negc Rm,Rn
            value = self.r[m] + (self.sr & 1)
            self.r[n] = -value & MASK
            self.flag(value != 0)
        elif op & 0xF00F == 0x600B:  # neg Rm,Rn
            self.r[n] = -self.r[m] & MASK
        elif op & 0xF00F == 0x600C:  # extu.b Rm,Rn
            self.r[n] = self.r[m] & 255
        elif op & 0xF00F == 0x200A:  # xor Rm,Rn
            self.r[n] ^= self.r[m]
        elif op & 0xF00F == 0x000E:  # mov.l @(R0,Rm),Rn
            self.r[n] = self.read((self.r[0] + self.r[m]) & MASK)
        elif op & 0xF00F == 0x000D:  # mov.w @(R0,Rm),Rn
            self.r[n] = signed(self.read((self.r[0] + self.r[m]) & MASK, 2), 16) & MASK
        elif op & 0xF00F == 0x0005:  # mov.w Rm,@(R0,Rn)
            self.write((self.r[0] + self.r[n]) & MASK, self.r[m], 2)
        elif op & 0xF00F == 0x0007:  # mul.l Rm,Rn
            self.macl = self.r[n] * self.r[m] & MASK
        elif op & 0xF0FF == 0x001A:  # sts MACL,Rn
            self.r[n] = self.macl
        elif op & 0xF0FF == 0x0029:  # movt Rn
            self.r[n] = self.sr & 1
        elif op & 0xF0FF == 0x4010:  # dt Rn
            self.r[n] = self.r[n] - 1 & MASK
            self.flag(self.r[n] == 0)
        elif op & 0xF0FF == 0x4028:  # shll16 Rn
            self.r[n] = self.r[n] << 16 & MASK
        elif op & 0xF0FF == 0x4029:  # shlr16 Rn
            self.r[n] >>= 16
        elif op & 0xF00F == 0x400D:  # shld Rm,Rn
            shift = self.r[m]
            if shift & 0x80000000:
                count = (-shift) & 31
                self.r[n] = self.r[n] >> count if count else 0
            else:
                self.r[n] = self.r[n] << (shift & 31) & MASK
        elif op & 0xF00F == 0x400C:  # shad Rm,Rn
            shift = self.r[m]
            if shift & 0x80000000:
                count = (-shift) & 31 or 32
                self.r[n] = signed(self.r[n], 32) >> count & MASK
            else:
                self.r[n] = self.r[n] << (shift & 31) & MASK
        elif op & 0xFF00 == 0xC800:  # tst #imm,R0
            self.flag(not (self.r[0] & (op & 255)))
        elif op & 0xFF00 == 0xCA00:  # xor #imm,R0
            self.r[0] ^= op & 255
        elif op & 0xFF00 == 0x8800:  # cmp/eq #imm,R0
            self.flag(self.r[0] == (signed(op & 255, 8) & MASK))
        else:
            return super().plain(op, pc)
        self.executing = pc
        self.instructions += 1

    def call(self, name, args, *, measure=True):
        entry = self.symbols[name][0]
        saved = self.r[8:15]
        self.pc, self.pr, self.r[15] = entry, 0xDEADBEEE, STACK
        for i, value in enumerate(args):
            if i < 4:
                self.r[4 + i] = value
            else:
                self.write(STACK + (i - 4) * 4, value)
        self.instructions = self.private_reads = self.private_writes = self.guest_reads = 0
        self.active = measure
        for _ in range(2000000):
            pc = self.pc
            if pc == 0xDEADBEEE:
                assert self.r[15] == STACK and self.r[8:15] == saved
                self.active = False
                return {"result": self.r[0], "instructions": self.instructions,
                        "private_p2_reads": self.private_reads,
                        "private_p2_writes": self.private_writes,
                        "guest_reads": self.guest_reads}
            if pc == self.symbols.get("_fail", (None,))[0]:
                raise AssertionError(f"Actual worker refused with code {self.r[4]:#x}")
            if pc in self.stubs:
                self.r[0], self.pc = self.stubs[pc], self.pr
                continue
            if pc == READER or pc == self.symbols.get("_read_heap", (None,))[0]:
                self.guest_reads += 1
                active, self.active = self.active, False
                value = self.read(self.r[5])
                self.active = active
                self.write(self.r[6], value)
                self.r[0], self.pc = 1, self.pr
                continue
            op = self.read(pc, 2)
            target, delay, n = None, False, (op >> 8) & 15
            if op >> 12 in (0xA, 0xB):
                target, delay = pc + 4 + 2 * signed(op & 0xFFF, 12) & MASK, True
                if op >> 12 == 0xB:
                    self.pr = pc + 4
            elif op & 0xF0FF in (0x400B, 0x402B):
                target, delay = self.r[n], True
                if op & 0xF0FF == 0x400B:
                    self.pr = pc + 4
            elif op == 0x000B:
                target, delay = self.pr, True
            elif op >> 8 in (0x89, 0x8B, 0x8D, 0x8F):
                taken = bool(self.sr & 1) == (op >> 8 in (0x89, 0x8D))
                delay = op >> 8 in (0x8D, 0x8F)
                target = pc + 4 + 2 * signed(op & 255, 8) if taken else pc + (4 if delay else 2)
            if target is None:
                self.plain(op, pc)
                self.pc = pc + 2
            else:
                self.instructions += 1
                if delay:
                    self.plain(self.read(pc + 2, 2), pc + 2)
                self.pc = target & MASK
        raise AssertionError("Release model exceeded its bounded path")


def fixture(cpu, nodes, ranges):
    begin, heap_bytes, lease_bytes = 0x8C600000, 0x100000, 0x30000
    payload = begin + heap_bytes - lease_bytes
    sentinel, table = 0x8C400000, 0x8C011100
    owner = [1, 72, 1, 1, 0x200000, 0x8C010000, 0x8C013400, 0x8C0133F0,
             0x8C013400, 0x8C0134E2, 0x8C0135B0, sentinel - 4, table,
             sentinel, sentinel + 0x20, sentinel + 0x24, sentinel + 0x28, sentinel + 0x2C]
    lease = [1, 116, 2, 1, *owner, begin, heap_bytes, lease_bytes, payload - 32,
             payload, lease_bytes // 32 + 1, heap_bytes // 32 - lease_bytes // 32 - 1]
    cpu.store_block(LEASE, struct.pack("<29I", *lease))
    for address, value in ((sentinel - 4, table), (table, owner[9]), (table + 4, owner[10]),
                           (sentinel + 4, 0), (sentinel + 0x20, sentinel),
                           (sentinel + 0x24, begin), (sentinel + 0x28, heap_bytes),
                           (sentinel + 0x2C, begin), (payload - 28, lease[-2])):
        cpu.write(address, value)
    cpu.write(sentinel, begin)
    for i in range(nodes):
        at = begin + i * 64
        cpu.write(at, sentinel if i + 1 == nodes else at + 64)
        cpu.write(at + 4, 1)
    candidate = begin + 0x10020
    cpu.write(candidate - 28, 2)
    assert nodes * 64 <= 0x10000
    for i in range(ranges):
        cpu.write(RANGES + i * 8, 0x8C300000 + i * 0x800)
        cpu.write(RANGES + i * 8 + 4, 1024)
    cpu.guest_words = {address & 0x1FFFFFFF for address in
                       (sentinel - 4, table, table + 4, sentinel, sentinel + 4,
                        sentinel + 0x20, sentinel + 0x24, sentinel + 0x28,
                        sentinel + 0x2C, payload - 28, candidate - 28)}
    cpu.guest_words.update((begin + i * 64 + word) & 0x1FFFFFFF
                           for i in range(nodes) for word in (0, 4))
    return candidate


def worker_fixture(cpu, candidate, ranges):
    """Prepare fixed-width worker ABI state without substituting the validator."""
    config, size = cpu.symbols["_config"][:2]
    # All fields through the fixed 116-byte lease are uint32_t/byte arrays.
    # These assertions bind this fixture to the persistent worker ABI.
    assert size == 2304
    cpu.store_block(config + 116, cpu.block(LEASE, 116))
    cpu.write(cpu.symbols["_state"][0], 1)
    cpu.write(cpu.symbols["_kui_vmu_native_readonly_release_resume"][0], 0x8C0135B0 + 20)
    cpu.write(FRAME + 56, candidate)
    cpu.stubs = {cpu.symbols[name][0]: 1
                 for name in ("_stack_valid", "_installed_hooks_valid")}
    if "_lifetime_plan" in cpu.symbols:
        plan = cpu.symbols["_lifetime_plan"][0]
        result = cpu.call("_kui_vmu_native_lifetime_prepare",
                          [plan, LEASE, READER, 0, 1, RANGES, ranges], measure=False)
        assert result["result"] == 1
        # This fixture deliberately places every protected range outside the
        # heap, as in the SA2 fixed-arena profile. The constructor must clip all.
        assert cpu.read(plan + 8) == 0
    else:
        cpu.write(cpu.symbols["_protected_count"][0], ranges)
        cpu.store_block(cpu.symbols["_protected_ranges"][0], cpu.block(RANGES, ranges * 8))


def hook_fixture(cpu, exits):
    """Use actual SH encoders/preparation and their exact live patch bytes."""
    config, size = cpu.symbols["_config"][:2]
    assert size == 2304 and 0 <= exits <= 4
    cpu.store_block(config + 116, cpu.block(LEASE, 116))
    submit, complete_slot = 0x8C021000, 0x8C021100
    low_terminal, low_heap_hook = 0x8C008000, 0x8C008100
    cpu.write(config + 36, submit)
    cpu.write(config + 60, complete_slot)
    cpu.write(config + 2240, low_terminal)
    cpu.write(config + 2244, low_heap_hook)
    cpu.write(config + 2268, exits)
    entries = [0x8C022000, 0x8C022102, 0x8C022200, 0x8C022302][:exits]
    for i, address in enumerate(entries):
        cpu.write(config + 2272 + i * 4, address)
    exports = cpu.symbols["_kui_vmu_native_worker_exports"][0]
    hooks = [(submit, cpu.read(exports + 16)),
             (0x8C0135B0, cpu.read(exports + 56)),
             (0x8C013400, low_terminal),
             *((address, low_terminal) for address in entries)]
    slots = [(0x8C0133F0, low_heap_hook), (complete_slot, cpu.read(exports + 20))]
    witness_words = set()
    for address, target in hooks:
        result = cpu.call("_kui_vmu_native_patch_encode", [address, target, FRAME], measure=False)
        assert result["result"] == 1
        cpu.store_block(address, cpu.block(FRAME, 12))
        witness_words.update(range((address & 0x1FFFFFFF) & ~3,
                                   ((address & 0x1FFFFFFF) + 11 & ~3) + 4, 4))
    for address, value in slots:
        cpu.write(address, value)
        witness_words.add(address & 0x1FFFFFFF)
    cpu.guest_words = witness_words
    cpu.stubs = {}
    if "_hook_witness" in cpu.symbols:
        for address, target in hooks:
            result = cpu.call("_prepare_hook_entry", [address, target], measure=False)
            assert result["result"] == 1
        for address, value in slots:
            result = cpu.call("_hook_word", [address, value, MASK], measure=False)
            assert result["result"] == 1
        cpu.write(cpu.symbols["_hook_witness_ready"][0], 0x4E485031)
    protected_bytes = {address + i for address, _ in hooks for i in range(12)}
    protected_bytes.update(address + i for address, _ in slots for i in range(4))
    adjacent_bytes = {address + i for address, _ in hooks if address & 2 for i in (-2, -1, 12, 13)}
    assert not adjacent_bytes & protected_bytes
    return protected_bytes, adjacent_bytes


def check_hook_corruption(cpu, protected, adjacent):
    """Every covered live byte matters; masked neighboring bytes must not."""
    for address in sorted(protected | adjacent):
        original = cpu.read(address, 1)
        cpu.write(address, original ^ 0x80, 1)
        admitted = cpu.call("_installed_hooks_valid", [], measure=False)["result"]
        assert admitted == (address in adjacent), hex(address)
        cpu.write(address, original, 1)
    return {"bit_flipped": 128, "protected_bytes_rejected": len(protected),
            "adjacent_bytes_admitted": len(adjacent)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--nodes", type=int, nargs="+", default=[1, 8, 64, 256, 1023])
    parser.add_argument("--ranges", type=int, default=60)
    parser.add_argument("--kind", choices=("full", "worker", "worker-with-hooks", "hooks"), default="full")
    parser.add_argument("--exits", type=int, default=3)
    parser.add_argument("--check-hook-corruption", action="store_true",
                        help="Check every covered byte and unaligned neighboring byte after counting")
    args = parser.parse_args()
    for count in ([1] if args.kind == "hooks" else args.nodes):
        assert 1 <= count < 1024 and 0 <= args.ranges <= 64
        cpu = ReleaseSH(args.build / "native-worker.elf")
        candidate = fixture(cpu, count, args.ranges)
        if args.kind == "hooks":
            protected, adjacent = hook_fixture(cpu, args.exits)
            result = cpu.call("_installed_hooks_valid", [])
            assert result["result"] == 1
            corruption = None
            if args.check_hook_corruption:
                corruption = check_hook_corruption(cpu, protected, adjacent)
        elif args.kind in ("worker", "worker-with-hooks"):
            worker_fixture(cpu, candidate, args.ranges)
            if args.kind == "worker-with-hooks":
                lifetime_words = cpu.guest_words.copy()
                hook_fixture(cpu, args.exits)
                cpu.guest_words.update(lifetime_words)
                cpu.stubs = {cpu.symbols["_stack_valid"][0]: 1}
            result = cpu.call("_kui_vmu_native_readonly_release_check", [FRAME])
            result["result"] = 1  # A successful void return, without reaching fail.
        else:
            result = cpu.call("_kui_vmu_native_lifetime_release_safe_ranges",
                              [LEASE, READER, 0, 1, candidate, RANGES, args.ranges])
            assert result["result"] == 1
        profile = {"kind": args.kind, "elf_sha256": hashlib.sha256(cpu.elf[0]).hexdigest(), **result}
        if args.kind == "hooks":
            profile.update(exits=args.exits, covered_bytes=len(protected),
                           corruption=corruption)
        else:
            profile.update(nodes=count, ranges=args.ranges)
            if args.kind == "worker-with-hooks":
                profile["exits"] = args.exits
        print(json.dumps(profile))


if __name__ == "__main__":
    main()
