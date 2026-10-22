#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the actual relocated native persistent worker, low SCI loan and source.

Integer gateway/ABI models establish the authored control transfers; they do
not establish console execution or universal game/reset compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import random
import shutil
import struct
import subprocess
import tempfile

import retail_package as layout
import check_retail_loader_layout as resident_layout
from check_vmu_native_heap_probe import (
    LOW, HIGH, LIMIT, typed_function, check_bss, inspect_dynamic_hook, regression,
)
from check_vmu_native_readonly import (ReadonlyGateway, relocated, model_hooks, model_mutations)
from check_vmu_native_worker_gateway import (
    MASK, P1, P2, literal_aware_instructions, model_gateway, read_elf, symbol_bytes,
    Gateway as StageGateway, ENTRY as P2_ENTRY, TOP, CONFIG, RESULT,
)
from check_loader_layout import inspect_elf, padded, region
from check_retail_instructions import audit as instruction_audit
from runtime_package import verify
from package_vmu_maple_observe import (linked_stack_rows, _function_base, check_pio_callgraph, linked_function_names)
from check_retail_stack import EDGE, NODE, load as load_graph
from vmu_maple_observe_symbols import sd_gateway_span
from check_vmu_pio_write import check_pio_entry
from vmu_native_relocations import generate, validate_loads
from test_armada_ce_maple_hook import CPU

ROOT = Path(__file__).resolve().parents[1]
SPAN = 0x20000

# Whole integer trampoline, including its P2 resume literal and zero padding.
TRAMPOLINE_PREFIX = bytes.fromhex(
    "062f162f262f362f462f562f662f762f862f962fa62fb62fc62fd62fe62f224f"
    "034f234f134f024f124f0200f0cb0e4001d02b4009000900")


def resume_template(cached, relay):
    """Authored complete game_resume instruction body and all literal words."""
    ops = [0x68f3, 0, 0, 0x6902, 0, 0, 0x00a3, 0x7020, 0x3012, 0x8bfb,
           0, 0, 0x2012] + [9] * 8 + [0x6483, 0x6593, 0, 0x400b, 9,
           0, 0x209b, 0, 0x2102] + [9] * 8 + [0x6f83, 0x4f16, 0x4f06,
           0x4f17, 0x4f27, 0x7f04, 0x4f26] + [0x60f6 | r << 8 for r in range(14, 0, -1)] + \
          [0x60f3, 0x70c0, 0x6002, 0x400e, 0, 0x402b, 0x60f6]
    assert len(ops) == 66
    values = [0x8cff0000, 0xff00001c, 0x8c000000, 0x8d000000, 0x808]
    if cached:
        values.append(0x908)
    values += [relay, 0xac010000]
    for index, reg, word in ((1, 15, 0), (2, 0, 1), (4, 0, 2), (5, 1, 3),
                              (10, 0, 1), (11, 1, 5 if cached else 4),
                              (23, 0, 6 if cached else 5), (26, 0, 4),
                              (28, 1, 1), (63, 0, 7 if cached else 6)):
        at = 132 + 4 * word
        delta = at - ((2 * index + 4) & ~3)
        assert delta >= 0 and delta % 4 == 0
        ops[index] = 0xd000 | reg << 8 | delta // 4
    return struct.pack("<66H", *ops) + struct.pack("<" + "I" * len(values), *values)


class ResumeGateway(CPU):
    """Execute actual pinned assembly, folding only its proven RAM purge loop."""
    def __init__(self, seed, mask, ccr, resume, blob, trampoline, relay, cached):
        super().__init__(seed, resume, 1)
        self.resume, self.blob, self.trampoline, self.relay = resume, blob, trampoline, relay
        self.cached = cached
        self.r[15] = 0x8c00ed00
        self.start_sp = self.r[15]
        self.sr = (self.sr & ~0xf0) | mask << 4
        self.vbr = random.Random(seed + 83).getrandbits(32)
        self.owner_ccr, self.ccr = ccr, ccr
        self.pc, self.mem, self.called, self.purged = 0xac010000, {}, 0, False
        self.ccr_writes, self.wait = [], 0
        self.initial = (self.r.copy(), self.pr, self.mach, self.macl, self.gbr,
                        self.vbr, self.sr, self.fpscr, self.fr.copy())

    def code(self, address, size):
        if 0xac010000 <= address < address + size <= 0xac010000 + len(self.trampoline):
            return self.trampoline[address - 0xac010000:address - 0xac010000 + size]
        assert self.resume + 0x20000000 <= address < address + size <= self.resume + 0x20000000 + len(self.blob), \
            "cache-control relay must execute through P2"
        offset = address - self.resume - 0x20000000
        return self.blob[offset:offset + size]

    def read(self, address):
        if address == 0xff00001c:
            return self.ccr
        if address in self.mem:
            return self.mem[address]
        return struct.unpack("<I", self.code(address, 4))[0]

    def write(self, address, value):
        if address == 0xff00001c:
            assert self.pc >= self.resume + 0x20000000 and self.purged
            assert not self.wait
            self.ccr_writes.append(value & MASK)
            expected = (0x908 if self.cached else 0x808) if len(self.ccr_writes) == 1 else self.owner_ccr | 0x808
            assert value == expected and len(self.ccr_writes) <= 2, "unexpected CCR mode or owner-state loss"
            # ICI and OCI are command bits; reads retain the enable/mode bits.
            self.ccr, self.wait = value & ~0x808, 8
            return
        assert address % 4 == 0 and self.start_sp - 84 <= address < self.start_sp
        assert not self.ccr_writes, "relay writes original owner frame after publication"
        self.mem[address] = value & MASK

    def plain(self, word, pc):
        if self.wait:
            assert word == 9, "CCR access lacks eight following NOPs"
            self.wait -= 1
        if word in (0x0002,):
            self.r[0] = self.sr; return
        if word == 0x4f03:
            self.r[15] -= 4; self.write(self.r[15], self.sr); return
        if word in (0x4f23, 0x4f27):
            if word == 0x4f23:
                self.r[15] -= 4; self.write(self.r[15], self.vbr)
            else:
                self.vbr = self.read(self.r[15]); self.r[15] += 4
            return
        if word & 0xf00f == 0x200b:
            self.r[word >> 8 & 15] |= self.r[word >> 4 & 15]; return
        super().plain(word, pc)

    def relay_call(self):
        assert not self.wait and self.purged and self.called == 0
        assert self.r[4:6] == [self.start_sp - 84, self.owner_ccr]
        assert self.r[15] == 0x8cff0000 and self.sr & 0xf0 == 0xf0
        assert self.ccr == (0x100 if self.cached else 0) and not self.ccr & 1
        self.called += 1
        for i in range(8):
            self.r[i] = (0x92345678 + 0x1234567 * i) & MASK
        self.mach ^= 0x12345678; self.macl ^= 0x98765432
        self.gbr ^= 0x13572468; self.vbr ^= 0x87654321
        self.sr ^= 1
        self.pc = self.pr

    def run(self):
        for _ in range(200):
            pc = self.pc
            # Exact four-op loop is byte-pinned below. Folding preserves the
            # final R0/T and proves every aligned line in the 16MiB RAM range.
            if pc == self.resume + 0x20000000 + 12:
                assert self.code(pc, 8) == struct.pack("<4H", 0x00a3, 0x7020, 0x3012, 0x8bfb)
                assert self.r[:2] == [0x8c000000, 0x8d000000]
                self.r[0] = self.r[1]; self.t(True); self.purged = True
                self.pc += 8
                continue
            word = struct.unpack("<H", self.code(pc, 2))[0]
            if word & 0xf0ff in (0x402b, 0x400b):
                target = self.r[word >> 8 & 15]
                if word & 0xf0ff == 0x400b:
                    self.pr = pc + 4
                self.plain(struct.unpack("<H", self.code(pc + 2, 2))[0], pc + 2)
                if target == self.relay:
                    self.relay_call()
                elif target == 0xac010000:
                    self.pc = target
                    return self
                else:
                    self.pc = target
            else:
                self.plain(word, pc); self.pc += 2
        raise AssertionError("unbounded cache publication gateway")

    def check(self):
        r, pr, mach, macl, gbr, vbr, sr, fpscr, fr = self.initial
        assert self.r == r and self.pc == 0xac010000 and self.called == 1
        assert (self.pr, self.mach, self.macl, self.gbr, self.vbr, self.sr, self.fpscr, self.fr) == \
               (pr, mach, macl, gbr, vbr, sr, fpscr, fr)
        assert self.ccr_writes == [0x908 if self.cached else 0x808, self.owner_ccr | 0x808]
        assert self.ccr == self.owner_ccr & ~0x808 and not self.wait


def check_resume_publication(stage, cached, self_test=False):
    symbols = stage[4]
    resume = symbols["_kui_retail_game_resume"][0]
    relay, _, _ = symbol_bytes(stage, "_kui_retail_stage_relay", 2)
    require(resume % 4 == 0 and HIGH <= resume < LIMIT and HIGH <= relay < LIMIT,
            "unbounded cache relay symbols")
    expected = resume_template(cached, relay)
    image = inspect_elf(stage[0], HIGH, LIMIT)
    require(region(image["payload"], resume - HIGH, len(expected), "cache resume") == expected,
            "linked game_resume cache mode/publication/owner restoration changed")
    trampoline = symbols["__retail_trampoline_start"][0]
    require(symbols["__retail_trampoline_end"][0] == trampoline + 128 and
            symbols["__retail_trampoline_code_end"][0] == trampoline + 60,
            "owner trampoline span changed")
    template = TRAMPOLINE_PREFIX + struct.pack("<I", resume + 0x20000000) + bytes(68)
    require(region(image["payload"], trampoline - HIGH, 128, "cache trampoline") == template,
            "linked owner trampoline/P2 resume changed")
    cases = 0
    for seed in range(40):
        for mask in range(16):
            for ccr in (0, 1, 4, 0x20, 0x80, 0x100, 0x105, 0x1a7):
                ResumeGateway(seed, mask, ccr, resume, expected, template, relay, cached).run().check()
                cases += 1
    rejected = 0
    if self_test:
        # Coherent instruction/literal changes must fail the closed model too.
        mutations = [(12, 0x0009), (26, 0xe000), (60, 0xe000), (124, 0x0009),
                     (148, 0x0809), (156 if cached else 152, 0x0009)]
        if cached:
            mutations += [(152, 0x0808), (152, 0x0909)]
        for at, value in mutations:
            changed = bytearray(expected); struct.pack_into("<H", changed, at, value)
            try:
                ResumeGateway(0, 3, 0x100, resume, bytes(changed), template, relay, cached).run().check()
            except (AssertionError, struct.error):
                rejected += 1
            else:
                raise ValueError("resume gateway model accepted mutation at" + str(at))
    return {"instruction_cache_enabled_for_scan": cached, "operand_cache_disabled_for_scan": True,
            "scan_ccr": "0x00000908" if cached else "0x00000808", "owner_ccr_restored": True,
            "full_ram_purge_bytes": 0x1000000, "cache_write_nops": [8, 8],
            "owner_integer_frame_bytes": 84, "gateway_model_cases": cases,
            "gateway_mutation_rejections": rejected, "complete_linked_assembly_pinned": True}


def check_worker_publication(stage, cached, disassembly):
    """Pin the P2 publisher and witness initialization before its first arm."""
    image = inspect_elf(stage[0], HIGH, LIMIT)
    symbols = stage[4]
    address = symbols["_kui_native_passive_publish"][0]
    ops = [0xd001, 0x402b, 9, 9] + list(struct.unpack("<2H", struct.pack("<I", address + 12 + 0x20000000))) + \
          [0xe0e0, 0x2409, 0x3452, 0x8903, 0x04a3, 0x7420, 0xaffa, 9,
           0xd007, 0x6102, 0xd207, 0x212b, 0x2012] + [9] * 8 + [0x000b, 9, 9] + \
          list(struct.unpack("<4H", struct.pack("<2I", 0xff00001c, 0x800)))
    expected = struct.pack("<34H", *ops)
    require(address % 4 == 0 and region(image["payload"], address - HIGH, len(expected), "I-cache publisher") == expected,
            "linked P2 code publisher cache purge/ICI/owner CCR preservation changed")
    report = {"publisher_p2": hex(address + 12 + 0x20000000), "publisher_bytes": len(expected),
              "exact_linked_publisher_pinned": True, "ici_only_preserves_ccr": True,
              "post_ccr_write_nops": 8, "worker_prearm_publication_required": cached}
    if not cached:
        return report
    begin, body, _ = symbol_bytes(stage, "_native_passive_run", 2)
    words = list(struct.unpack("<" + "H" * (len(body) // 2), body))
    def literal(index):
        op = words[index]
        at = ((begin + index * 2 + 4) & ~3) + 4 * (op & 255)
        return struct.unpack("<I", region(body, at - begin, 4, "installer literal"))[0]
    candidates = [i for i, op in enumerate(words[:-20]) if op & 0xff00 == 0xde00 and
                  literal(i) == address and words[i + 1:i + 10] ==
                  [0x6593, 0x57b7, 0x358c, 0x2372, 0x57be, 0x53ad, 0x2372, 0x4e0b, 0x1f23] and
                  words[i + 10] & 0xff00 == 0xd000 and
                  literal(i + 10) == symbols["_kui_vmu_native_worker_call"][0] and
                  words[i + 11:i + 20] ==
                  [0x67f3, 0x52f3, 0x7720, 0x55aa, 0x66f3, 0x54a3, 0x7630, 0x400b, 0x1f28]]
    require(len(candidates) == 1, "missing unique linked worker-publication/first-arm block")
    first = candidates[0]
    # This bounded block loads the worker's own two dynamic target cells,
    # publishes exactly [load,load+worker_bytes), and immediately calls arm.
    publish, arm = begin + (first + 8) * 2, begin + (first + 18) * 2
    require(words[first - 1] & 0xff00 == 0xdb00 and
            literal(first - 1) == symbols["_native_driver"][0], "publisher uses a different driver witness")
    relocate = symbols["_kui_vmu_native_relocate"][0]
    relocations = [i for i, op in enumerate(words[:-11]) if op & 0xff00 == 0xd000 and
                   literal(i) == relocate and words[i + 1] & 0xff00 == 0xd200 and
                   literal(i + 1) == SPAN and words[i + 2] == 0x67e3 and
                   words[i + 3] & 0xff00 == 0xd600 and literal(i + 3) == symbols["__native_worker_blob_end"][0] and
                   words[i + 4] == 0x37d8 and words[i + 5] & 0xff00 == 0xd400 and
                   literal(i + 5) == symbols["__native_worker_blob_begin"][0] and
                   words[i + 6:i + 11] == [0x1f92, 0x1f21, 0x2fa2, 0x400b, 0x1f23]]
    zeroes = [i for i in range(len(words) - 7) if words[i:i + 7] ==
              [0x56f3, 0x6ba3, 0x3b8c, 0x3688, 0x64b3, 0x4c0b, 0xe500]]
    releases = [i for i in range(len(words) - 3) if words[i:i + 3] == [0x53ca, 0x7314, 0x2032]]
    require(len(relocations) == len(zeroes) == len(releases) == 1,
            "missing linked relocation/BSS/release-cell initialization witnesses")
    relocate_pc = begin + (relocations[0] + 9) * 2
    zero_pc = begin + (zeroes[0] + 5) * 2
    release_pc = begin + (releases[0] + 2) * 2
    # The compiler keeps memset in callee-saved R12 until this BSS call.
    # Pin its initializer and reject any intervening write to R12.
    memset_loads = [i for i in range(zeroes[0]) if words[i] & 0xff00 == 0xdc00 and
                    literal(i) == symbols["_memset"][0]]
    require(len(memset_loads) == 1, "BSS zeroing has no exact linked memset target")
    decoded = {pc: (op, mnemonic, operands.split("!", 1)[0].strip())
               for pc, op, mnemonic, operands in literal_aware_instructions(disassembly)
               if begin <= pc < begin + len(body)}
    for pc, (_, mnemonic, operands) in decoded.items():
        if begin + 2 * memset_loads[0] < pc < zero_pc and mnemonic not in ("cmp/eq", "cmp/hi", "cmp/hs", "tst"):
            require(not re.search(r"(?:^|,)\s*r12$", operands), "BSS memset target register is overwritten")
    # A conservative intraprocedural CFG treats every C call as returning.
    # The initialization witnesses must dominate the publication and arm;
    # byte order alone would allow a branch to bypass publication.
    edges, todo, seen = {}, [begin], set()
    while todo:
        pc = todo.pop()
        if pc in seen:
            continue
        require(pc in decoded, "installer branch enters non-instruction/literal bytes")
        seen.add(pc); op = decoded[pc][0]
        delay = op >> 12 in (0xa, 0xb) or op & 0xf0ff in (0x400b, 0x402b) or op == 0x000b or op >> 8 in (0x8d, 0x8f)
        if delay:
            require(pc + 2 in decoded, "installer delay slot is not an instruction")
        if op >> 12 == 0xa:
            d = op & 4095; d -= 4096 if d & 2048 else 0
            targets = [pc + 4 + 2 * d]
        elif op >> 8 in (0x89, 0x8b, 0x8d, 0x8f):
            d = op & 255; d -= 256 if d & 128 else 0
            targets = [pc + 4 + 2 * d, pc + (4 if delay else 2)]
        elif op == 0x000b:
            targets = []
        else:
            require(op & 0xf0ff != 0x402b, "unmodeled installer indirect tail jump")
            targets = [pc + (4 if delay else 2)]
        edges[pc] = targets; todo.extend(targets)
    predecessors = {pc: set() for pc in seen}
    for pc, targets in edges.items():
        for target in targets:
            predecessors[target].add(pc)
    dom = {pc: ({pc} if pc == begin else set(seen)) for pc in seen}
    for _ in range(len(seen)):
        changed = False
        for pc in seen - {begin}:
            value = {pc} | set.intersection(*(dom[p] for p in predecessors[pc]))
            if value != dom[pc]:
                dom[pc] = value; changed = True
        if not changed:
            break
    require({relocate_pc, zero_pc, release_pc, begin + (first + 4) * 2,
             begin + (first + 7) * 2} <= dom[publish] and publish in dom[arm],
            "installer can enter P1 arm before initialization/publication")
    require(begin + 2 * memset_loads[0] in dom[zero_pc],
            "BSS memset target can be bypassed")
    report.update(worker_relocation_before_publication=True, worker_bss_zero_before_publication=True,
                  dynamic_cells_before_publication=True, publication_dominates_first_arm=True,
                  first_worker_publish=hex(publish), first_worker_arm=hex(arm),
                  linked_installer_cfg_nodes=len(seen))
    return report


def require(value, message):
    if not value:
        raise ValueError(message)


def physical(address):
    return address & 0x1FFFFFFF


class CachedGateway(ReadonlyGateway):
    """Actual gateways fetch cached code/literals while frames/cells stayP2."""
    def code(self, address, size):
        assert address & 0xFF000000 == 0x8C000000, "gateway fetched uncached code/constant"
        offset = physical(address) - physical(self.base)
        assert 0 <= offset < offset + size <= len(self.blob)
        return self.blob[offset:offset + size]


def model_cached_hooks(binary, wire, symbols):
    cases = 0
    for destination in (P2, 0xAC38A880, 0xAC700000):
        blob = relocated(binary, wire, destination)
        mapped = {name: address + destination - P2 for name, address in symbols.items()}
        for seed in range(40):
            for mask in range(16):
                for mode in ("submit", "complete", "terminal", "release"):
                    CachedGateway(seed, mask, destination, blob, mapped, mode).run().check()
                    cases += 1
    return cases


class CachedStageGateway(StageGateway):
    def __init__(self, seed, mask, base, blob, **options):
        super().__init__(seed, mask, base, blob, entry=options.pop("entry", P2_ENTRY - 0x20000000), **options)
        self.cache_entry = P2_ENTRY - 0x20000000
    def run(self):
        steps = 0
        while self.pc != self.initial[1]:
            steps += 1;assert steps < 200
            pc = self.pc;word = struct.unpack("<H", self.code(pc, 2))[0]
            if word >> 12 == 0xA or word & 0xF0FF == 0x400B or word == 0xB:
                if word >> 12 == 0xA:
                    displacement = word & 4095
                    displacement -= 4096 if displacement & 2048 else 0
                    target = (pc + 4 + displacement * 2) & MASK
                elif word & 0xF0FF == 0x400B:
                    target = self.r[word >> 8 & 15];self.pr = pc + 4
                else:
                    target = self.pr
                self.plain(struct.unpack("<H", self.code(pc + 2, 2))[0], pc + 2)
                if target == self.cache_entry:
                    self.worker()
                else:
                    self.pc = target
            elif word >> 8 in (0x89, 0x8B):
                displacement = word & 255
                displacement -= 256 if displacement & 128 else 0
                take = bool(self.sr & 1) == (word >> 8 == 0x89)
                self.pc = (pc + 4 + displacement * 2 if take else pc + 2) & MASK
            else:
                self.plain(word, pc);self.pc = pc + 2
        return self


def model_cached_stage(base, blob):
    cases = 0
    for seed in range(80):
        for mask in range(16):
            CachedStageGateway(seed, mask, base, blob).run().check(int(mask == 15)); cases += 1
    entry = P2_ENTRY - 0x20000000
    invalid = (("entry", entry + 1), ("entry", P2_ENTRY), ("top", TOP + 4),
               ("top", 0xAD000020), ("top", 0xAC000FE0), ("config", 0), ("result", 0))
    for field, value in invalid:
        CachedStageGateway(1, 15, base, blob, **{field: value}).run().check(0); cases += 1
    CachedStageGateway(1, 15, base, blob, top=0xAD000000).run().check(1)
    return cases + 1

def model_cached_mutations(binary, symbols):
    """Reject control-flow/FPU changes without relying on ELF byte mismatch."""
    tests = []
    begin = physical(symbols["_kui_vmu_native_passive_submit_hook"]) - physical(P2)
    changed = bytearray(binary)
    struct.pack_into("<H", changed, begin, 0xF000)
    tests.append((changed, "submit", "FPU in gateway"))
    # Change the referenced callback literal in situ. Its wrong target is a
    # real known callback, so rejection must come from the frame/control-flow
    # model, rather than merely an out-of-bounds instruction read.
    for mode, target_name, wrong_name in (
            ("submit", "_kui_vmu_native_passive_submit", "_kui_vmu_native_passive_complete"),
            ("complete", "_kui_vmu_native_passive_complete", "_kui_vmu_native_passive_submit"),
            ("terminal", "_kui_vmu_native_passive_report", "_kui_vmu_native_passive_submit"),
            ("release", "_kui_vmu_native_readonly_release_check", "_kui_vmu_native_passive_complete")):
        function = symbols["_kui_vmu_native_readonly_release_hook"] if mode == "release" else symbols["_kui_vmu_native_passive_" + ("terminal" if mode == "terminal" else mode + "_hook")]
        matches = []
        # Authored hook functions are all shorter than 256 bytes. Inspect
        # PC-relative MOV.L operands, not arbitrary pointer-looking words.
        for offset in range(physical(function) - physical(P2), min(physical(function) - physical(P2) + 256, len(binary)) - 1, 2):
            word = struct.unpack_from("<H", binary, offset)[0]
            if word >> 12 != 0xD:
                continue
            literal = ((P2 + offset + 4) & ~3) + 4 * (word & 255) - P2
            if (0 <= literal <= len(binary) - 4 and
                    struct.unpack_from("<I", binary, literal)[0] == symbols[target_name]):
                matches.append(literal)
        require(matches, "missing actual gateway callback literal for mutation")
        changed = bytearray(binary)
        struct.pack_into("<I", changed, matches[0], symbols[wrong_name])
        tests.append((changed, mode, "wrong callback target"))
    # The completion must dereference its own consumer cell, not a stage
    # address, callback, or submission resume cell.
    complete = physical(symbols["_kui_vmu_native_passive_complete_hook"]) - physical(P2)
    matches = []
    for offset in range(complete, min(complete + 256, len(binary)) - 1, 2):
        word = struct.unpack_from("<H", binary, offset)[0]
        if word & 0xFF00 != 0xD300:
            continue
        literal = ((P2 + offset + 4) & ~3) + 4 * (word & 255) - P2
        if (0 <= literal <= len(binary) - 4 and
                struct.unpack_from("<I", binary, literal)[0] == symbols["_kui_vmu_native_passive_consumer"]):
            matches.append(literal)
    require(len(matches) == 1, "missing unique dynamic consumer-cell load")
    changed = bytearray(binary)
    struct.pack_into("<I", changed, matches[0], symbols["_kui_vmu_native_passive_resume"])
    tests.append((changed, "complete", "wrong consumer cell"))
    # Move inherited SR restoration one frame pop too early. This must fail
    # independently of final register equality, since a same-priority IRQ may
    # arrive as soon as the original mask is restored.
    pattern = struct.pack("<4H", 0x63F6, 0x61F6, 0x60F6, 0x430E)
    at = binary.find(pattern, complete, min(complete + 256, len(binary)))
    require(at >= 0, "missing complete-frame final restore sequence")
    changed = bytearray(binary)
    struct.pack_into("<2H", changed, at + 4, 0x430E, 0x60F6)
    tests.append((changed, "complete", "premature inherited SR restoration"))
    changed = bytearray(binary)
    struct.pack_into("<H", changed, at + 8, 0x6883)  # MOV R8,R8 is otherwise harmless.
    tests.append((changed, "complete", "missing SR latency instruction"))
    # Replay the original MDST store twice; the linked model admits that
    # exact address but requires precisely one store during submission.
    submit = physical(symbols["_kui_vmu_native_passive_submit_hook"]) - physical(P2)
    store = binary.find(struct.pack("<H", 0x12C6), submit, min(submit + 256, len(binary)))
    require(store >= 0, "missing original MDST replay")
    changed = bytearray(binary)
    struct.pack_into("<H", changed, store + 2, 0x12C6)
    tests.append((changed, "submit", "duplicate MDST replay"))
    # Apply the same lifetime-entry restoration mutation to the allocator
    # wrapper. Its C callback may execute in contexts different from Maple.
    release = physical(symbols["_kui_vmu_native_readonly_release_hook"]) - physical(P2)
    pattern = struct.pack("<5H", 0x60F6, 0x61F6, 0x6703, 0xCBF0, 0x400E)
    at = binary.find(pattern, release, min(release + 256, len(binary)))
    require(at >= 0, "missing release-frame final restore sequence")
    changed = bytearray(binary)
    struct.pack_into("<2H", changed, at + 2, 0x400E, 0x61F6)
    tests.append((changed, "release", "premature release SR restoration"))
    # Removing the OR before the first post-callback SR load produces the
    # same final state after the original mask-establishment sequence, yet
    # transiently permits IRQs to invalidate the just-checked free list.
    changed = bytearray(binary)
    struct.pack_into("<H", changed, at + 6, 0x0009)
    tests.append((changed, "release", "transient release IRQ admission"))
    # Cached code must never fall back to a P2 execution target, while its
    # private stack and mutable consumer cell must never acquire P1 aliases.
    for mode, hook, value, delta in (
            ("submit", "_kui_vmu_native_passive_submit_hook", "_kui_vmu_native_passive_submit", 0x20000000),
            ("complete", "_kui_vmu_native_passive_complete_hook", "_kui_vmu_native_passive_complete", 0x20000000),
            ("terminal", "_kui_vmu_native_passive_terminal", "_kui_vmu_native_passive_report", 0x20000000),
            ("release", "_kui_vmu_native_readonly_release_hook", "_kui_vmu_native_readonly_release_check", 0x20000000),
            ("complete", "_kui_vmu_native_passive_complete_hook", "_kui_vmu_native_passive_consumer", -0x20000000),
            ("complete", "_kui_vmu_native_passive_complete_hook", "__vmu_native_worker_stack_top", -0x20000000)):
        begin = physical(symbols[hook]) - physical(P2)
        references = []
        for offset in range(begin, min(begin + 256, len(binary)) - 1, 2):
            word = struct.unpack_from("<H", binary, offset)[0]
            if word >> 12 != 0xD: continue
            literal = ((P2 + offset + 4) & ~3) + 4 * (word & 255) - P2
            if 0 <= literal <= len(binary) - 4 and struct.unpack_from("<I", binary, literal)[0] == symbols[value]:
                references.append(literal)
        require(references, "missing actual cached gateway alias literal")
        changed = bytearray(binary)
        struct.pack_into("<I", changed, references[0], symbols[value] + delta)
        tests.append((changed, mode, "wrong cached code or mutable-state alias"))
    cases = 0
    for blob, mode, description in tests:
        try:
            CachedGateway(7, 6, P2, blob, symbols, mode).run().check()
        except (AssertionError, struct.error):
            cases += 1
        else:
            raise ValueError("gateway model accepted " + description)
    return cases


def worker_load_policy(worker, binary):
    data, _, segments, sections, _ = worker
    loads = validate_loads(data, binary, P1, SPAN, sections)
    if len(segments) == 1:
        kind, offset, va, pa, size, memory, flags, _ = segments[0]
        require((kind, va, pa, size, memory, flags) == (1, P2, P1, len(binary), SPAN, 7),
                "uncached worker differs from its exact legacy P2 load")
        require(binary == region(data, offset, size, "worker load"), "legacy flat worker differs from ELF")
        return {"cached_code": False, "code_alias": P2, "loads": loads}
    require(len(segments) == len(loads) == 3, "cached worker needs exactly3 physical loads")
    header, code, mutable = loads
    require((header["virtual"], header["physical"], header["bytes"], header["memory"], header["flags"]) ==
            (P2, P1, 80, 80, 4), "cached worker header must be immutable P2/80B")
    require(code["virtual"] == code["physical"] and code["virtual"] & 0xFF000000 == 0x8C000000 and
            code["flags"] == 5 and code["bytes"] == code["memory"] and code["bytes"] and
            not (code["first"] + code["bytes"]) & 31 and
            code["first"] in (physical(P1) + 80, (physical(P1) + 80 + 31) & ~31),
            "cached code/const load must own readonly P1 bytes through an aligned end")
    require(mutable["virtual"] == mutable["physical"] + 0x20000000 and mutable["flags"] == 6 and
            mutable["first"] == code["first"] + code["memory"] and not mutable["first"] & 31 and
            mutable["first"] + mutable["memory"] == physical(P1) + SPAN,
            "mutable worker load must be P2 and disjoint from every cached line")
    shstrings = sections[worker[1][13]]
    section_names = region(data, shstrings[4], shstrings[5], "section names")
    for section in sections:
        if not section[2] & 2 or not section[5]: continue
        if section[2] & 1:
            require(section[3] & 0xFF000000 == 0xAC000000 and
                    section[2] & 7 == 3, "cached worker has writable P1/code state")
        elif section[3] == P2:
            require(section[1] == 1 and section[5] == 80 and section[2] & 7 == 2,
                    "P2 header contains unexpected code/state")
        elif section[3] & 0xFF000000 == 0xAC000000:
            name = section_names[section[0]:].split(b"\0", 1)[0]
            require(name == b".image_align" and section[1] == 1 and section[2] & 7 == 2 and
                    0 < section[5] <= 32 and not any(region(data, section[4], section[5], "mutable tail padding")),
                    "P2 immutable bytes are neither header nor zero tail padding")
        else:
            require(section[3] & 0xFF000000 == 0x8C000000 and section[2] & 7 in (2, 6),
                    "code/const bytes do not exclusively use cached P1")
    return {"cached_code": True, "code_alias": P1, "loads": loads}


def inspect_base(directory, *, self_test=False, objdump="sh-elf-objdump"):
    """Reuse exact bootstrap/layout checks while admitting the one SCI loan."""
    images, elfs = {}, {}
    for name, base, limit in (("resident-sci", LOW, 0x8C007800),
                              ("stage", HIGH, LIMIT), ("entry", layout.EXEC_ADDRESS, 0x8C112000)):
        elfs[name] = (directory / (name + ".elf")).read_bytes()
        images[name] = inspect_elf(elfs[name], base, limit)
        if name != "entry":
            require((directory / (name + ".bin")).read_bytes() == images[name]["payload"],
                    name + " binary differs from ELF")
    resident, stage, entry = (images[name] for name in ("resident-sci", "stage", "entry"))
    check_bss(resident, LOW, "__retail_resident")
    check_bss(stage, HIGH, "__retail_stage")
    for name in ("resident-sci", "stage"):
        require(not any(symbol.startswith(("_kui_vmu_maple_observer_", "__vmu_observe_worker_blob_"))
                        for symbol in images[name]["symbols"]), "old fixed-address VMU worker linked")
    require(not any(symbol.startswith(("_kui_vmu_durable_", "_kui_vmu_persist_", "_kui_sci_sd_write",
                                       "_kui_sci_sd_sync")) for symbol in resident["symbols"]),
            "low reader contains a high-only persistent writer")
    for name in ("_kui_vmu_native_heap_discover", "_kui_vmu_native_heap_call_discover", "_kui_vmu_native_heap_fresh",
                 "_kui_vmu_native_heap_preallocate", "_kui_vmu_native_heap_postallocate",
                 "_kui_vmu_native_heap_lease_valid", "_kui_vmu_observe_stage_install",
                 "_kui_vmu_persist_stage_admit", "_kui_vmu_persist_stage_prepare",
                 "_kui_vmu_persist_stage_handoff_matches"):
        typed_function(elfs["stage"], stage, name, HIGH)
    for transport in ("scif", "sci", "ide", "scia"):
        prefix = "__retail_resident_" + transport + "_blob_"
        begin, end = (stage["symbols"].get(prefix + suffix, 0) for suffix in ("start", "end"))
        require(region(stage["payload"], begin - HIGH, end - begin, "embedded resident") ==
                padded(resident["payload"]), "stage embeds different resident: " + transport)
    es = entry["symbols"]
    require(es.get("__retail_map") == layout.EXEC_ADDRESS + layout.MAP_OFFSET and
            es.get("__retail_stage_blob_start") == layout.EXEC_ADDRESS + layout.STAGE_BLOB_OFFSET and
            es.get("__retail_stage_blob_end") == layout.EXEC_ADDRESS + len(entry["payload"]) and
            entry["memory_end"] == layout.EXEC_ADDRESS + len(entry["payload"]) and
            entry["payload"][layout.STAGE_BLOB_OFFSET:] == padded(stage["payload"]),
            "entry relocation/stage bytes differ from ELF")
    require(not any(region(entry["payload"], layout.MAP_OFFSET, layout.MAP_BYTES, "blank manifest")),
            "launch manifest must ship blank")
    require(region(entry["payload"], layout.HEADER_OFFSET, layout.HEADER_BYTES, "relocation header") ==
            layout.relocation_header(len(padded(stage["payload"])), False, True),
            "persistent probe must select low synchronous native reader")
    module = (directory / "retail-maple-observe.kui").read_bytes()
    runtime = verify(module)
    require(module[64:] == entry["payload"] and runtime["memory_bytes"] == entry["memory_end"] - layout.EXEC_ADDRESS,
            "module differs from actual entry ELF")
    instructions = {}
    for name in images:
        disassembly = subprocess.check_output([objdump, "-d", str(directory / (name + ".elf"))], text=True)
        instructions[name] = instruction_audit(name, disassembly)
    gateway = check_loan_gateway(directory, resident, elfs["resident-sci"])
    report = {"schema": "kui.native.persistent.bootstrap.v1", "runtime": runtime,
              "module_sha256": hashlib.sha256(module).hexdigest(),
              "resident_memory_end": f"0x{resident['memory_end']:08x}",
              "stage_memory_end": f"0x{stage['memory_end']:08x}",
              "dynamic_bootstrap": inspect_dynamic_hook(elfs["resident-sci"], resident),
              "integer_instruction_counts": instructions, "sd_lease_gateway": gateway,
              "old_fixed_worker_linked": False, "console_verified": False}
    if self_test:
        report["dynamic_bootstrap_mutation_rejections"] = regression(elfs["resident-sci"], resident)
    return report


def check_loan_gateway(directory, resident, elf):
    address, size, declared = sd_gateway_span(elf, resident)
    reports = sorted((directory / "resident/lto").glob("*.ltrans*.ci"))
    if not reports:
        reports = sorted(directory.glob("resident-sci.elf*.ci"))
    nodes = set()
    require(reports, "missing final-LTO SCI loan callgraph proof")
    for report in reports:
        data = report.read_text()
        nodes.update(_function_base(title) for title, _ in NODE.findall(data))
        require(not any(_function_base(target) == "kui_vmu_observe_sd_lease"
                        for _, target, _ in EDGE.findall(data)), "resident call enters worker-stack-only loan")
    require("kui_vmu_observe_sd_lease" in nodes, "final-LTO graph omits SCI loan")
    for offset in range(0, len(resident["payload"]) - 3, 4):
        at = LOW + offset
        if address <= at < address + size:
            continue
        value = struct.unpack_from("<I", resident["payload"], offset)[0]
        if value & 0xFF000000 in (0x0C000000, 0x8C000000, 0xAC000000):
            canonical = value & 0x1FFFFFFF | 0x80000000
            require(not address <= canonical < address + size, "low resident literal enters worker-stack-only loan")
    return {"address": f"0x{address:08x}", "bytes": size, "declared_symbol_bytes": declared,
            "incoming_resident_edges": 0, "resident_pointer_references": 0,
            "callgraph_reports": [report.name for report in reports]}


def check_pio_writers(directory, worker):
    results = {}
    for image_name, image, base, folder, adapter in (
            ("worker", worker, P1 if worker[4]["_kui_vmu_native_passive_arm"][0] < P2 else P2,
             "native", "durable_card_pio_write"),
            ("stage", read_elf(directory / "stage.elf"), HIGH, "stage", "native_persist_pio_write")):
        elf = image[0]
        binary = (directory / ("native-worker.bin" if image_name == "worker" else "stage.bin")).read_bytes()
        address, body, _ = symbol_bytes(image, "_" + adapter, 2)
        writer, _, _ = symbol_bytes(image, "_kui_sci_sd_write", 2)
        report = check_pio_entry({"payload": binary}, address, len(body), writer, base=base)
        report.update(check_pio_callgraph(directory / folder, adapter, linked_function_names(elf, {})))
        results[image_name] = report
    return results


def check_source(directory, runtime):
    raw = (directory / "entry.compile.json").read_bytes()
    record = json.loads(raw)
    require(isinstance(record, dict) and isinstance(record.get("commit"), str) and
            re.fullmatch(r"[0-9a-f]{40}", record["commit"]) and type(record.get("source_dirty")) is bool and
            record.get("elf_sha256") == hashlib.sha256((directory / "entry.elf").read_bytes()).hexdigest() and
            runtime["build"] == record["commit"][:12], "source provenance differs from compiled entry")
    return {"commit": record["commit"], "source_dirty": record["source_dirty"],
            "compile_provenance_sha256": hashlib.sha256(raw).hexdigest(),
            "elf_sha256": record["elf_sha256"]}


def worker_stack_paths(directory, worker, resident):
    """Walk source-qualified emitted graphs with finite authored callbacks."""
    report_sets = {"worker": sorted((directory / "native").rglob("*.ci")),
                   "low": sorted(directory.glob("resident-sci.elf*.ltrans*.ci"))}
    if not report_sets["low"]:
        report_sets["low"] = sorted((directory / "resident/lto").glob("*.ltrans*.ci"))
    images = {"worker": worker[4], "low": resident["symbols"]}
    frames, calls, origins = {}, {}, {}
    for domain, reports in report_sets.items():
        require(reports, "missing emitted callgraph: " + domain)
        f, c = load_graph(reports, qualified=True, scope=domain)
        live = {_function_base(name) for name in images[domain]}
        f = {name: value for name, value in f.items() if _function_base(name) in live}
        frames.update(f); calls.update(c)
        for report in reports:
            for title, label in NODE.findall(report.read_text()):
                pieces = label.split(r"\n")
                if len(pieces) >= 2:
                    origins[domain + ":" + title] = Path(pieces[1].rsplit(":", 2)[0]).name
    by_base = {}
    for name in frames:
        by_base.setdefault((name.split(":", 1)[0], _function_base(name)), []).append(name)
    def resolve(domain, function, owner=None):
        exact = domain + ":" + function
        found = [exact] if exact in frames else by_base.get((domain, _function_base(function)), [])
        if owner is not None:
            found = [name for name in found if origins.get(name) == owner]
        require(found, "missing emitted callback frame: " + domain + ":" + function)
        return found
    def high(*names):
        return [node for name in names for node in resolve("worker", name)]
    def bus(*names):
        return [node for name in names for node in resolve("low", name, "sci_sd_bus.c")]
    memo, paths, visiting, indirect_sites, null_sites = {}, {}, set(), set(), set()
    def indirect(name, label):
        domain, function = name.split(":", 1)[0], _function_base(name)
        source = Path(label.split(":", 1)[0]).name
        if domain == "low":
            require((function, source) == ("kui_vmu_observe_sd_lease", "vmu_sd_lease.inc"),
                    "unexpected low callback from worker stack: " + name + " at " + label)
            return high("kui_vmu_native_persistent_sd_transaction")
        fixed = {
            "vmu_durable.c": ("persistent_lease",),
            "vmu_emulation.c": ("durable_card_read", "durable_card_commit"),
            "vmu_native_heap.c": ("read_heap",),
            "vmu_native_layout.c": ("read_value",),
            "vmu_native_lifetime.c": ("read_heap",),
            "vmu_native_runtime.c": ("boundary_read",),
            "vmu_store.c": ("durable_card_sector_read", "durable_card_sector_write", "durable_card_sector_sync"),
            "vmu_readonly_overlay.c": ("persistent_admit", "persistent_handler"),
        }
        owners = {
            "vmu_durable.c": {"durable_card_execute"},
            "vmu_emulation.c": {"kui_vmu_emulation_handle"},
            "vmu_native_heap.c": {"read_is", "kui_vmu_native_heap_lease_valid"},
            "vmu_native_layout.c": {"kui_vmu_native_layout_bind_checked"},
            "vmu_native_lifetime.c": {"kui_vmu_native_lifetime_release_safe_ranges"},
            "vmu_native_runtime.c": {"kui_vmu_native_runtime_check"},
            "vmu_store.c": {"write_sync", "kui_vmu_store_open", "kui_vmu_store_read", "kui_vmu_store_sync"},
            "vmu_readonly_overlay.c": {"kui_vmu_readonly_overlay_prepare", "kui_vmu_readonly_overlay_complete"},
        }
        if source in fixed:
            require(function in owners[source], "unexpected callback source owner: " + name)
            return high(*fixed[source])
        if (function, source) == ("persistent_lease", "vmu_native_passive.c"):
            return resolve("low", "kui_vmu_observe_sd_lease")
        if source == "sd_reader.c":
            targets = {"transfer": ("transfer",), "ticks": ("ticks",), "release": ("select_card",),
                       "read_payload": ("transfer_block",), "kui_loader_sd_read": ("select_card",)}
            require(function in targets, "unexpected initialized-card reader callback: " + name)
            return bus(*targets[function])
        if source == "vmu_backing.c":
            require(function in ("byte", "wait_byte", "kui_vmu_sd_identity_read"),
                    "unexpected backing callback: " + name)
            return bus("transfer", "ticks", "select_card")
        if source == "sci_sd_storage.c":
            if function == "kui_sci_sd_write" and label.endswith(":83:17"):
                # Actual native PIO adapters independently prove this callback
                # NULL at the sole writer entry; this optional branch is dead.
                null_sites.add((name, label)); return []
            return bus("transfer", "ticks", "select_card")
        raise ValueError("unadmitted indirect worker call: " + name + " at " + label)
    def depth(name):
        if name in memo:
            return memo[name]
        require(name not in visiting, "recursive private-stack path: " + name)
        if name not in frames:
            if _function_base(name) in ("__udivsi3", "__sdivsi3", "__udivdi3", "__umoddi3", "__moddi3"):
                return 0, [name + "(library margin)"]
            domain, target = name.split(":", 1)
            alternatives = resolve(domain, target)
            return max((depth(node) for node in alternatives), key=lambda item: item[0])
        visiting.add(name); best = (0, [])
        for target, label in calls.get(name, []):
            targets = indirect(name, label) if target == "__indirect_call" else [target]
            if target == "__indirect_call":
                indirect_sites.add((name, label, tuple(targets)))
            for callee in targets:
                found = depth(callee)
                if found[0] > best[0]: best = found
        visiting.remove(name)
        memo[name] = (frames[name] + best[0], [name + "(" + str(frames[name]) + ")"] + best[1])
        return memo[name]
    for root in ("kui_vmu_native_passive_arm", "kui_vmu_native_passive_submit",
                 "kui_vmu_native_passive_complete", "kui_vmu_native_readonly_release_check",
                 "kui_vmu_native_passive_report"):
        answer = max((depth(node) for node in resolve("worker", root)), key=lambda item: item[0])
        total = answer[0] + 80 + 64
        require(total <= 4096, f"private stack path {root} needs{total} bytes")
        paths[root] = {"c_frame_bytes": answer[0], "assembly_and_guard_margin": 144,
                       "conservative_bytes": total, "worst_path": answer[1]}
    require(null_sites, "missing independently guarded optional writer block callback")
    return {"paths": paths, "indirect_sites": len(indirect_sites),
            "null_writer_block_sites": [list(site) for site in sorted(null_sites)],
            "source_qualified_calls": True}


def check_stacks(directory, worker):
    resident = inspect_elf((directory / "resident-sci.elf").read_bytes(), LOW, 0x8C007800)
    low_rows = linked_stack_rows(sorted((directory / "resident/lto").glob("*.ltrans*.su")), resident["symbols"])
    high_rows = linked_stack_rows(sorted((directory / "native").rglob("*.su")), worker[4])
    reported = {_function_base(name) for name, _ in high_rows}
    assembly = {"kui_vmu_native_passive_submit_hook", "kui_vmu_native_passive_complete_hook",
                "kui_vmu_native_passive_terminal", "kui_vmu_native_readonly_release_hook"}
    required = {_function_base(name) for name, (_, _, kind, _) in worker[4].items()
                if kind == 2 and _function_base(name).startswith(("kui_", "durable_", "readonly_overlay_", "sci_storage_"))
                and _function_base(name) not in assembly}
    require(required <= reported, "missing linked native C stack frame: " + ", ".join(sorted(required - reported)))
    lease = [(name, frame) for name, frame in low_rows if _function_base(name) == "kui_vmu_observe_sd_lease"]
    require(len(lease) == 1, "missing unique worker-stack-only lease frame")
    # Every retained C frame is included, including low bus callbacks called
    # from the worker. Summing them overstates any single actual path.
    high_sum, low_sum = sum(frame for _, frame in high_rows), sum(frame for _, frame in low_rows)
    call_paths = worker_stack_paths(directory, worker, resident)
    total = max(path["conservative_bytes"] for path in call_paths["paths"].values())
    available = (resident_layout.stack_top("sci", False, True) -
                 resident_layout.stack_bottom("sci", False, True) -
                 resident_layout.STACK_GUARD_BYTES - resident_layout.STACK_ALIGNMENT_GAP)
    ordinary = low_sum - lease[0][1] + resident_layout.ASSEMBLY_STACK_BYTES
    require(ordinary <= available, f"ordinary native GD stack sum {ordinary} exceeds{available}")
    return {"worker_c_frame_sum": high_sum, "resident_callback_c_frame_sum": low_sum,
            "conservative_worker_bytes": total, "available_worker_bytes": 4096,
            "ordinary_low_stack_bytes": ordinary, "ordinary_low_available_bytes": available,
            "lease_only_frame_bytes": lease[0][1], "required_worker_frames": len(required), "callgraph": call_paths}


def check(directory, *, self_test=False):
    require(__debug__, "gateway model requires Python assertions")
    directory = Path(directory)
    base = inspect_base(directory, self_test=self_test,
                        objdump=shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump"))
    worker = read_elf(directory / "native-worker.elf")
    data, header, segments, sections, raw_symbols = worker
    symbols = {name: record[0] for name, record in raw_symbols.items()}
    binary = (directory / "native-worker.bin").read_bytes()
    wire = (directory / "native-worker.reloc").read_bytes()
    policy = worker_load_policy(worker, binary)
    cached, code_base = policy["cached_code"], policy["code_alias"]
    require(len(binary) >= 80, "worker header is truncated")
    fields = struct.unpack_from("<20I", binary)
    names = ("_kui_vmu_native_passive_arm", "_kui_vmu_native_passive_submit_hook",
             "_kui_vmu_native_passive_complete_hook", "_kui_vmu_native_passive_terminal",
             "__vmu_native_worker_bss_begin", "__vmu_native_worker_bss_end",
             "__vmu_native_worker_stack_bottom", "__vmu_native_worker_stack_top",
             "__vmu_native_worker_end", "_kui_vmu_native_passive_resume", "_kui_vmu_native_passive_consumer",
             "_kui_vmu_native_readonly_release_hook", "_kui_vmu_native_readonly_release_resume",
             "_kui_vmu_native_persistent_sd_transaction")
    require(fields[:3] == (0x4B4E5031, 1, 80) and
            fields[3:17] == tuple(symbols[name] for name in names) and not any(fields[17:]),
            "read-only worker exports differ from exact typed symbols")
    require(fields[3] == header[4] and all(not p & 1 and code_base + 80 <= p < code_base + len(binary) for p in fields[3:7]),
            "worker entry/hook points outside initialized code")
    require(P2 + len(binary) == fields[7] <= fields[8] <= fields[9] and
            fields[9:12] == (P2 + 0x1F000, P2 + SPAN, P2 + SPAN), "worker BSS/stack layout differs")
    address, exported, section = symbol_bytes(worker, "_kui_vmu_native_worker_exports", 1)
    require(address == P2 and exported == binary[:80] and section[2] & 7 == 2,
            "exports must be typed immutable header")
    for name in names[:4] + (names[11], names[13]):
        _, _, section = symbol_bytes(worker, name, 2)
        require(section[2] & 7 == 6, "exported entry is not executable")
    for name in ("_kui_vmu_native_passive_resume", "_kui_vmu_native_passive_consumer",
                 "_kui_vmu_native_readonly_release_resume"):
        address, cell, section = symbol_bytes(worker, name, 1)
        require(len(cell) == 4 and address % 4 == 0 and not any(cell) and section[2] & 7 == 3,
                "resume/consumer cells must be typed, zero-initialized worker-owned words")
    for section in sections:
        require(not section[2] & 2 or not section[5] or
                physical(P1) <= physical(section[3]) < physical(section[3]) + section[5] <= physical(P1) + SPAN,
                "allocated worker bytes escape physical lease span")
    for name, (value, _, symbol_kind, owner) in raw_symbols.items():
        require(symbol_kind not in (1, 2) or owner < len(sections) and
                physical(P1) <= physical(value) < physical(P1) + SPAN,
                "worker object/function escapes physical lease: " + name)
        if symbol_kind == 2:
            require(code_base <= value < code_base + len(binary), "function uses wrong cached/uncached alias: " + name)
        if symbol_kind == 1 and sections[owner][2] & 1:
            require(value & 0xFF000000 == 0xAC000000, "mutable object has a cached P1 alias: " + name)
        require(not name.startswith(("_kui_retail_storage_", "_kui_sci_sd_acquire", "_kui_sci_sd_release",
                                      "_kui_loader_sd_init", "_kui_loader_sd_adopt", "_kui_loader_sd_shutdown")),
                "persistent worker independently owns/reinitializes resident storage: " + name)
    for name in ("_kui_vmu_durable_init", "_kui_vmu_durable_handle", "_kui_vmu_durable_admit",
                 "_kui_vmu_durable_transaction", "_kui_vmu_store_open", "_kui_vmu_store_write",
                 "_kui_vmu_store_read", "_kui_sci_sd_write", "_kui_sci_sd_sync",
                 "_kui_vmu_readonly_overlay_prepare", "_kui_vmu_readonly_overlay_complete",
                 "_kui_vmu_volatile_chain_build", "_kui_vmu_volatile_chain_verify",
                 "_kui_vmu_native_readonly_release_check", "_kui_vmu_native_lifetime_release_safe_ranges"):
        _, _, section = symbol_bytes(worker, name, 2)
        require(section[2] & 7 == 6, "missing bounded persistent bridge implementation: " + name)
    require(generate(data, binary, P1, SPAN) == wire, "relocation table differs from exact ELF extraction")
    relocations = struct.unpack_from("<I", wire, 28)[0]
    for i in range(relocations):
        value = struct.unpack_from("<I", wire, 76 + i * 8)[0]
        area = value & 0xFF000000
        require(area in ((0x8C000000, 0xAC000000) if cached else (0xAC000000,)),
                "internal relocated pointer uses an unadmitted alias")
        if cached:
            code = policy["loads"][1]
            if area == 0x8C000000:
                require(code["first"] <= physical(value) <= code["first"] + code["memory"],
                        "cached pointer aliases mutable P2 state")
            else:
                require(not code["first"] <= physical(value) < code["first"] + code["memory"],
                        "uncached pointer aliases immutable cached code/constants")

    stage = read_elf(directory / "stage.elf")
    prepared = inspect_elf(stage[0], 0x8CE00000, 0x8CFE0000)
    stage_binary = (directory / "stage.bin").read_bytes()
    require(prepared["payload"] == stage_binary, "stage binary differs from ELF")
    publication = check_resume_publication(stage, cached, self_test)
    objdump = shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump")
    stage_disassembly = subprocess.check_output([objdump, "-d", str(directory / "stage.elf")], text=True)
    publisher = check_worker_publication(stage, cached, stage_disassembly)
    for begin, end, payload in (("__native_worker_blob_begin", "__native_worker_blob_end", binary),
                                ("__native_worker_reloc_begin", "__native_worker_reloc_end", wire)):
        a, b = stage[4][begin][0], stage[4][end][0]
        require(b - a == len(payload) and region(stage_binary, a - 0x8CE00000, b - a, begin) == payload,
                "stage embeds different worker/relocation bytes")
        require(any(s[1] == 1 and s[2] & 7 == 2 and s[3] <= a < b <= s[3] + s[5] for s in stage[3]),
                "embedded worker must occupy stage immutable data")
    gateway_address, gateway, _ = symbol_bytes(stage, "_kui_vmu_native_worker_call", 2)
    stage_cases = (model_cached_stage if cached else model_gateway)(gateway_address, gateway)
    hook_cases = (model_cached_hooks if cached else model_hooks)(binary, wire, symbols)
    objdump = shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump")
    disassembly = subprocess.check_output([objdump, "-d", str(directory / "native-worker.elf")], text=True)
    try:
        instructions = instruction_audit("native-readonly", disassembly)
    except SystemExit as error:
        raise ValueError(str(error)) from error
    hook_ranges = [symbol_bytes(worker, name, 2)[:2] for name in names[1:4] + (names[11],)]
    for address, _, mnemonic, operands in literal_aware_instructions(disassembly):
        require(mnemonic not in ("trapa", "sleep", "rte", "ldtlb"), "unexpected privileged worker instruction")
        if mnemonic in ("ldc", "ldc.l") and operands.split("!", 1)[0].strip().endswith(",sr"):
            require(any(begin <= address < begin + len(body) for begin, body in hook_ranges),
                    "C worker changes SR outside audited gateways")
    pio = check_pio_writers(directory, worker)
    stacks = check_stacks(directory, worker)
    source = check_source(directory, base["runtime"])
    report = {"schema": "kui.native.persistent.probe.artifact.v1", "allocator_bootstrap_audit": base,
              "module_sha256": base["module_sha256"], "runtime": base["runtime"],
              "worker_bytes": len(binary), "worker_span": SPAN, "worker_stack_bytes": 4096,
              "worker_load_policy": policy, "cached_code": cached,
              "stage_cache_publication": publication,
              "worker_icache_publication": publisher,
              "relocation_records": relocations, "worker_instructions": instructions,
              "stage_gateway_model_cases": stage_cases, "persistent_gateway_model_cases": hook_cases,
              "original_release_mask_model_cases": hook_cases // 4,
              "stage_embedded_worker_verified": True, "maple_interception_installed": True,
              "virtual_replies_enabled": True, "storage_writes_enabled": True, "scoped_sci_transactions": True,
              "source_provenance": source, "stack_audit": stacks, "pio_writer_audit": pio,
              "console_execution_verified": False, "expected_phase": "native persistent session"}
    if self_test:
        report["gateway_mutation_rejections"] = (model_cached_mutations if cached else model_mutations)(binary, symbols)
        report["artifact_mutation_rejections"] = mutation_checks(directory)
    return report


def mutation_checks(directory):
    """Coherent changed flat images still must fail their typed ELF binding."""
    checks = 0
    with tempfile.TemporaryDirectory() as temp:
        target = Path(temp)
        for path in directory.iterdir():
            if path.is_file() and path.suffix in (".elf", ".bin", ".reloc", ".kui"):
                (target / path.name).symlink_to(path.resolve())
        for name, at in (("native-worker.bin", 0), ("native-worker.bin", 12),
                         ("native-worker.reloc", 72), ("stage.bin", 128)):
            path = target / name
            original = (directory / name).read_bytes()
            changed = bytearray(original); changed[at] ^= 1
            path.unlink(); path.write_bytes(changed)
            try:
                check(target)
            except (ValueError, AssertionError, KeyError, struct.error):
                checks += 1
            else:
                raise ValueError("modified artifact accepted: " + name)
            path.unlink(); path.symlink_to((directory / name).resolve())
    return checks



def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    try:
        result = json.dumps(check(args.directory, self_test=args.self_test), indent=2) + "\n"
    except (OSError, ValueError, KeyError, AssertionError, struct.error, subprocess.CalledProcessError) as error:
        p.exit(2, f"Native persistent artifact audit refused: {error}\n")
    if args.output: args.output.write_text(result)
    print(result, end="")


if __name__ == "__main__":
    main()
