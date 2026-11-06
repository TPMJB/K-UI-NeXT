#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked temporary cache wrapper with adversarial C callees.

Only the small stage wrapper is interpreted. The real cache decoder/discovery
and live ownership modules have separate C tests; their success/failure values
are explicit inputs here. No cache record is treated as live ownership.
"""
import random
import struct
import hashlib
from pathlib import Path
import sys

from check_vmu_native_worker_gateway import CPU, MASK, symbol_bytes, ROOT
from sh4_delay_slots import validate_delay_slot
from check_native_general_lean_profile import stage_profile,stage_pins

STAGE, LIMIT = 0x8CE00000, 0x8CFE0000


def discovery_name(stage):
    marker=stage[4].get('__kui_native_cached_discovery')
    wrapper='_kui_native_stage_cached_discover_image'
    require(bool(marker)==(wrapper in stage[4]),'Fresh discovery marker/function mismatch')
    if marker is None:return '_kui_vmu_native_discover_layout'
    require(marker==(1,0,0,0xfff1) and '_kui_native_stage_discover_image' in stage[4],
            'Fresh discovery lacks its exact typed immutable leaf')
    return wrapper


def coherence_name(stage):
    """Select the reviewed image-cohort continuation, never an alias fallback."""
    checked='_native_sdk_coherence_after_image_proof'
    full='_native_sdk_coherence_cached_admit'
    if checked not in stage[4]:
        require('__kui_native_cached_discovery' not in stage[4] and full in stage[4],
                'New immutable discovery cannot use an unqualified coherence continuation')
        return full
    require(discovery_name(stage)=='_kui_native_stage_cached_discover_image' and
            stage[4].get('__kui_native_cached_resolution')==(1,0,0,0xfff1) and full not in stage[4],
            'Checked coherence requires the reviewed fresh image-replay/discovery cohort')
    return checked


def coherence_binding(stage):
    selected=coherence_name(stage)
    if selected=='_native_sdk_coherence_cached_admit':return {'same_invocation_image_proof_reused':False}
    path=ROOT/'src/core/vmu_native_resolution_cache.c'
    require(hashlib.sha256(path.read_bytes()).hexdigest()==
            '32dd8684d6a90dad9a184734b537973470ad1b55fa3d72f080f65cd72903d093',
            'Reprove the fresh SHA/role ordering after changing the replay leaf')
    _,body,_=symbol_bytes(stage,'_kui_vmu_native_resolution_cache_replay',2)
    role=stage[4]['_kui_vmu_native_early_p2_calls_valid'][0]
    require(struct.pack('<I',role) in body,'Actual replay leaf lost fresh EARLY_P2 role validation')
    return dict(same_invocation_image_proof_reused=True,selected_coherence_target=selected,
        current_live_slot_admission_retained=True,replay_source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        replay_leaf_sha256=hashlib.sha256(body).hexdigest(),fresh_role_predicate=hex(role))


def replay_name(stage):
    """Select only the explicitly marked, same seven-argument pure wrapper."""
    marker=stage[4].get('__kui_native_cached_resolution')
    wrapper='_kui_vmu_native_cached_resolution_replay'
    require(bool(marker)==(wrapper in stage[4]),'Common replay cache marker/function mismatch')
    if marker is not None:
        require(marker==(1,0,0,0xfff1),'Invalid common replay cache scope marker')
        address,body,section=symbol_bytes(stage,wrapper,2)
        require(body and section[1]==1 and section[2]&7==6 and STAGE<=address<address+len(body)<=LIMIT,
                'Common replay cache scope is not a bounded typed stage function')
        return wrapper
    return '_kui_vmu_native_resolution_cache_replay'


def require(value, message):
    if not value:
        raise ValueError(message)


def preclear_window(stage, body=None):
    """One typed, zero-argument call followed by the existing code-SHA call."""
    from check_native_bottom_heap import records
    symbols = stage[4]
    relay, original, _ = symbol_bytes(stage, '_kui_retail_stage_relay', 2)
    body = original if body is None else body
    entry, code, kind, section = records(stage)['_native_preclear_prepare@vmu_native_heap_probe_stage.c']
    require(kind == 2 and section[1] == 1 and section[2] & 7 == 6 and
            symbols['_native_preclear_prepare'][:3] == (entry, len(code), 2),
            'Preclear route lost its exact source-qualified function entry')
    digest, size, kind, owner = symbols['_native_heap_code_sha']
    require(0 < owner < len(stage[3]), 'Preclear digest has no valid typed section owner')
    target_section = stage[3][owner]
    require(kind == 1 and size == 32 and target_section[1] == 8 and target_section[2] & 7 == 3 and
            STAGE <= digest < digest + size <= symbols['__retail_stage_bss_end'][0] <= LIMIT and
            target_section[3] <= digest < digest + size <= target_section[3] + target_section[5],
            'Preclear continuation lost its exact mutable32-byte digest object')
    sha_entry, sha_body, sha_section = symbol_bytes(stage, '_code_sha', 2)
    require(sha_body and sha_section[1] == 1 and sha_section[2] & 7 == 6,
            'Preclear continuation code SHA must be a typed executable function')
    def literal(at, register):
        op = struct.unpack_from('<H', body, at)[0]
        if op & 0xff00 != 0xd000 | register << 8:
            return None
        address = ((relay + at + 4) & ~3) + 4 * (op & 255)
        if not relay <= address <= relay + len(body) - 4:
            return None
        return struct.unpack_from('<I', body, address - relay)[0], address
    windows = []
    for at in range(0, len(body) - 13, 2):
        first = literal(at, 1)
        if not first or first[0] != entry:
            continue
        words = struct.unpack_from('<7H', body, at)
        second, output = literal(at + 6, 1), literal(at + 8, 4)
        if words[1:3] != (0x410b, 9) or words[5:] != (0x410b, 9) or \
                not second or second[0] != sha_entry or not output or output[0] != digest:
            continue
        windows.append(dict(entry=entry, call=relay + at + 2, return_pr=relay + at + 6,
            code_sha_return_pr=relay + at + 14, digest=digest,
            literal_addresses=[first[1], second[1], output[1]],
            window_offset=at, window_hex=body[at:at + 14].hex(),
            function_sha256=hashlib.sha256(code).hexdigest(), argument_count=0))
    require(len(windows) == 1, 'Preclear route requires its unique exact JSR/NOP/code-SHA window')
    return windows[0]


def bind_preclear(stage, directory=None, *, self_test=False):
    from check_native_dual_callbacks import preclear_profile
    if not preclear_profile(stage):
        return None
    require(directory is not None, 'Preclear cache route requires its actual artifact-bound companion directory')
    from check_vmu_native_worker_gateway import read_elf
    from check_native_preclear import check as check_preclear
    directory = Path(directory).resolve()
    actual = read_elf(directory / 'stage.elf')
    require(actual[0] == stage[0] and actual[1:] == stage[1:],
            'Preclear companion directory stage differs from the parsed stage under test')
    proof_path = directory / 'native-preclear-proof.json'
    require(proof_path.is_file(), 'Preclear cache route requires its actual-original companion file')
    proof_bytes = proof_path.read_bytes()
    companion = check_preclear(directory, self_test=self_test)
    require(companion.get('enabled') is True and companion.get('original_linked_readers') == 2 and
            companion.get('original_linked_refusals') == 62,
            'Preclear route lacks the complete actual-original companion')
    require(proof_path.read_bytes() == proof_bytes and
            companion.get('actual_original_linked_companion_sha256') == hashlib.sha256(proof_bytes).hexdigest() and
            (directory / 'stage.elf').read_bytes() == stage[0],
            'Preclear companion or stage changed while its binding was checked')
    return dict(stage_sha256=hashlib.sha256(stage[0]).hexdigest(), window=preclear_window(stage),
                actual_companion=companion)


class CacheGateway(CPU):
    """fixed_fallback: a replayed fixed-arena record (no initializer slot);
    arena: a replayed arena lease (heap family 2), which binds its plan again
    and then has no allocator release entry to admit."""
    def __init__(self, stage, seed, mask, outcomes, blob=None, *, fixed_fallback=False, arena=False):
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
        self.fixed_fallback,self.arena=fixed_fallback,arena
        require(not (fixed_fallback and arena), 'one cache replay fallback at a time')
        require(not (fixed_fallback or arena) or stage_profile(stage),
                'fixed-plan or arena cache replay requires the exact lean General stage')
        require(len(outcomes)==(6 if fixed_fallback or arena else 5),
                "cache wrapper needs every independently admitted callback result")
        self.outcomes = dict(zip(("seed", "replay", "allocation", "release", "sdk-slot",
                                  "arena-plan" if arena else "fixed-plan"), outcomes))
        self.symbols, self.calls, self.copies = symbols, [], []
        self.seed_address = symbols["__native_resolution_cache_seed_start"][0]
        self.state = symbols["_native_cached_resolution"][0]
        self.mem[symbols["_exec_bytes"][0]] = 0x200000
        values = random.Random(seed + 903).choices(range(0x100000000), k=172)
        values[544 // 4 + 6] = 0x8c013000
        values[544 // 4 + 10] = 0x8c0131b0
        values[616 // 4 + 3] = 0x0c300000
        values[616 // 4 + 4] = 0x00800000
        # The heap family: the allocator (1) or an arena lease (2).
        values[544//4+2]=2 if arena else 1
        if fixed_fallback:
            values[544//4+7]=values[544//4+8]=0
        self.proof = [int(v) for v in values]
        self.fixed_writes=[]
        self.targets = {symbols[name][0]: label for name, label in (
            ("_kui_vmu_native_cache_seed_read", "seed"),
            (replay_name(stage), "replay"),
            ("_memcpy", "memcpy"), ("_allocation_safe", "allocation"),
            ("_readonly_release_replay_valid", "release"),
            (coherence_name(stage), "sdk-slot"),
            ("_native_passive_capture_windows", "capture"))}
        if fixed_fallback:
            self.targets[symbols['_native_fixed_plan_bind'][0]]='fixed-plan'
        # The dual stage's arena route flag is clear when the replay begins;
        # only a successful arena plan bind sets it (with the fixed route's).
        self.arena_flag=symbols['_native_arena'][0] if '_native_arena' in symbols else None
        if self.arena_flag is not None:
            self.mem[self.arena_flag]=0
        if arena:
            self.targets[symbols['_native_arena_plan_bind'][0]]='arena-plan'

    def required(self):
        """The callback results a successful replay depends on."""
        names=["seed","replay"]+(["arena-plan"] if self.arena else ["fixed-plan"] if self.fixed_fallback else [])
        return names+([] if self.arena else ["release"])+["allocation","sdk-slot"]

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
        if word & 0xf00f == 0x3006:  # unsigned cmp/hi Rm,Rn
            self.t(self.r[n]>self.r[m]);return
        if word & 0xf00f == 0x2009:
            self.r[n]&=self.r[m];return
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
                      ("_native_heap", 544, 72), ("_native_call", 616, 20), ("_native_exits", 636, 52))
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
            assert not self.arena or self.calls[-2]=='arena-plan' and self.outcomes['arena-plan']
            result = int(self.outcomes[name])
        elif name == "release":
            assert len(self.copies) == 5 and not self.arena, "an arena lease has no release entry to admit"
            assert not self.fixed_fallback or self.outcomes['fixed-plan'] and self.calls[-2]=='fixed-plan'
            result = int(self.outcomes[name])
        elif name == 'arena-plan':
            assert self.arena and self.calls==['seed','replay']+['memcpy']*5+['arena-plan']
            assert len(self.copies)==5 and self.r[4]==1
            heap=self.symbols['_native_heap'][0]
            assert self.read(heap+8)==2
            if self.outcomes[name]:
                # The separately proved arena plan API admits exactly the
                # replayed owner and call, unchanged, and selects the arena
                # route; the wrapper must still admit placement and the SDK.
                self.fixed_writes=[(self.arena_flag,1)]
                self.mem.update(self.fixed_writes)
                result=1
        elif name == 'fixed-plan':
            assert self.fixed_fallback and self.calls==['seed','replay']+['memcpy']*5+['fixed-plan']
            assert len(self.copies)==5 and self.r[4]==1
            heap=self.symbols['_native_heap'][0]
            assert self.read(heap+28)==self.read(heap+32)==0
            if self.outcomes[name]:
                # The separately proved fixed-plan API admits exactly the
                # replayed call/arena. Its successful initializer metadata
                # is private stage state; the wrapper must still perform
                # its release, allocation and SDK-slot admissions afterward.
                self.fixed_writes=[(heap+28,0x8c014000),(heap+32,self.read(heap+24))]
                self.mem.update(self.fixed_writes)
                result=1
        elif name == "sdk-slot":
            assert len(self.copies)==5 and all(self.outcomes[n] for n in self.required() if n!="sdk-slot")
            assert self.calls[-3:]==["arena-plan" if self.arena else "release","allocation","sdk-slot"]
            result=int(self.outcomes[name])
        else:
            assert name == "capture" and all(self.outcomes[n] for n in self.required())
            assert len(self.copies) == 5 and "allocation" in self.calls and ("release" in self.calls)!=self.arena
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
            if word >> 12 == 0xa or word & 0xf0ff in (0x400b, 0x402b) or word == 0xb:
                if word >> 12 == 0xa:
                    d = word & 4095; d -= 4096 if d & 2048 else 0
                    target = pc + 4 + 2 * d
                elif word & 0xf0ff in (0x400b, 0x402b):
                    target = self.r[word >> 8 & 15]
                    if word & 0xf0ff == 0x400b:
                        self.pr = pc + 4
                else:
                    target = self.pr
                slot=struct.unpack("<H", self.code(pc + 2, 2))[0]
                validate_delay_slot(slot,pc+2)
                self.plain(slot, pc + 2)
                if target in self.targets:
                    self.api(target)
                else:
                    self.pc = target
            elif word >> 8 in (0x89, 0x8b, 0x8d, 0x8f):
                d = word & 255; d -= 256 if d & 128 else 0
                taken = bool(self.sr & 1) == (word >> 8 in (0x89, 0x8d))
                delayed = word >> 8 in (0x8d, 0x8f)
                if delayed:
                    slot=struct.unpack("<H", self.code(pc + 2, 2))[0]
                    validate_delay_slot(slot,pc+2)
                    self.plain(slot, pc + 2)
                self.pc = pc + 4 + 2 * d if taken else pc + (4 if delayed else 2)
            else:
                self.plain(word, pc); self.pc += 2
        raise AssertionError("unbounded native cache wrapper")

    def check(self):
        registers, pr, sr, fpscr, fr = self.before
        assert self.r[8:] == registers[8:] and self.pr == pr
        assert (self.fpscr, self.fr) == (fpscr, fr) and self.sr & ~1 == sr & ~1
        expected = all(self.outcomes[n] for n in self.required())
        assert self.r[0] == int(expected)
        assert self.calls.count("capture") == int(expected)
        calls=["seed"]
        if self.outcomes["seed"]:
            calls.append("replay")
            if self.outcomes["replay"]:
                calls += ["memcpy"]*5
                if self.arena:
                    calls.append('arena-plan')
                    admitted=bool(self.outcomes['arena-plan'])
                else:
                    if self.fixed_fallback:calls.append('fixed-plan')
                    if not self.fixed_fallback or self.outcomes['fixed-plan']:
                        calls.append('release')
                    admitted='release' in calls and bool(self.outcomes["release"])
                if admitted:
                    calls.append("allocation")
                    if self.outcomes["allocation"]:
                        calls.append("sdk-slot")
                        if self.outcomes["sdk-slot"]:calls.append("capture")
        assert self.calls==calls,"cache wrapper bypassed/reordered a live metadata/ownership/SDK-slot admission"
        if self.outcomes["seed"] and self.outcomes["replay"]:
            assert len(self.copies) == 5
            assert ('release' in self.calls)==(not self.arena and
                                               (not self.fixed_fallback or bool(self.outcomes['fixed-plan'])))
            if 'release' in self.calls and self.outcomes["release"]:
                assert "allocation" in self.calls
        else:
            assert not self.copies and "allocation" not in self.calls and "release" not in self.calls
        if self.fixed_fallback:
            heap=self.symbols['_native_heap'][0]
            expected_fixed=[(heap+28,0x8c014000),(heap+32,self.proof[544//4+6])]
            assert self.fixed_writes==(expected_fixed if 'fixed-plan' in self.calls and self.outcomes['fixed-plan'] else [])
        if self.arena:
            assert self.fixed_writes==([(self.arena_flag,1)] if self.outcomes['seed'] and self.outcomes['replay'] and
                                       self.outcomes['arena-plan'] else [])
        # Check every copied word, including the two documented fixed-plan
        # output fields. Failure never permits an extra metadata publication.
        copies=(("_native_driver",0,104),("_passive_layout",104,440),
                ("_native_heap",544,72),("_native_call",616,20),("_native_exits",636,52))
        for name,offset,count in copies[:len(self.copies)]:
            for i in range(0,count,4):
                address=self.symbols[name][0]+i
                expected=dict(self.fixed_writes).get(address,self.proof[(offset+i)//4])
                assert self.read(address)==expected


class CacheRoute(CacheGateway):
    """Execute the outer relay through cache-hit/cold-scan selection only."""
    def __init__(self, stage, seed, mask, hit, invalid=0, *, mode=0, normalized=False, blob=None, eager=False,
                 preclear=None, preclear_refusal=0):
        begin, self.blob, _ = symbol_bytes(stage, "_kui_retail_stage_relay", 2)
        if blob is not None:
            self.blob = blob
        CPU.__init__(self, seed, begin, 1)
        self.pc, self.r[15], self.mem = begin, 0x8cff0000, {}
        self.start_sp = self.r[15]
        # A valid stack word can be speculatively loaded in a branch delay
        # slot even when the failed callee left its out parameter untouched.
        # Give every local an unrelated initial value; only a successful
        # extent result may publish its output to exec_bytes.
        self.mem.update({self.start_sp-4*i:(0xa5a50000+seed*97+i)&MASK
                         for i in range(1,33)})
        self.sr = (self.sr & ~0xf0) | mask << 4
        self.pr = begin + len(self.blob) + 0x100
        self.before = self.r.copy(), self.pr
        self.r[4:6] = [0x8c00ecac, 0x105]
        self.hit, self.invalid, self.calls, self.mode = hit, invalid, [], mode
        self.route = None
        self.preclear, self.preclear_refusal = preclear, preclear_refusal
        require(preclear_refusal in (0, 0x53, 0x54), 'Unknown preclear boundary outcome')
        self.preclear_return = MASK if seed & 1 else 0
        self.preclear_terminal = None
        symbols = stage[4]
        self.discovery_cached=discovery_name(stage)=='_kui_native_stage_cached_discover_image'
        background='_native_background_boot_region' in symbols
        manifest = symbols["_manifest"][0]
        self.exec_address=symbols["_exec_bytes"][0]
        self.source_bytes=0x200000
        self.program_bytes=self.source_bytes-(0x8000 if normalized else 0)
        self.extent_writes=0
        self.mem[self.exec_address] = self.source_bytes
        self.mem.update({manifest + 44: 0 if invalid == 1 else 1,
                         manifest + 48: (0 if invalid==2 else 3 if invalid==7 else 2 if eager else 1)
                            if background else int(invalid == 2),
                         manifest + 52: 1 if invalid == 3 else 2 if invalid == 4 else 0})
        extent_name=("_kui_vmu_native_cached_extent" if "__kui_native_cached_extent" in symbols
                     else "_kui_vmu_native_program_extent")
        self.targets = {symbols[name][0]: label for name, label in (
            ("_retail_display_restore", "display"), ("_retail_display_line", "line"),
            ("_kui_native_heap_base_relay", "restore"), ("_kui_retail_observe_native_ip", "ip"),
            (extent_name, "extent"),
            ("_native_resolution_replay", "cache"), ("_code_sha", "cached"),
            (discovery_name(stage), "cold"), ("_native_report", "refused"))}
        if "_native_runtime_mode" in symbols:
            self.targets[symbols["_native_runtime_mode"][0]] = "mode"
        from check_native_dual_callbacks import preclear_profile
        if preclear_profile(stage):
            if preclear is None:
                require(not hit or invalid or mode == 1,
                        'A cached preclear continuation requires its verified companion')
            else:
                require(preclear.get('stage_sha256') == hashlib.sha256(stage[0]).hexdigest() and
                        preclear.get('window') == preclear_window(stage, self.blob),
                        'Preclear binding differs from the exact linked stage/window')
                self.targets[preclear['window']['entry']] = 'preclear'

    def write(self,address,value):
        if address==self.exec_address:
            assert self.calls.count("extent")==1 and not self.invalid
            assert value==self.program_bytes
            self.extent_writes+=1
            self.mem[address]=value
        else:
            super().write(address,value)

    def api(self, target):
        name = self.targets[target]
        self.calls.append(name); return_pc = self.pr
        if name == "restore":
            assert self.r[4:6] == [0x8c00ecac, 0x105]
            assert "ip" not in self.calls and "cache" not in self.calls
            assert self.mem[self.exec_address]==self.source_bytes
            self.relay_sp = self.r[15]
        if name == "mode":
            assert self.calls.count("restore") == 1 and not any(
                value in self.calls for value in ("ip", "extent", "cache", "cached", "cold", "refused"))
        if name in ("ip", "extent", "cache", "cached", "cold"):
            assert self.calls.count("restore") == 1, "mapping used before executable restoration"
        if name=="extent":
            assert self.calls.count("ip")==1 and self.invalid in (0,6)
            assert self.r[4:7]==[0x8c010000,self.source_bytes,0x8c010000]
            assert self.mem[self.exec_address]==self.source_bytes
            if self.invalid!=6:self.write(self.r[7],self.program_bytes)
        if name == "cache":
            assert self.calls.count("ip") == 1 and not self.invalid
            assert self.calls.count("extent")==1 and self.extent_writes==1
            assert self.mem[self.exec_address]==self.program_bytes
        if name == 'preclear':
            assert self.preclear is not None and self.hit and not self.invalid and self.mode != 1
            assert self.calls[-3:] == ['cache', 'line', 'preclear']
            assert self.calls.count('extent') == self.extent_writes == self.calls.count('cache') == 1
            assert self.mem[self.exec_address] == self.program_bytes
            assert self.pr == self.preclear['window']['return_pr'] and self.r[15] == self.relay_sp
            if self.preclear_refusal:
                self.preclear_terminal = [2, self.preclear_refusal]
                self.route = 'preclear-refused'; self.pc = self.before[1]
                return
        if name in ("cached", "cold", "refused"):
            if name=='cold' and self.discovery_cached:
                assert self.r[4:7]==[0x8c010000,self.program_bytes,0x8c010000]
                assert self.read(self.r[7])==0 and self.r[7]==self.r[15]+8
            if name == 'cached' and self.preclear is not None:
                assert self.calls[-2:] == ['preclear', 'cached'] and not self.preclear_refusal, \
                    'Code SHA continuation reached before its preclear boundary'
                assert self.pr == self.preclear['window']['code_sha_return_pr']
                assert self.r[4] == self.preclear['window']['digest'] and self.r[15] == self.relay_sp
            self.route = name; self.pc = self.before[1]
            return
        for i in range(8):
            self.r[i] = (0x9468ace0 + i * 0x13579bd + len(self.calls)) & MASK
        self.r[0] = (self.preclear_return if name == 'preclear' else
                     self.mode if name == "mode" else int(self.invalid != 5) if name == "ip"
                     else int(self.invalid!=6) if name=="extent"
                     else int(self.hit) if name == "cache" else 0)
        self.sr ^= 1; self.pr ^= 0x2468ace0; self.pc = return_pc

    def check(self):
        assert self.calls.count("restore") == 1
        if self.mode == 1:
            assert self.calls.count("mode") == 1 and self.route is None
            assert not any(value in self.calls for value in ("ip", "extent", "cache", "cached", "cold", "refused"))
            # Control ends in a legal sibling call to the display routine.
            # PR is volatile at the C ABI; the enclosing relay assembly owns
            # restoration of the game's separately saved return frame.
            assert self.r[8:] == self.before[0][8:]
            return
        if self.invalid:
            assert self.route == "refused" and "cache" not in self.calls
            assert not self.extent_writes and self.mem[self.exec_address]==self.source_bytes
        elif self.preclear_refusal and self.hit and self.preclear is not None:
            assert self.route == 'preclear-refused' and self.preclear_terminal == [2, self.preclear_refusal]
            assert self.calls.count('preclear') == self.calls.count('cache') == self.extent_writes == 1
            assert 'cached' not in self.calls and 'cold' not in self.calls
            assert self.mem[self.exec_address] == self.program_bytes
        else:
            assert self.calls.count("cache") == 1
            assert self.route == ("cached" if self.hit else "cold")
            assert ("cold" in self.calls) == (not self.hit)
            assert self.extent_writes==1 and self.mem[self.exec_address]==self.program_bytes
            if self.preclear is not None:
                assert self.calls.count('preclear') == int(self.hit)


class ExtentGateway(CacheGateway):
    """Execute the linked pure-proof cache scope, including P1/P2 routing.

    The only fold is the exact four-op normal-index whole-RAM OCBP loop.
    Cached callee writes are withheld from uncached readers until that purge,
    a stronger publication requirement than this scope's P1 write-through.
    """
    def __init__(self,stage,seed,mask,ccr,result,blob=None):
        begin,body,_=symbol_bytes(stage,"_kui_vmu_native_cached_extent",2)
        CPU.__init__(self,seed,begin,1)
        self.blob=body if blob is None else blob
        self.pc,self.r[15],self.mem=begin,0x8cff0000,{}
        self.start_sp=self.r[15]
        self.r[4:8]=[0x8c010000,0x3de7e0,0x8c010000,self.start_sp+32]
        self.args=self.r[4:8].copy()
        self.sr=(self.sr&~0xf0)|(mask<<4)
        self.pr=begin+len(body)+0x100
        self.before=(self.r.copy(),self.pr,self.sr,self.fpscr,self.fr.copy())
        self.owner_ccr,self.ccr,self.result=ccr,ccr,result
        self.calls,self.ccr_writes,self.wait,self.purged,self.dirty=[],[],0,False,set()
        self.targets={stage[4]["_kui_vmu_native_program_extent"][0]:"extent"}
        self.output_before=0xa5b6c7d8
        self.mem[self.args[3]]=self.output_before

    def code(self,address,size):
        offset=(address-self.base)&MASK
        if offset>=0x20000000:offset-=0x20000000
        assert 0<=offset<offset+size<=len(self.blob)
        return self.blob[offset:offset+size]

    def read(self,address):
        if address==0xff00001c:return self.ccr
        if address in self.mem:
            assert self.ccr&1 or address not in self.dirty,"unpublished proof output/stack read with operand cache off"
            return self.mem[address]
        return struct.unpack("<I",self.code(address,4))[0]

    def write(self,address,value):
        if address==0xff00001c:
            assert self.base+0x20000000<=self.pc<self.base+0x20000000+len(self.blob)
            assert not self.wait and self.owner_ccr==0x100
            expected=0x909 if not self.ccr_writes else 0x908
            assert value==expected and len(self.ccr_writes)<2,"unexpected pure-proof cache profile"
            if self.ccr_writes:assert self.purged and not self.dirty,"cache disabled before proof publication"
            self.ccr_writes.append(value);self.ccr=value&~0x808;self.wait=8
            return
        assert address%4==0 and self.start_sp-12<=address<self.start_sp
        self.mem[address]=value&MASK
        if self.ccr&1:self.dirty.add(address)

    def plain(self,word,pc):
        if self.wait:
            assert word==9,"pure-proof CCR update lacks eight NOPs"
            self.wait-=1
        if word==0x00a3:
            assert self.code(pc,8)==struct.pack("<4H",0x00a3,0x7020,0x3012,0x8bfb)
            assert self.r[:2]==[0x8c000000,0x8d000000] and self.calls==["extent"]
            assert self.ccr_writes==[0x909] and self.ccr==0x101 and not self.purged
            self.r[0]=0x8d000000-32;self.purged=True;self.dirty.clear()
            return
        super().plain(word,pc)

    def api(self,target):
        assert target in self.targets and not self.wait and not self.calls
        assert self.r[4:8]==self.args and self.r[15]==self.start_sp-12
        assert self.sr&0xf0==self.before[2]&0xf0
        assert self.ccr==(0x101 if self.owner_ccr==0x100 else self.owner_ccr)
        assert self.ccr_writes==([0x909] if self.owner_ccr==0x100 else [])
        self.calls.append("extent");return_pc=self.pr
        # An ABI-compliant proof may use its own P1 frame and set output only
        # on success. The caller's original output must survive a refusal.
        for i in range(1,33):
            a=self.r[15]-4*i;self.mem[a]=(0x13579bdf+7*i)&MASK
            if self.ccr&1:self.dirty.add(a)
        if self.result:
            self.mem[self.args[3]]=self.args[1]-0x8000
            if self.ccr&1:self.dirty.add(self.args[3])
        for i in range(8):self.r[i]=(0x913579bd+0x13579bd*i)&MASK
        self.r[0]=self.result;self.sr^=1;self.pr^=0x2468ace0;self.pc=return_pc

    def check(self):
        r,pr,sr,fpscr,fr=self.before
        assert self.r[8:]==r[8:] and self.pr==pr and self.r[0]==self.result
        assert self.sr&~1==sr&~1 and (self.fpscr,self.fr)==(fpscr,fr)
        assert self.calls==["extent"] and self.ccr==self.owner_ccr and not self.wait
        assert self.ccr_writes==([0x909,0x908] if self.owner_ccr==0x100 else [])
        assert self.purged==(self.owner_ccr==0x100)
        assert self.mem[self.args[3]]==(self.args[1]-0x8000 if self.result else self.output_before)
        if self.owner_ccr==0x100:assert not self.dirty


def check_extent_scope(stage,self_test=False):
    marker=stage[4].get("__kui_native_cached_extent")
    if marker is None:return {"enabled":False}
    require(marker==(1,0,0,0xfff1),"invalid pure-proof cache marker")
    begin,body,_=symbol_bytes(stage,"_kui_vmu_native_cached_extent",2)
    ops=(0x2f86,0x2f96,0x4f22,0xd019,0x402b,9,0xd118,0x6012,0x6803,0xd218,
         0x3020,0x8b09,0xd017,0x2102,9,9,9,9,9,9,9,9,0xd013,0x400b,9,0x6903,
         0xd00f,0x3800,0x8b11,0xd011,0xd111,0x00a3,0x7020,0x3012,0x8bfb,
         0xd010,0x208b,0xd109,0x2102,9,9,9,9,9,9,9,9,0x6093,0x4f26,0x69f6,0x68f6,0xb,9,9)
    expected=struct.pack('<54H8I',*ops,begin+12+0x20000000,0xff00001c,0x100,0x909,
                         stage[4]['_kui_vmu_native_program_extent'][0],0x8c000000,0x8d000000,0x808)
    require(body==expected,'linked pure-proof cache scope instructions/literals changed')
    cases=0
    for seed in range(24):
        for mask in range(16):
            for ccr in (0,1,4,0x20,0x80,0x100,0x105,0x1a7):
                for result in (0,1):
                    ExtentGateway(stage,seed,mask,ccr,result).run().check();cases+=1
    rejected=0
    if self_test:
        # Unsafe mode, publication, target and post-CCR spacing mutations
        # must be rejected by execution, independently of the exact pin.
        mutations=[]
        for needle,replacement in ((struct.pack("<I",0x909),struct.pack("<I",0x908)),
                                   (struct.pack("<I",0x808),struct.pack("<I",0x809)),
                                   (struct.pack("<4H",0x00a3,0x7020,0x3012,0x8bfb),
                                    struct.pack("<4H",9,0x7020,0x3012,0x8bfb))):
            at=body.find(needle);require(at>=0,"missing cache-scope mutation target")
            changed=bytearray(body);changed[at:at+len(needle)]=replacement;mutations.append(bytes(changed))
        proof=struct.pack("<I",stage[4]["_kui_vmu_native_program_extent"][0])
        at=body.find(proof);require(at>=0,"cache scope lacks exact pure-proof target")
        changed=bytearray(body);struct.pack_into("<I",changed,at,0x8ce00002);mutations.append(bytes(changed))
        for at in (*range(28,44,2),*range(78,94,2)):
            changed=bytearray(body);struct.pack_into('<H',changed,at,0x6003);mutations.append(bytes(changed))
        for changed in mutations:
            try:ExtentGateway(stage,7,15,0x100,1,changed).run().check()
            except (AssertionError,struct.error,KeyError):rejected+=1
            else:raise ValueError("pure-proof cache scope accepted unsafe mutation")
    return {"enabled":True,"entry":hex(begin),"bytes":len(body),
        "actual_instruction_model_cases":cases,"mutation_rejections":rejected,
        "complete_linked_assembly_pinned":True,
        "eligible_entry_ccr":"0x100","proof_ccr":"0x101",
        "other_ccr_profiles_unchanged":True,"same_pure_extent_proof":True,
        "full_ram_purge_bytes":0x1000000,"cache_write_nops":[8,8],
        "callee_preserved_integer_frame_bytes":12,"operand_cache_restored_off":True,
        "proof_output_published_before_cache_restore":True,"console_speed_verified":False}


def check_common_replay_pin(image):
    """Complete reviewed dual-plan code, with only typed addresses relocated.

    The prefix includes every instruction, branch, copy size, ABI frame
    and short constant. The literal pool binds exact named functions and
    exact private objects/interiors; no nearest-symbol normalization.
    """
    start,actual,section=symbol_bytes(image,'_native_resolution_replay',2)
    prefix=bytes.fromhex(
        '862f962f224f36d0ec7f6595f36735d41077f3660b400c760820078ff45100e8'
        '8360147f264ff6690b00f6682ed8111f53912ed0122f2ed62ed1821f6364f3570b40'
        '12650820ea8d83652bd92bd40b4968e6439683652ad40b49687529d848e629d50b49'
        '836429d529d40b4914e629d529d40b4934e6825002881b8f01e427d00b4009000820'
        'ca8925d112611821178924d01fd114550b4013540820bf8922d00b4009000820ba8d'
        '036820d10b410900b7af836087511821e68b1dd0e0af090086508a511b2003c8a98b'
        '1ad00b4009000820dd8ba4af00e860040004b8010900')
    require(len(prefix)==0xe0 and len(actual)==312 and actual[:0xe0]==prefix and
            section[1]==1 and section[2]&7==6,'Common dual-plan replay instructions changed')
    objects={'_native_cached_resolution':688,'_exec_bytes':4,'_native_driver':104,
             '_passive_layout':440,'_native_heap':72,'_native_call':20,'_native_exits':52,'_native_arena':4}
    for name,count in objects.items():
        value,size,kind,owner=image[4][name];target=image[3][owner]
        require(kind==1 and size==count and target[1]==8 and target[2]&7==3 and
                STAGE<=value<value+size<=LIMIT,'Common replay object role changed: '+name)
    seed=image[4]['__native_resolution_cache_seed_start'][0]
    require(image[4]['__native_resolution_cache_seed_end'][0]-seed==1120,
            'Common replay seed extent changed')
    bindings=(('_kui_vmu_native_cache_seed_read',0),('__native_resolution_cache_seed_start',0),
        ('_native_cached_resolution',0),(replay_name(image),0),(None,0x8c010000),('_exec_bytes',0),
        ('_memcpy',0),('_native_driver',0),('_passive_layout',0),('_native_heap',0),
        ('_native_cached_resolution',544),('_native_cached_resolution',616),('_native_call',0),
        ('_native_cached_resolution',636),('_native_exits',0),('_native_arena_plan_bind',0),
        ('_native_arena',0),('_allocation_safe',0),(coherence_name(image),0),
        ('_native_passive_capture_windows',0),('_native_fixed_plan_bind',0),('_readonly_release_replay_valid',0))
    expected=[]
    for name,offset in bindings:
        if name is None:expected.append(offset);continue
        if name not in objects and name!='__native_resolution_cache_seed_start':
            target,code,owner=symbol_bytes(image,name,2)
            require(code and owner[2]&7==6 and STAGE<=target<target+len(code)<=LIMIT,
                    'Common replay callback role changed: '+name)
        expected.append(image[4][name][0]+offset)
    require(actual[0xe0:]==struct.pack('<22I',*expected),'Common replay typed literal targets changed')


def check(stage, *, self_test=False, preclear_directory=None):
    symbols, sections = stage[4], stage[3]
    preclear = bind_preclear(stage, preclear_directory, self_test=self_test)
    coherence=coherence_binding(stage)
    lean_stage=stage_profile(stage)
    address, size, kind, owner = symbols["_native_cached_resolution"]
    require(kind == 1 and size == 688 and owner < len(sections), "cache metadata must be one typed688B stage object")
    section = sections[owner]
    require(section[1] == 8 and section[2] & 7 == 3 and
            STAGE <= address < address + size <= LIMIT and
            section[3] <= address < address + size <= section[3] + section[5],
            "temporary cache metadata must remain in high-stage BSS")
    begin, body, _ = symbol_bytes(stage, "_native_resolution_replay", 2)
    def pinned_replay(image):
        _,actual,section=symbol_bytes(image,'_native_resolution_replay',2)
        pins=stage_pins(image,'cache_gateway',{})
        require(set(pins)=={'_native_resolution_replay'} and section[2]&7==6 and
                (len(actual),hashlib.sha256(actual).hexdigest())==pins['_native_resolution_replay'],
                'Lean General complete cache replay instructions/targets changed')
    normalized_common=bool(lean_stage and '__kui_native_cached_resolution' in symbols and
                           '_native_arena_plan_bind' in symbols)
    if lean_stage:
        (check_common_replay_pin if normalized_common else pinned_replay)(stage)
    cases = 0
    for seed in range(24):
        for mask in range(16):
            for flags in range(32):
                outcomes = [(flags >> i) & 1 for i in range(5)]
                CacheGateway(stage, seed, mask, outcomes).run().check(); cases += 1
    fixed_cases=0
    if lean_stage:
        for seed in range(24):
            for mask in range(16):
                for flags in range(64):
                    CacheGateway(stage,seed,mask,[(flags>>i)&1 for i in range(6)],
                                 fixed_fallback=True).run().check();fixed_cases+=1
    # An arena lease record (The House of the Dead 2): only the dual stage's
    # arena route binds its plan again; no release entry is admitted.
    arena_route='_native_arena_plan_bind' in symbols
    require(not arena_route or lean_stage,'arena lease replay requires the exact lean General stage')
    arena_cases=0
    if arena_route:
        for seed in range(24):
            for mask in range(16):
                for flags in range(64):
                    CacheGateway(stage,seed,mask,[(flags>>i)&1 for i in range(6)],
                                 arena=True).run().check();arena_cases+=1
    replay_modes=((False,False),(True,False),(False,True)) if arena_route else \
        ((False,False),(True,False)) if lean_stage else ((False,False),)
    routes = 0
    background='_native_background_boot_region' in symbols
    modes = ((0,2,4,5,6) if background else (0,1,2,3,4,5,6)) if "_native_runtime_mode" in symbols else (0,)
    for seed in range(24):
        for mask in range(16):
            for mode in modes:
                for normalized in (False,True):
                    options=((False,0),(True,0),(True,1),(True,2),(True,3),(True,4),(True,5),(True,6))
                    if background:options+=((True,7),)
                    for eager in ((False,True) if background else (False,)):
                        for hit,invalid in options:
                            CacheRoute(stage,seed,mask,hit,invalid,mode=mode,normalized=normalized,eager=eager,
                                       preclear=preclear).run().check();routes+=1
    preclear_refusals = 0
    if preclear is not None:
        for seed in (0, 1):
            for mask in (0, 15):
                for mode in modes:
                    for normalized in (False, True):
                        for eager in (False, True):
                            for detail in (0x53, 0x54):
                                CacheRoute(stage, seed, mask, True, mode=mode, normalized=normalized,
                                    eager=eager, preclear=preclear, preclear_refusal=detail).run().check()
                                preclear_refusals += 1
    mutations = 0
    if self_test:
        # Mutate every actual callback literal and an exact memcpy length.
        targets = [symbols[name][0] for name in ("_kui_vmu_native_cache_seed_read",
            replay_name(stage), "_allocation_safe", "_readonly_release_replay_valid",
            coherence_name(stage),
            "_native_passive_capture_windows")]
        if lean_stage:targets.append(symbols['_native_fixed_plan_bind'][0])
        if arena_route:targets.append(symbols['_native_arena_plan_bind'][0])
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
            rejected=False
            for fixed,arena in replay_modes:
                try:CacheGateway(stage,7,15,[1]*(6 if fixed or arena else 5),changed,
                                 fixed_fallback=fixed,arena=arena).run().check()
                except (AssertionError,struct.error,KeyError):rejected=True;break
            if rejected:mutations+=1
            else:
                raise ValueError("cache wrapper accepted a callback/copy mutation")
        words = struct.unpack("<" + "H" * (len(body) // 2), body)
        gates = [i + 1 for i in range(len(words) - 1) if words[i] == 0x2008 and
                 words[i + 1] >> 8 in (0x89, 0x8b, 0x8d, 0x8f)]
        if arena_route:
            # Seed, replay, the shared fixed/arena plan call, allocation,
            # SDK slot and release results.
            require(tuple(2*i for i in gates)==(0x1a,0x46,0x86,0x9c,0xa6,0xd2),
                    'Dual stage must retain all six exact callback result gates')
        elif lean_stage:
            require(tuple(2*i for i in gates)==(0x1a,0x46,0x90,0x9e,0xa8,0xbe),
                    'Lean General must retain all six exact callback result gates')
        else:
            require(len(gates) == 5, "cache wrapper must branch on all five callback results")
        for gate in gates:
            changed = bytearray(body); struct.pack_into("<H", changed, gate * 2, 9)
            rejected=False
            for fixed,arena in replay_modes:
                bits=6 if fixed or arena else 5
                for flags in range(1<<bits):
                    try:CacheGateway(stage,7,15,[(flags>>i)&1 for i in range(bits)],bytes(changed),
                                     fixed_fallback=fixed,arena=arena).run().check()
                    except (AssertionError,struct.error,KeyError):rejected=True;break
                if rejected:break
            if rejected:mutations+=1
            else:
                raise ValueError("cache wrapper accepted a missing result guard")
        def literal(index):
            op=words[index];at=((begin+index*2+4)&~3)+4*(op&255)
            return struct.unpack_from('<I',body,at-begin)[0]
        if arena_route:
            # Family 2 binds the arena plan (R4=1), then refuses on its result;
            # the arena route flag alone skips the allocator release entry.
            # Otherwise a zero initializer slot binds the fixed plan through
            # the same call and result gate.
            require(words[0x76//2:0x90//2]==(0x5082,0x8802,0x8f1b,0xe401,0xd027,0x400b,9,0x2008,0x89ca,
                                              0xd125,0x6112,0x2118,0x8917) and
                    literal(0x7e//2)==symbols['_native_arena_plan_bind'][0] and
                    literal(0x88//2)==symbols['_native_arena'][0] and
                    words[0xb4//2:0xc0//2]==(0x5187,0x2118,0x8be6,0xd01d,0xafe0,9) and
                    literal(0xba//2)==symbols['_native_fixed_plan_bind'][0],
                    'Dual stage arena/fixed-plan selection/callback/refusal block changed')
            for at,replacement,mode,label in ((0xb8,0x89e6,(True,False),'required fixed-plan fallback'),
                                              (0x7a,0x8d1b,(False,True),'required arena plan'),
                                              (0x7a,0x8d1b,(False,False),'allocator release admission'),
                                              (0x8e,0x8b17,(False,True),'arena route without release')):
                changed=bytearray(body);struct.pack_into('<H',changed,at,replacement)
                try:CacheGateway(stage,7,15,[1]*(5 if mode==(False,False) else 6),bytes(changed),
                                 fixed_fallback=mode[0],arena=mode[1]).run().check()
                except (AssertionError,struct.error,KeyError):mutations+=1
                else:raise ValueError('cache wrapper bypassed its '+label)
        elif lean_stage:
            require(words[0x76//2:0x7c//2]==(0x5197,0x2118,0x891c) and
                    words[0xb6//2:0xc6//2]==(0xd018,0x400b,0xe401,0x2008,0x8fdd,0xe800,0xafae,0x6083),
                    'Lean General fixed-plan selection/callback/refusal block changed')
            changed=bytearray(body);struct.pack_into('<H',changed,0x7a,9)
            try:CacheGateway(stage,7,15,[1]*6,bytes(changed),fixed_fallback=True).run().check()
            except (AssertionError,struct.error,KeyError):mutations+=1
            else:raise ValueError('cache wrapper bypassed its required fixed-plan fallback')
    pin_mutations=0
    if self_test and lean_stage:
        _,_,section=symbol_bytes(stage,'_native_resolution_replay',2)
        offset=section[4]+begin-section[3]
        for index in range(len(body)):
            changed=bytearray(stage[0]);changed[offset+index]^=1
            try:(check_common_replay_pin if normalized_common else pinned_replay)((bytes(changed),*stage[1:]))
            except ValueError:pin_mutations+=1
            else:raise ValueError('Lean cache replay pin accepted changed instructions or targets')
    extent_scope=check_extent_scope(stage,self_test)
    resolution_scope={'enabled':False}
    if replay_name(stage)=='_kui_vmu_native_cached_resolution_replay':
        require(preclear_directory is not None,'Common replay scope requires its exact stage artifact directory')
        path=Path(preclear_directory)/'stage.elf'
        require(path.read_bytes()==stage[0],'Common replay scope directory differs from parsed stage')
        sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
        from test_native_stage_proof_scope import audit as proof_scope
        resolution_scope=dict(proof_scope(path,self_test=self_test),enabled=True)
        require(resolution_scope.get('status')=='PASS' and resolution_scope.get('argument_count')==7 and
                resolution_scope.get('actual_SH_cases')==3840 and
                resolution_scope.get('mutation_rejections')==(26 if self_test else 0),
                'Common replay scope lacks its complete linked argument/cache/publication proof')
    discovery_scope={'enabled':False}
    if discovery_name(stage)=='_kui_native_stage_cached_discover_image':
        require(preclear_directory is not None,'Fresh discovery scope requires its actual stage artifact directory')
        path=Path(preclear_directory)/'stage.elf'
        require(path.read_bytes()==stage[0],'Discovery scope directory differs from parsed stage')
        from test_native_stage_discovery_scope import audit as discovery_audit
        discovery_scope=dict(discovery_audit(path),enabled=True)
        require(discovery_scope.get('status')=='PASS' and discovery_scope.get('argument_count')==4 and
                discovery_scope.get('actual_SH_cases')==792 and discovery_scope.get('unsafe_mutation_rejections')==38,
                'Fresh discovery lacks its linked ABI/cache/publication and caller proof')
    return {"enabled": True, "temporary_resolution_address": hex(address), "temporary_resolution_bytes": size,
            "temporary_high_bss_only": True, "wrapper_entry": hex(begin), "wrapper_bytes": len(body),
            "actual_instruction_model_cases": cases, "mutation_rejections": mutations,
            "actual_outer_relay_route_cases": routes, "executable_restored_before_replay": True,
            "fresh_scan_selected_only_on_cache_miss": True, "launch_eligibility_before_replay": True,
            "full_source_restored_before_extent_selection":True,
            "validated_extent_before_cache_or_discovery":True,
            "normal_and_shortened_extent_routes_checked":True,
            "extent_failure_keeps_source_length_and_refuses":True,"pure_extent_cache_scope":extent_scope,
            "pure_resolution_cache_scope":resolution_scope,
            "pure_discovery_cache_scope":discovery_scope,"coherence_same_image_proof":coherence,
            "preheap_clear_boundary":dict(enabled=preclear is not None,
                typed_void_no_arguments=True if preclear else None,
                exact_call_and_code_sha_window=preclear['window'] if preclear else None,
                actual_original_companion=preclear['actual_companion'] if preclear else None,
                terminal_refusal_boundary_cases=preclear_refusals,
                model_scope='Cached-hit relay continuation only; preclear internals and their actual original execution remain in the separately mandatory artifact/source-bound companion'),
            "no_metadata_before_exact_image_replay": True, "fresh_allocation_and_release_guards": True,
            "fresh_sdk_initializer_slot_admission":True,"sdk_slot_admission_is_independent_image_predicate":True,
            "capture_only_after_all_guards": True,
            "fixed_plan_fallback":dict(enabled=lean_stage,actual_wrapper_instruction_cases=fixed_cases,
                callback_result_combinations=64 if lean_stage else 0,
                initializer_slot_offset=544+28,initializer_target_offset=544+32,
                fixed_callback_argument_one=True if lean_stage else None,
                exact_six_result_gate_offsets=([0x1a,0x46,0x86,0x9c,0xa6,0xd2] if arena_route else
                                               [0x1a,0x46,0x90,0x9e,0xa8,0xbe] if lean_stage else []),
                capture_requires_fixed_release_allocation_sdk_admissions=lean_stage,
                complete_wrapper_byte_mutation_rejections=pin_mutations,
                typed_relocated_common_template=normalized_common,
                model_scope='Actual wrapper instructions; fixed-plan is a typed success/failure API boundary, with its two consumed private heap-output fields modeled; fixed-plan decoder itself is separately proved'),
            "arena_lease_replay":dict(enabled=arena_route,actual_wrapper_instruction_cases=arena_cases,
                callback_result_combinations=64 if arena_route else 0,heap_family_offset=544+8,
                arena_callback_argument_one=True if arena_route else None,
                release_entry_admitted=False if arena_route else None,
                capture_requires_arena_plan_allocation_sdk_admissions=arena_route,
                model_scope='Actual wrapper instructions; the arena plan bind is a typed success/failure API boundary that sets the arena route flag; the plan itself is separately proved')}
