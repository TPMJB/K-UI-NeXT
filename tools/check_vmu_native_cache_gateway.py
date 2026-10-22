#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked temporary cache wrapper with adversarial C callees.

Only the small stage wrapper is interpreted. The real cache decoder/discovery
and live ownership modules have separate C tests; their success/failure values
are explicit inputs here. No cache record is treated as live ownership.
"""
import random
import struct

from check_vmu_native_worker_gateway import CPU, MASK, symbol_bytes

STAGE, LIMIT = 0x8CE00000, 0x8CFE0000


def require(value, message):
    if not value:
        raise ValueError(message)


class CacheGateway(CPU):
    def __init__(self, stage, seed, mask, outcomes, blob=None):
        symbols = stage[4]
        begin, body, _ = symbol_bytes(stage, "_native_resolution_replay", 2)
        super().__init__(seed, begin, 1)
        self.blob = body if blob is None else blob
        self.pc, self.r[15], self.mem = begin, 0x8CFF0000, {}
        self.start_sp = self.r[15]
        self.mem.update({self.start_sp - 4 * i: (0xa5a50000 + seed * 97 + i) & MASK for i in range(1, 33)})
        self.sr = (self.sr & ~0xf0) | mask << 4
        self.pr = begin + len(self.blob) + 0x100
        self.before = self.r.copy(), self.pr, self.sr, self.fpscr, self.fr.copy()
        self.outcomes = dict(zip(("seed", "replay", "allocation", "release"), outcomes))
        self.symbols, self.calls, self.copies = symbols, [], []
        self.seed_address = symbols["__native_resolution_cache_seed_start"][0]
        self.state = symbols["_native_cached_resolution"][0]
        self.mem[symbols["_exec_bytes"][0]] = 0x200000
        values = random.Random(seed + 903).choices(range(0x100000000), k=171)
        values[544 // 4 + 6] = 0x8c013000
        values[544 // 4 + 10] = 0x8c0131b0
        values[616 // 4 + 3] = 0x0c300000
        values[616 // 4 + 4] = 0x00800000
        self.proof = [int(v) for v in values]
        self.targets = {symbols[name][0]: label for name, label in (
            ("_kui_vmu_native_cache_seed_read", "seed"),
            ("_kui_vmu_native_resolution_cache_replay", "replay"),
            ("_memcpy", "memcpy"), ("_allocation_safe", "allocation"),
            ("_readonly_release_replay_valid", "release"),
            ("_native_passive_capture_windows", "capture"))}

    def code(self, address, size):
        assert self.base <= address < address + size <= self.base + len(self.blob)
        return self.blob[address - self.base:address - self.base + size]

    def read(self, address):
        assert address % 4 == 0
        if address in self.mem:
            return self.mem[address]
        return struct.unpack("<I", self.code(address, 4))[0]

    def write(self, address, value):
        assert address % 4 == 0 and self.start_sp - 128 <= address < self.start_sp
        self.mem[address] = value & MASK

    def plain(self, word, pc):
        n, m = word >> 8 & 15, word >> 4 & 15
        if word >> 12 == 9:
            value = struct.unpack("<h", self.code(pc + 4 + 2 * (word & 255), 2))[0]
            self.r[n] = value & MASK; return
        if word & 0xf00f == 0x2008:
            self.t((self.r[n] & self.r[m]) == 0); return
        if word >> 8 == 0xc8:
            self.t((self.r[0] & (word & 255)) == 0); return
        if word >> 8 == 0xc9:
            self.r[0] &= word & 255; return
        if word >> 8 == 0x88:
            value = word & 255; value -= 256 if value & 128 else 0
            self.t(self.r[0] == value & MASK); return
        if word & 0xf00f == 0x200b:
            self.r[n] |= self.r[m]; return
        super().plain(word, pc)

    def api(self, target):
        assert target in self.targets, "unexpected cache wrapper callback"
        name = self.targets[target]
        self.calls.append(name)
        return_pc, result = self.pr, 0
        if name == "seed":
            assert self.calls == ["seed"] and self.r[4:6] == [self.seed_address, 1120]
            assert self.r[6] != self.r[7]
            if self.outcomes[name]:
                self.write(self.r[6], self.seed_address + 64)
                self.write(self.r[7], self.seed_address + 24)
                result = 1
        elif name == "replay":
            assert self.calls == ["seed", "replay"] and self.outcomes["seed"]
            assert self.r[4:8] == [0x8c010000, 0x200000, 0x8c010000, self.seed_address + 64]
            assert [self.read(self.r[15] + 4 * i) for i in range(3)] == [1024, self.seed_address + 24, self.state]
            if self.outcomes[name]:
                self.mem.update({self.state + 4 * i: word for i, word in enumerate(self.proof)})
                result = 1
        elif name == "memcpy":
            copies = (("_native_driver", 0, 104), ("_passive_layout", 104, 440),
                      ("_native_heap", 544, 72), ("_native_call", 616, 20), ("_native_exits", 636, 48))
            assert self.outcomes["seed"] and self.outcomes["replay"] and len(self.copies) < len(copies), \
                "metadata published before exact-image replay succeeded"
            symbol, offset, count = copies[len(self.copies)]
            destination = self.symbols[symbol][0]
            assert self.r[4:7] == [destination, self.state + offset, count]
            for i in range(0, count, 4):
                self.mem[destination + i] = self.read(self.state + offset + i)
            self.copies.append(symbol); result = destination
        elif name == "allocation":
            assert len(self.copies) == 5
            assert self.r[4:6] == [0x0c300000, 0x00800000]
            result = int(self.outcomes[name])
        elif name == "release":
            assert len(self.copies) == 5
            result = int(self.outcomes[name])
        else:
            assert name == "capture" and all(self.outcomes.values())
            assert len(self.copies) == 5 and "allocation" in self.calls and "release" in self.calls
        # Every volatile GPR/PR and T may be clobbered by an ABI-compliant
        # callee. Unbanked R8..R15 retain the wrapper's saved targets/frame.
        for i in range(8):
            self.r[i] = (0x913579bd + 0x13579bd * i + len(self.calls)) & MASK
        self.r[0] = result
        self.sr ^= 1; self.pr ^= 0x2468ace0; self.pc = return_pc

    def run(self):
        for _ in range(250):
            if self.pc == self.before[1]:
                return self
            pc = self.pc; word = struct.unpack("<H", self.code(pc, 2))[0]
            if word >> 12 == 0xa or word & 0xf0ff == 0x400b or word == 0xb:
                if word >> 12 == 0xa:
                    d = word & 4095; d -= 4096 if d & 2048 else 0
                    target = pc + 4 + 2 * d
                elif word & 0xf0ff == 0x400b:
                    target = self.r[word >> 8 & 15]; self.pr = pc + 4
                else:
                    target = self.pr
                self.plain(struct.unpack("<H", self.code(pc + 2, 2))[0], pc + 2)
                if target in self.targets:
                    self.api(target)
                else:
                    self.pc = target
            elif word >> 8 in (0x89, 0x8b, 0x8d, 0x8f):
                d = word & 255; d -= 256 if d & 128 else 0
                taken = bool(self.sr & 1) == (word >> 8 in (0x89, 0x8d))
                delayed = word >> 8 in (0x8d, 0x8f)
                if delayed:
                    self.plain(struct.unpack("<H", self.code(pc + 2, 2))[0], pc + 2)
                self.pc = pc + 4 + 2 * d if taken else pc + (4 if delayed else 2)
            else:
                self.plain(word, pc); self.pc += 2
        raise AssertionError("unbounded native cache wrapper")

    def check(self):
        registers, pr, sr, fpscr, fr = self.before
        assert self.r[8:] == registers[8:] and self.pr == pr
        assert (self.fpscr, self.fr) == (fpscr, fr) and self.sr & ~1 == sr & ~1
        expected = all(self.outcomes.values())
        assert self.r[0] == int(expected)
        assert self.calls.count("capture") == int(expected)
        if self.outcomes["seed"] and self.outcomes["replay"]:
            assert len(self.copies) == 5 and "release" in self.calls
            if self.outcomes["release"]:
                assert "allocation" in self.calls
        else:
            assert not self.copies and "allocation" not in self.calls and "release" not in self.calls


class CacheRoute(CacheGateway):
    """Execute the outer relay through cache-hit/cold-scan selection only."""
    def __init__(self, stage, seed, mask, hit, invalid=0):
        begin, self.blob, _ = symbol_bytes(stage, "_kui_retail_stage_relay", 2)
        CPU.__init__(self, seed, begin, 1)
        self.pc, self.r[15], self.mem = begin, 0x8cff0000, {}
        self.start_sp = self.r[15]
        self.sr = (self.sr & ~0xf0) | mask << 4
        self.pr = begin + len(self.blob) + 0x100
        self.before = self.r.copy(), self.pr
        self.r[4:6] = [0x8c00ecac, 0x105]
        self.hit, self.invalid, self.calls = hit, invalid, []
        self.route = None
        symbols = stage[4]
        manifest = symbols["_manifest"][0]
        self.mem[symbols["_exec_bytes"][0]] = 0x200000
        self.mem.update({manifest + 44: 0 if invalid == 1 else 1,
                         manifest + 48: int(invalid == 2),
                         manifest + 52: 1 if invalid == 3 else 2 if invalid == 4 else 0})
        self.targets = {symbols[name][0]: label for name, label in (
            ("_retail_display_restore", "display"), ("_retail_display_line", "line"),
            ("_kui_native_heap_base_relay", "restore"), ("_kui_retail_observe_native_ip", "ip"),
            ("_native_resolution_replay", "cache"), ("_code_sha", "cached"),
            ("_kui_vmu_native_discover_layout", "cold"), ("_native_report", "refused"))}

    def api(self, target):
        name = self.targets[target]
        self.calls.append(name); return_pc = self.pr
        if name == "restore":
            assert self.r[4:6] == [0x8c00ecac, 0x105]
            assert "ip" not in self.calls and "cache" not in self.calls
        if name in ("ip", "cache", "cached", "cold"):
            assert self.calls.count("restore") == 1, "mapping used before executable restoration"
        if name == "cache":
            assert self.calls.count("ip") == 1 and not self.invalid
        if name in ("cached", "cold", "refused"):
            self.route = name; self.pc = self.before[1]
            return
        for i in range(8):
            self.r[i] = (0x9468ace0 + i * 0x13579bd + len(self.calls)) & MASK
        self.r[0] = int(self.invalid != 5) if name == "ip" else int(self.hit) if name == "cache" else 0
        self.sr ^= 1; self.pr ^= 0x2468ace0; self.pc = return_pc

    def check(self):
        assert self.calls.count("restore") == 1
        if self.invalid:
            assert self.route == "refused" and "cache" not in self.calls
        else:
            assert self.calls.count("cache") == 1
            assert self.route == ("cached" if self.hit else "cold")
            assert ("cold" in self.calls) == (not self.hit)


def check(stage, *, self_test=False):
    symbols, sections = stage[4], stage[3]
    address, size, kind, owner = symbols["_native_cached_resolution"]
    require(kind == 1 and size == 684 and owner < len(sections), "cache metadata must be one typed684B stage object")
    section = sections[owner]
    require(section[1] == 8 and section[2] & 7 == 3 and
            STAGE <= address < address + size <= LIMIT and
            section[3] <= address < address + size <= section[3] + section[5],
            "temporary cache metadata must remain in high-stage BSS")
    begin, body, _ = symbol_bytes(stage, "_native_resolution_replay", 2)
    cases = 0
    for seed in range(24):
        for mask in range(16):
            for flags in range(16):
                outcomes = [(flags >> i) & 1 for i in range(4)]
                CacheGateway(stage, seed, mask, outcomes).run().check(); cases += 1
    routes = 0
    for seed in range(24):
        for mask in range(16):
            for hit, invalid in ((False, 0), (True, 0), (True, 1), (True, 2),
                                 (True, 3), (True, 4), (True, 5)):
                CacheRoute(stage, seed, mask, hit, invalid).run().check(); routes += 1
    mutations = 0
    if self_test:
        # Mutate every actual callback literal and an exact memcpy length.
        targets = [symbols[name][0] for name in ("_kui_vmu_native_cache_seed_read",
            "_kui_vmu_native_resolution_cache_replay", "_allocation_safe", "_readonly_release_replay_valid",
            "_native_passive_capture_windows")]
        changed_bodies = []
        for target in targets:
            at = body.find(struct.pack("<I", target))
            require(at >= 0, "cache wrapper callback lacks its linked literal")
            changed = bytearray(body); struct.pack_into("<I", changed, at, 0x8ce00002)
            changed_bodies.append(bytes(changed))
        at = body.find(struct.pack("<H", 0xe668))
        require(at >= 0, "cache wrapper lacks exact104B metadata copy")
        changed = bytearray(body); struct.pack_into("<H", changed, at, 0xe664)
        changed_bodies.append(bytes(changed))
        for changed in changed_bodies:
            try:
                CacheGateway(stage, 7, 15, [1, 1, 1, 1], changed).run().check()
            except (AssertionError, struct.error, KeyError):
                mutations += 1
            else:
                raise ValueError("cache wrapper accepted a callback/copy mutation")
        words = struct.unpack("<" + "H" * (len(body) // 2), body)
        gates = [i + 1 for i in range(len(words) - 1) if words[i] == 0x2008 and
                 words[i + 1] >> 8 in (0x89, 0x8b, 0x8d, 0x8f)]
        require(len(gates) == 4, "cache wrapper must branch on all four callback results")
        for gate in gates:
            changed = bytearray(body); struct.pack_into("<H", changed, gate * 2, 9)
            for flags in range(16):
                try:
                    CacheGateway(stage, 7, 15, [(flags >> i) & 1 for i in range(4)], bytes(changed)).run().check()
                except (AssertionError, struct.error, KeyError):
                    mutations += 1
                    break
            else:
                raise ValueError("cache wrapper accepted a missing result guard")
    return {"enabled": True, "temporary_resolution_address": hex(address), "temporary_resolution_bytes": size,
            "temporary_high_bss_only": True, "wrapper_entry": hex(begin), "wrapper_bytes": len(body),
            "actual_instruction_model_cases": cases, "mutation_rejections": mutations,
            "actual_outer_relay_route_cases": routes, "executable_restored_before_replay": True,
            "fresh_scan_selected_only_on_cache_miss": True, "launch_eligibility_before_replay": True,
            "no_metadata_before_exact_image_replay": True, "fresh_allocation_and_release_guards": True,
            "capture_only_after_all_guards": True}
