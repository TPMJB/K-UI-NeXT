#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked Smash native image-proof cache wrapper.

The exact zero-argument C proof is replaced by an ABI-conforming adversary.
Its private output and P1 stack writes remain dirty until the actual wrapper
publishes RAM. Only the pinned four-instruction whole-RAM purge loop is
folded; no cache timing, SDK, media or console speed is modeled.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "tests")]
from check_vmu_native_worker_gateway import CPU, MASK, read_elf, symbol_bytes
from check_vmu_native_cache_gateway import CacheGateway

WRAPPER = "_kui_smash_native_menu_cached_resolution_replay"
HELPER = "_kui_smash_native_menu_resolution_replay"
MARKER = "__kui_smash_native_menu_cached_resolution"
CCR, RAM, END, STACK = 0xff00001c, 0x8c000000, 0x8d000000, 0x8cff0000
PURGE_OPS = struct.pack("<4H", 0x00a3, 0x7020, 0x3012, 0x8bfb)


def require(condition, message):
    if not condition:
        raise ValueError(message)


class ReplayGateway(CacheGateway):
    def __init__(self, stage, seed, mask, ccr, result, body=None):
        begin, linked, _ = symbol_bytes(stage, WRAPPER, 2)
        CPU.__init__(self, seed, begin, 1)
        self.blob = linked if body is None else body
        self.pc, self.r[15], self.mem = begin, STACK, {}
        self.start_sp = STACK
        self.sr = (self.sr & ~0xf0) | mask << 4
        self.pr = begin + len(self.blob) + 0x100
        self.before = (self.r.copy(), self.pr, self.sr, self.fpscr,
                       self.fr.copy(), self.gbr)
        self.owner_ccr, self.ccr, self.result = ccr, ccr, result
        self.calls, self.ccr_writes, self.events = [], [], []
        self.wait, self.purged, self.purge_lines, self.dirty = 0, False, 0, set()
        self.targets = {stage[4][HELPER][0]: "proof"}
        self.output, self.output_bytes, _, _ = stage[4]["_native_cached_resolution"]
        self.expected_output = {}
        for offset in range(0, self.output_bytes, 4):
            self.mem[self.output + offset] = (0xa5a50000 + offset + seed) & MASK

    def code(self, address, size):
        offset = (address - self.base) & MASK
        if offset >= 0x20000000:
            offset -= 0x20000000
        assert 0 <= offset < offset + size <= len(self.blob), "wrapper left its exact linked body"
        return self.blob[offset:offset + size]

    def read(self, address):
        if address == CCR:
            assert self.base + 0x20000000 <= self.pc < self.base + 0x20000000 + len(self.blob)
            return self.ccr
        if address in self.mem:
            assert self.ccr & 1 or address not in self.dirty, "uncached read preceded proof output/stack publication"
            return self.mem[address]
        return struct.unpack("<I", self.code(address, 4))[0]

    def write(self, address, value):
        if address == CCR:
            assert self.base + 0x20000000 <= self.pc < self.base + 0x20000000 + len(self.blob)
            assert not self.wait and self.owner_ccr == 0x100
            expected = 0x909 if not self.ccr_writes else 0x908
            assert value == expected and len(self.ccr_writes) < 2, "unexpected cached replay CCR profile"
            if self.ccr_writes:
                assert self.purged and not self.dirty, "CCR disabled operand cache before proof publication"
            self.ccr_writes.append(value)
            self.events.append(("ccr", value))
            self.ccr, self.wait = value & ~0x808, 8
            return
        assert not address & 3 and self.start_sp - 12 <= address < self.start_sp
        self.mem[address] = value & MASK
        if self.ccr & 1:
            self.dirty.add(address)

    def plain(self, word, pc):
        if self.wait:
            assert word == 9, "CCR update lacks eight following NOPs"
            self.wait -= 1
        assert word >> 12 != 0xf, "cached replay wrapper introduced an FPU instruction"
        if word == 0x00a3:
            assert self.code(pc, 8) == PURGE_OPS, "whole-RAM publication loop changed"
            assert self.r[:2] == [RAM, END] and self.calls == ["proof"]
            assert self.ccr_writes == [0x909] and self.ccr == 0x101 and not self.purged
            # Exact pinned loop: OCBP at RAM + 32*i for every line through
            # END-32, with no omitted line and no address outside main RAM.
            self.purge_lines = (END - RAM) // 32
            self.r[0] = END - 32
            self.purged = True
            self.dirty.clear()
            self.events.append(("publish", self.purge_lines))
            return
        super().plain(word, pc)

    def api(self, target):
        assert target in self.targets and not self.wait and not self.calls
        assert target & 0xff000000 == 0x8c000000, "proof must execute its P1 entry"
        assert self.r[15] == self.start_sp - 12
        assert self.sr & 0xf0 == self.before[2] & 0xf0
        assert self.ccr == (0x101 if self.owner_ccr == 0x100 else self.owner_ccr)
        assert self.ccr_writes == ([0x909] if self.owner_ccr == 0x100 else [])
        self.calls.append("proof")
        self.events.append(("proof", self.result))
        return_pc = self.pr
        # A rejected proof may also have written partial private output.
        # Publication must therefore be correct for both bool outcomes.
        for index in range(1, 33):
            address = self.r[15] - 4 * index
            self.mem[address] = (0x13579bdf + 7 * index) & MASK
            if self.ccr & 1:
                self.dirty.add(address)
        for offset in range(0, self.output_bytes, 4):
            if not self.result and offset & 12:
                continue
            address = self.output + offset
            value = (0x92345678 + offset * 13 + self.result) & MASK
            self.mem[address] = self.expected_output[address] = value
            if self.ccr & 1:
                self.dirty.add(address)
        for index in range(8):
            self.r[index] = (0x913579bd + 0x13579bd * index) & MASK
        self.r[0] = self.result
        self.mach, self.macl = 0xbad0c0de, 0xc001d00d
        self.sr ^= 1
        self.pr ^= 0x2468ace0
        self.pc = return_pc

    def check(self):
        registers, pr, sr, fpscr, fr, gbr = self.before
        assert self.r[8:] == registers[8:] and self.pr == pr and self.r[0] == self.result
        assert self.sr & ~1 == sr & ~1 and self.gbr == gbr
        assert (self.fpscr, self.fr) == (fpscr, fr)
        assert (self.mach, self.macl) == (0xbad0c0de, 0xc001d00d)
        assert self.calls == ["proof"] and self.ccr == self.owner_ccr and not self.wait
        cached = self.owner_ccr == 0x100
        assert self.ccr_writes == ([0x909, 0x908] if cached else [])
        assert self.purged == cached and self.purge_lines == (524288 if cached else 0)
        assert self.events == ([("ccr", 0x909), ("proof", self.result),
                                ("publish", 524288), ("ccr", 0x908)]
                               if cached else [("proof", self.result)])
        assert all(self.read(address) == value for address, value in self.expected_output.items())
        if cached:
            assert not self.dirty


def audit(elf, self_test=True):
    require(__debug__, "cached replay model requires Python assertions; run without -O")
    stage = read_elf(elf)
    require(stage[4].get(MARKER) == (1, 0, 0, 0xfff1), "cached replay marker differs")
    begin, body, _ = symbol_bytes(stage, WRAPPER, 2)
    helper, helper_body, _ = symbol_bytes(stage, HELPER, 2)
    output, size, kind, owner = stage[4]["_native_cached_resolution"]
    section = stage[3][owner]
    require(kind == 1 and size and not size & 3 and section[1] == 8 and section[2] & 7 == 3 and
            section[3] <= output < output + size <= section[3] + section[5] and
            RAM <= stage[4]["__retail_stage_bss_begin"][0] <= output < output + size <=
            stage[4]["__retail_stage_bss_end"][0] <= END,
            "proof output lacks its complete private typed BSS object")
    ops = (0x2f86, 0x2f96, 0x4f22, 0xd019, 0x402b, 9, 0xd118, 0x6012, 0x6803, 0xd218,
           0x3020, 0x8b09, 0xd017, 0x2102, 9, 9, 9, 9, 9, 9, 9, 9, 0xd013, 0x400b, 9, 0x6903,
           0xd00f, 0x3800, 0x8b11, 0xd011, 0xd111, 0x00a3, 0x7020, 0x3012, 0x8bfb,
           0xd010, 0x208b, 0xd109, 0x2102, 9, 9, 9, 9, 9, 9, 9, 9, 0x6093, 0x4f26, 0x69f6,
           0x68f6, 0xb, 9, 9)
    expected = struct.pack("<54H8I", *ops, begin + 12 + 0x20000000, CCR, 0x100, 0x909,
                           helper, RAM, END, 0x808)
    require(body == expected, "actual cached replay wrapper instructions/literals differ")
    ccrs = (0, 1, 4, 0x20, 0x80, 0x100, 0x105, 0x109, 0x909, 0xffffffff)
    cases = 0
    for seed in range(12):
        for mask in range(16):
            for ccr in ccrs:
                for result in (0, 1):
                    ReplayGateway(stage, seed, mask, ccr, result).run().check()
                    cases += 1
    rejected = 0
    if self_test:
        mutations = []
        for needle, replacement in ((struct.pack("<I", 0x909), struct.pack("<I", 0x908)),
                                    (struct.pack("<I", 0x808), struct.pack("<I", 0x809)),
                                    (struct.pack("<I", 0x100), struct.pack("<I", 0x105)),
                                    (PURGE_OPS, struct.pack("<4H", 9, 0x7020, 0x3012, 0x8bfb)),
                                    (struct.pack("<I", helper), struct.pack("<I", helper + 0x20000000))):
            at = body.find(needle)
            require(at >= 0, "missing cached replay mutation target")
            changed = bytearray(body)
            changed[at:at + len(needle)] = replacement
            mutations.append(bytes(changed))
        for at in (*range(28, 44, 2), *range(78, 94, 2)):
            changed = bytearray(body)
            struct.pack_into("<H", changed, at, 0x6003)
            mutations.append(bytes(changed))
        for changed in mutations:
            try:
                ReplayGateway(stage, 7, 15, 0x100, 1, changed).run().check()
            except (AssertionError, struct.error, KeyError):
                rejected += 1
            else:
                raise ValueError("cached replay model accepted an unsafe wrapper mutation")
    require(Path(elf).read_bytes() == stage[0], "child stage changed during cached replay audit")
    return dict(schema="kui.smash-native.cached-replay.v1", passed=True,
                elf_sha256=hashlib.sha256(stage[0]).hexdigest(), entry=hex(begin), wrapper_bytes=len(body),
                helper=hex(helper), helper_sha256=hashlib.sha256(helper_body).hexdigest(),
                actual_instruction_model_cases=cases, mutation_rejections=rejected,
                exact_wrapper_instructions_and_literals_pinned=True,
                no_argument_pure_proof_calls=1, callee_preserved_integer_frame_bytes=12,
                eligible_entry_ccr="0x100", proof_ccr="0x101", restored_ccr="0x100",
                ccr_write_values=["0x909", "0x908"], cache_write_nops=[8, 8],
                ram_publication_lines=524288, output_published_before_operand_cache_restore=True,
                unknown_ccr_profiles_unchanged=True, callee_preserved_integer_and_fpu_state=True,
                limits=["The exact C proof is substituted by an ABI-conforming adversary; its SHA256 and record validation have separate tests.",
                        "Only the pinned whole-RAM OCBP loop is folded, after checking its exact operands and four instructions.",
                        "Cache timing, peripheral timing, SDK calls, VMU saving and console speed are not modeled."])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if not __debug__:
        parser.error("Python assertions must remain enabled")
    result = audit(args.elf)
    rendered = json.dumps(result, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
