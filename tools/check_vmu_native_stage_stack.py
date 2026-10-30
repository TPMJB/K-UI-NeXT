#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prove the temporary post-initializer stack from typed ELF and actual SH.

The ordinary entry/relay stack remains at its package ABI address. Only the
post-initializer installer uses the separately linked BSS object. Models use
adversarial ABI callees, not a replacement implementation of the SDK allocator.
The exact SDK write set and allocation exclusions have separate C/SH tests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from check_loader_layout import inspect_elf
from check_vmu_native_worker_gateway import CPU, MASK, read_elf, symbol_bytes
from sh4_delay_slots import validate_delay_slot

STAGE, LIMIT, OLD_STACK = 0x8CE00000, 0x8CFE0000, 0x8CFF0000
STACK_SYMBOL, STACK_BYTES = "_native_installer_stack", 65536
HELPER = "_kui_vmu_native_stage_allocation_safe"
BOTTOM_HELPER = "_kui_vmu_native_stage_bottom_allocation_safe"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def canonical(address):
    physical = address & 0x1FFFFFFF
    if address & 0xE0000000 in (0, 0x80000000, 0xA0000000) and 0x0C000000 <= physical < 0x0D000000:
        return physical | 0x80000000
    return address


def allocation_helper(stage):
    from check_native_followup_profile import bottom_stage_profile
    return BOTTOM_HELPER if bottom_stage_profile(stage) else HELPER


def typed_stack(stage):
    """Return the complete zero-filled object; no inferred unused RAM is owned."""
    symbols, sections, segments = stage[4], stage[3], stage[2]
    helper = allocation_helper(stage)
    if STACK_SYMBOL not in symbols:
        require(HELPER not in symbols and BOTTOM_HELPER not in symbols,
                "native stage allocation helper lacks its typed installer stack")
        require(helper != BOTTOM_HELPER, "bottom-heap stage lacks its typed installer stack")
        return None
    address, size, kind, owner = symbols[STACK_SYMBOL]
    require(size == STACK_BYTES and kind == 1 and owner < len(sections) and not address & 31,
            "native installer stack must be one aligned typed64KiB object")
    section = sections[owner]
    require(section[1] == 8 and section[2] & 7 == 3 and section[8] >= 32 and
            section[3] <= address < address + size <= section[3] + section[5],
            "native installer stack must remain in aligned writable zero-fill BSS")
    begin, end = symbols["__retail_stage_bss_begin"][0], symbols["__retail_stage_bss_end"][0]
    binary_end = symbols["__retail_stage_binary_end"][0]
    require(STAGE <= binary_end <= begin <= address < address + size <= end <= LIMIT,
            "native installer stack escapes complete linked stage/BSS protection")
    loads = [p for p in segments if p[0] == 1 and p[2] <= address < address + size <= p[2] + p[5]]
    require(len(loads) == 1 and loads[0][2] == loads[0][3] and loads[0][6] & 2 and
            loads[0][2] + loads[0][4] <= address,
            "native installer stack must be owned by one writable zero-fill load")
    for name, (other, count, other_kind, _) in symbols.items():
        if name == STACK_SYMBOL or not count or other_kind != 1:
            continue
        require(address + size <= canonical(other) or canonical(other) + count <= address,
                "native installer stack overlaps another typed object: " + name)
    require(helper in symbols and symbols[helper][2] == 2,
            "native installer stack lacks its exact live allocation helper")
    return address, size, address + size - 32, begin, end


class Machine(CPU):
    def __init__(self, image, symbol, seed, mask, stack, *, body=None):
        base, original, _ = symbol_bytes(image, symbol, 2)
        super().__init__(seed, base, 1)
        self.image, self.blob = image, original if body is None else body
        self.pc, self.r[15], self.mem = base, stack, {}
        self.start_sp = stack
        self.sr = (self.sr & ~0xF0) | mask << 4
        self.pr = base + len(self.blob) + 0x100
        self.before = self.r.copy(), self.pr, self.mach, self.macl, self.gbr, self.sr, self.fpscr, self.fr.copy()
        self.stop_pc, self.calls = self.pr, []

    def code(self, address, size):
        require(self.base <= address < address + size <= self.base + len(self.blob),
                "native stack model leaves its exact instruction body")
        return self.blob[address - self.base:address - self.base + size]

    def read(self, address):
        assert not address & 3
        key = canonical(address)
        if key in self.mem:
            return self.mem[key]
        return struct.unpack("<I", self.code(address, 4))[0]

    def write(self, address, value):
        assert not address & 3 and self.start_sp - 512 <= address < self.start_sp
        self.mem[canonical(address)] = value & MASK

    def plain(self, word, pc):
        n, m = word >> 8 & 15, word >> 4 & 15
        if word & 0xF0FF == 0x0002:
            self.r[n] = self.sr; return
        if word & 0xF0FF == 0x400E:
            self.sr = self.r[n]; return
        if word == 0x4F07:
            self.sr = self.read(self.r[15]); self.r[15] += 4; return
        if word & 0xF0FF == 0x002A:
            self.r[n] = self.pr; return
        if word & 0xF0FF == 0x0029:
            self.r[n] = self.sr & 1; return
        if word & 0xF0FF == 0x4008:
            self.r[n] = self.r[n] << 2 & MASK; return
        if word & 0xF0FF == 0x4011:  # cmp/pz Rn: signed Rn >= 0
            self.t(not (self.r[n] & 0x80000000)); return
        if word & 0xF0FF == 0x00A3:
            self.published.append(self.r[n] & ~31); return
        if word & 0xF00F == 0x2009:
            self.r[n] &= self.r[m]; return
        if word & 0xF00F == 0x200B:
            self.r[n] |= self.r[m]; return
        if word & 0xF00F == 0x2008:
            self.t((self.r[n] & self.r[m]) == 0); return
        if word & 0xF00F == 0x3006:
            self.t(self.r[n] > self.r[m]); return
        if word & 0xF00F == 0x300C:
            self.r[n] = (self.r[n] + self.r[m]) & MASK; return
        if word & 0xF00F == 0x3008:
            self.r[n] = (self.r[n] - self.r[m]) & MASK; return
        if word >> 8 == 0x88:
            immediate = word & 255; immediate -= 256 if immediate & 128 else 0
            self.t(self.r[0] == immediate & MASK); return
        if word >> 8 == 0xC8:
            self.t((self.r[0] & (word & 255)) == 0); return
        if word >> 8 == 0xC9:
            self.r[0] &= word & 255; return
        if word >> 8 == 0xC7:
            self.r[0] = ((pc + 4) & ~3) + 4 * (word & 255); return
        if word >> 12 == 9:
            self.r[n] = struct.unpack("<h", self.code(pc + 4 + 2 * (word & 255), 2))[0] & MASK; return
        super().plain(word, pc)

    def abi_return(self, result=0):
        return_pc = self.pr
        for i in range(8):
            self.r[i] = (0x913579BD + 0x13579BD * i + len(self.calls)) & MASK
        self.r[0] = result & MASK
        self.sr ^= 1; self.pr ^= 0x2468ACE0; self.pc = return_pc

    def call(self, target):
        return False

    def run(self):
        self.published = []
        for _ in range(getattr(self, 'instruction_limit', 1200)):
            if self.pc == self.stop_pc:
                return self
            pc = self.pc; word = struct.unpack("<H", self.code(pc, 2))[0]
            if word >> 12 in (0xA, 0xB) or word & 0xF0FF in (0x400B, 0x402B) or word == 0x000B:
                if word >> 12 in (0xA, 0xB):
                    d = word & 4095; d -= 4096 if d & 2048 else 0
                    target = pc + 4 + 2 * d
                    if word >> 12 == 0xB:
                        self.pr = pc + 4
                elif word == 0x000B:
                    target = self.pr
                else:
                    target = self.r[word >> 8 & 15]
                    if word & 0xF0FF == 0x400B:
                        self.pr = pc + 4
                slot = struct.unpack("<H", self.code(pc + 2, 2))[0]
                validate_delay_slot(slot, pc)
                self.plain(slot, pc + 2)
                if not self.call(target):
                    self.pc = target
            elif word >> 8 in (0x89, 0x8B, 0x8D, 0x8F):
                d = word & 255; d -= 256 if d & 128 else 0
                taken = bool(self.sr & 1) == (word >> 8 in (0x89, 0x8D))
                delayed = word >> 8 in (0x8D, 0x8F)
                if delayed:
                    slot = struct.unpack("<H", self.code(pc + 2, 2))[0]
                    validate_delay_slot(slot, pc)
                    self.plain(slot, pc + 2)
                self.pc = pc + 4 + 2 * d if taken else pc + (4 if delayed else 2)
            else:
                self.plain(word, pc); self.pc += 2
        raise AssertionError("unbounded native stack instruction path")


class AllocationWrapper(Machine):
    def __init__(self, stage, seed, mask, outcome, *, body=None):
        super().__init__(stage, "_allocation_safe", seed, mask, OLD_STACK, body=body)
        self.r[4:6] = [0x8CC00000, 0x00400000]
        self.symbols, self.outcome = stage[4], outcome
        self.helper = allocation_helper(stage)
        self.mem[self.symbols["_exec_bytes"][0]] = 0x200000
        self.before = self.r.copy(), self.pr, self.mach, self.macl, self.gbr, self.sr, self.fpscr, self.fr.copy()

    def call(self, target):
        if target == self.stop_pc:
            return False
        assert target == self.symbols[self.helper][0], "allocation wrapper changed its exact helper"
        self.calls.append("allocation")
        assert self.r[4:8] == [self.symbols["_native_heap"][0], 0x200000, STAGE,
                               self.symbols["__retail_stage_bss_end"][0]]
        assert [self.read(self.r[15] + 4 * i) for i in range(2)] == [0x8CC00000, 0x00400000]
        self.abi_return(int(self.outcome)); return True

    def check(self):
        before, pr, _, _, _, sr, fpscr, fr = self.before
        assert self.calls == ["allocation"] and self.r[0] == int(self.outcome)
        assert self.r[8:] == before[8:] and self.pr == pr and self.sr & ~1 == sr & ~1
        assert self.fpscr == fpscr and self.fr == fr


class ArmRoute(Machine):
    """Execute the complete relay through publication of the low boot control."""
    def __init__(self, stage, resident, stack, seed, mask, *, body=None, helper_bodies=None,
                 cache_hit=True, mode=0, sdk_admitted=True, normalized=False, crt_admitted=True):
        super().__init__(stage, "_kui_retail_stage_relay", seed, mask, OLD_STACK, body=body)
        self.symbols, self.stack = stage[4], stack
        self.callback_helpers, self.callback_helper_calls = {}, []
        self.callback_table = None
        self.selected_reader = None
        marker=self.symbols.get('__kui_native_dual_low_callbacks_stage')
        require(marker is None or marker==(1,0,0,0xfff1),
                'Native stack selected-callback marker differs')
        self.selected_callbacks=marker is not None
        if self.selected_callbacks:
            from check_native_dual_callbacks import (SELECTOR,ARM,TABLE,PINS,
                                                      canonical_helper,expected_entries)
            require(helper_bodies is None or set(helper_bodies)<={ARM,SELECTOR},
                    'Native stack helper override escapes the actual callback helpers')
            for name in (ARM,SELECTOR):
                original,normalized=canonical_helper(stage,name)
                require((len(original),hashlib.sha256(normalized).hexdigest())==PINS[name],
                        'Native stack actual callback helper changed: '+name)
                override=(helper_bodies or {}).get(name,original)
                require(len(override)==len(original),'Native stack helper override changes its extent')
                self.callback_helpers[self.symbols[name][0]]=(name,override)
            address,table,section=symbol_bytes(stage,TABLE,1)
            require(len(table)==48 and not address&3 and section[2]&7==2,
                    'Native stack callback table is not immutable two-row six-word data')
            self.selected_reader=1 if '__kui_native_sdk_sci_route_resident' in resident[4] else 2
            offset=24*(self.selected_reader-1)
            require(struct.unpack_from('<6I',table,offset)==expected_entries(resident),
                    'Native stack selected table row differs from its actual low reader')
            self.callback_table=(address,table)
        else:
            require(not helper_bodies,'Historical relay cannot override selected callback helpers')
        self.cache_hit, self.mode = cache_hit, mode
        self.sdk_admitted,self.refused=sdk_admitted,False
        self.crt_admitted=crt_admitted
        self.control = resident[4]["_kui_vmu_observe_boot_control"][0]
        self.cell = resident[4]["_kui_vmu_native_heap_original"][0]
        self.heap_hook = resident[4]["_kui_vmu_observe_heap_hook"][0]
        self.return_hook = resident[4]["_kui_vmu_observe_return_hook"][0]
        self.frame, self.slot, self.original = 0x8C00ECAC, 0x8C0101BC, 0x8C011014
        self.r[4:6] = [self.frame, 0x105]
        self.before = self.r.copy(), self.pr, self.mach, self.macl, self.gbr, self.sr, self.fpscr, self.fr.copy()
        self.mem.update({self.control + 4 * i: 0 for i in range(16)})
        self.mem.update({self.cell + 4 * i: 0 for i in range(8)})
        self.mem[self.slot] = self.original
        self.mem[self.frame + 20] = 0x8C010000
        self.exec_address=self.symbols["_exec_bytes"][0]
        self.source_bytes=0x200000
        self.program_bytes=self.source_bytes-(0x8000 if normalized else 0)
        self.mem[self.exec_address] = self.source_bytes
        manifest = self.symbols["_manifest"][0]
        compact_transport=self.symbols.get('__kui_native_vmu_only_transport_stage')
        if compact_transport is not None:
            require(compact_transport in ((0,0,0,0xfff1),(1,0,0,0xfff1)),
                    'VMU-only stage transport marker differs')
        self.mem.update({manifest + 44: compact_transport[0] if compact_transport is not None else 1,
                         manifest + 48: (self.selected_reader if self.selected_callbacks else
                                         1 if '_native_background_boot_region' in self.symbols else 0),
                         manifest + 52: 0})
        for name, count in (("_native_heap", 18), ("_native_call", 5), ("_native_driver", 26)):
            self.mem.update({self.symbols[name][0] + 4 * i: 0 for i in range(count)})
        # An initializer-call record leaves the fixed-arena flag clear; that
        # route is executed by check_native_fixed_arena.py.
        if "_native_fixed" in self.symbols:
            self.mem[self.symbols["_native_fixed"][0]] = 0
        heap, call = self.symbols["_native_heap"][0], self.symbols["_native_call"][0]
        self.mem.update({heap + 24: self.original, heap + 28: self.slot, heap + 32: self.original,
                         call + 8: 0x8C0100C6, call + 12: 0x8CC00000, call + 16: 0x00400000})
        extent_name=("_kui_vmu_native_cached_extent" if "__kui_native_cached_extent" in self.symbols
                     else "_kui_vmu_native_program_extent")
        labels = (("_retail_display_restore", "display"), ("_retail_display_line", "line"),
                  ("_kui_native_heap_base_relay", "restore"), ("_kui_retail_observe_native_ip", "ip"),
                  (extent_name, "extent"),
                  ("_native_resolution_replay", "cache"), ("_code_sha", "sha"),
                  ("_kui_vmu_native_discover_layout", "driver"), ("_kui_vmu_native_heap_discover", "heap"),
                  ("_kui_vmu_native_heap_call_discover", "call"), ("_allocation_safe", "allocation"),
                  ("_native_passive_prepare", "prepare"), ("_native_report", "refused"),
                  ("_readonly_release_replay_valid", "release"),
                  ("_native_sdk_coherence_cached_admit", "sdk-slot"),
                  ("_kui_vmu_native_exit_discover_guarded", "exits"),
                  ("_native_passive_capture_windows", "capture"), ("_native_runtime_mode", "mode"),
                  ("_native_crt_bootstrap_prearm", "crt-prearm"))
        self.targets = {self.symbols[name][0]: label for name, label in labels if name in self.symbols}
        self.armed = False
        self.nonstack_writes = []

    def code(self,address,size):
        for base,(_,body) in self.callback_helpers.items():
            if base<=address<address+size<=base+len(body):
                return body[address-base:address-base+size]
        return super().code(address,size)

    def read(self,address):
        if self.callback_table is not None:
            base,body=self.callback_table
            if base<=address<address+4<=base+len(body):
                require(not address&3,'Native stack callback row read is unaligned')
                return struct.unpack_from('<I',body,address-base)[0]
        return super().read(address)

    def plain(self,word,pc):
        if word&0xf00f==0x000e:  # MOV.L @(R0,Rm),Rn; capture both sources before Rn.
            n,m=word>>8&15,word>>4&15
            self.r[n]=self.read((self.r[0]+self.r[m])&MASK)
            return
        super().plain(word,pc)

    def write(self, address, value):
        key = canonical(address)
        if self.callback_table is not None:
            base,body=self.callback_table
            assert key+4<=base or base+len(body)<=key, 'relay writes its immutable callback table'
        allowed = (self.start_sp - 256 <= key < self.start_sp or
                   STAGE <= key < self.symbols["__retail_stage_bss_end"][0] or
                   self.control <= key < self.control + 64 or self.cell <= key < self.cell + 32 or
                   key in (self.slot, self.frame + 20))
        assert not address & 3 and allowed, "relay store leaves its exact owned objects"
        if not self.start_sp - 256 <= key < self.start_sp:
            self.nonstack_writes.append((key, value & MASK))
        if key == self.control + 8 and value == 1:
            assert self.calls.count("extent")==1 and self.mem[self.exec_address]==self.program_bytes
            assert self.mem[self.control] == self.symbols["_kui_vmu_observe_stage_install"][0]
            assert self.mem[self.control + 4] == self.stack[2], "ARMED before publishing typed installer stack"
            assert [self.mem[self.cell + i * 4] for i in range(4)] == [self.original, 0x8CC00000, 0x00400000, 0x0C0100C6]
            self.armed = True
        self.mem[key] = value & MASK

    def call(self, target):
        if target == self.stop_pc:
            return False
        if target in self.callback_helpers:
            self.callback_helper_calls.append(self.callback_helpers[target][0])
            return False
        if any(base<=target<base+len(body) for base,(_,body) in self.callback_helpers.items()):
            return False
        if self.base <= target < self.base + len(self.blob):
            return False
        assert target in self.targets, "unexpected native relay callback"
        label = self.targets[target]; self.calls.append(label)
        if label=="refused":
            if self.calls[-2]=="crt-prearm":
                assert not self.crt_admitted and self.sdk_admitted and self.armed
                assert self.r[4:6]==[2,0x48],"cold CRT prearm refusal changed its exact terminal diagnostic"
            else:
                assert not self.sdk_admitted and not self.cache_hit and not self.armed and self.calls[-2]=="sdk-slot"
                assert self.r[4:6]==[2,13],"cold SDK-slot refusal changed its exact terminal diagnostic"
            self.refused,self.pc=True,self.stop_pc;return True
        if label == "restore":
            assert self.r[4:6] == [self.frame, 0x105]
            assert self.mem[self.exec_address]==self.source_bytes
        if label == "mode":
            assert self.calls.count("restore") == 1 and not any(value in self.calls for value in
                ("ip", "extent", "cache", "driver", "heap", "call", "allocation", "prepare", "sha", "release", "exits", "sdk-slot", "capture"))
        if label in ("ip", "extent", "cache", "driver", "heap", "call", "allocation", "prepare", "sha", "release", "exits", "sdk-slot", "capture"):
            assert self.calls.count("restore") == 1
        if label=="extent":
            assert self.calls.count("ip")==1 and self.r[4:7]==[0x8c010000,self.source_bytes,0x8c010000]
            assert self.mem[self.exec_address]==self.source_bytes
            self.write(self.r[7],self.program_bytes)
        if label in ("cache","driver","heap","call"):
            assert self.calls.count("extent")==1 and self.mem[self.exec_address]==self.program_bytes
        # With a cache-bearing stage use the hit path; without it exercise the
        # same common publisher after the reviewed full-discovery callbacks.
        if label in ("driver", "heap"):
            assert self.r[4:7] == [0x8C010000, self.program_bytes, 0x8C010000]
        if label == "call":
            assert self.r[4:8] == [0x8C010000, self.program_bytes, self.symbols["_native_heap"][0], self.symbols["_native_call"][0]]
        if label == "allocation":
            assert self.r[4:6] == [0x8CC00000, 0x00400000]
        if label=="sdk-slot":
            assert not self.cache_hit and not self.armed and self.calls[-2]=="exits"
            assert all(self.calls.count(name)==1 for name in ("driver","heap","call","allocation","release","exits"))
        if label=="capture" and "_native_sdk_coherence_cached_admit" in self.symbols:
            assert self.sdk_admitted and self.calls[-2]=="sdk-slot","cold windows captured before SDK-slot admission"
        if label=="crt-prearm":
            assert self.calls.count("sha")==1, "CRT entry patched before immutable proof completed"
            if "fixed-arm" in self.calls:
                # This dispatch-only test models the fixed arm; its actual
                # stores are proved by the separate fixed-arena CPU audit.
                assert self.calls[-2]=="fixed-arm"
            else:
                assert self.armed and self.mem[self.control+8]==1
                assert self.mem[self.slot]==self.heap_hook and self.mem[self.frame+20]==self.return_hook
            assert self.calls.count("crt-prearm")==1, "CRT entry prearm repeated during initial relay"
        result = (self.mode if label == "mode" else int(self.cache_hit) if label == "cache"
                  else int(self.sdk_admitted) if label=="sdk-slot"
                  else int(self.crt_admitted) if label=="crt-prearm"
                  else int(label in ("ip", "extent", "driver", "heap", "call", "allocation", "release", "exits")))
        self.abi_return(result)
        return True

    def check(self):
        registers, pr, _, _, _, sr, fpscr, fr = self.before
        if not self.crt_admitted and self.mode!=1:
            assert self.refused and self.armed and self.calls[-2:]==["crt-prearm","refused"]
            assert self.calls.count("sha")==1 and self.mem[self.slot]==self.heap_hook
            return
        if not self.sdk_admitted and self.mode!=1:
            assert self.refused and not self.armed and not any(label in self.calls for label in ("capture","sha"))
            assert self.mem[self.slot]==self.original and self.mem[self.frame+20]==0x8C010000
            assert all(self.mem[self.control+4*i]==0 for i in range(16))
            assert all(self.mem[self.cell+4*i]==0 for i in range(8))
            assert not self.callback_helper_calls
            return
        if self.mode == 1:
            assert not self.armed and not self.nonstack_writes
            assert self.calls.count("restore") == 1 and self.calls.count("mode") == 1
            assert not any(value in self.calls for value in
                ("ip", "extent", "cache", "driver", "heap", "call", "allocation", "prepare", "sha", "release", "exits", "sdk-slot", "capture"))
            assert all(self.mem[self.control + 4 * i] == 0 for i in range(16))
            assert all(self.mem[self.cell + 4 * i] == 0 for i in range(8))
            assert self.mem[self.slot] == self.original and self.mem[self.frame + 20] == 0x8C010000
            assert not self.callback_helper_calls
        else:
            assert self.armed and self.mem[self.control + 4] == self.stack[2]
            assert self.mem[self.slot] == self.heap_hook and self.mem[self.frame + 20] == self.return_hook
            assert self.mem[self.control + 8] == 1 and self.calls.count("sha") == 1
            if "_native_crt_bootstrap_prearm" in self.symbols:
                assert self.calls[-1]=="crt-prearm" and self.calls.count("crt-prearm")==1
            if not self.cache_hit and "_native_sdk_coherence_cached_admit" in self.symbols:
                assert self.calls.count("sdk-slot")==1 and self.calls.index("sdk-slot")<self.calls.index("capture")
            if self.selected_callbacks:
                assert self.callback_helper_calls==['_native_arm_heap_hook','_native_low_bindings'], \
                    'relay did not execute both actual selected callback helpers'
        assert self.r[8:] == registers[8:] and (self.mode == 1 or self.pr == pr) and self.sr & ~1 == sr & ~1
        assert self.fpscr == fpscr and self.fr == fr


class HeapHook(Machine):
    """Real low gateway; allocation oracle overwrites the departed relay stack."""
    def __init__(self, resident, stage, stack, seed, mask, *, alias=0x80000000, body=None,
                 owner_sp=0x0C00ED00, expect_refusal=False):
        super().__init__(resident, "_kui_vmu_observe_heap_hook", seed, mask, alias | owner_sp, body=body)
        self.expect_refusal=expect_refusal
        self.symbols, self.stage, self.stack = resident[4], stage, stack
        self.control = self.symbols["_kui_vmu_observe_boot_control"][0]
        self.cell = self.symbols["_kui_vmu_native_heap_original"][0]
        self.initializer, self.installer = 0x8C011014, stage[4]["_kui_vmu_observe_stage_install"][0]
        self.r[4:6] = [alias | 0x0CC00000, 0x00400000]
        self.before = self.r.copy(), self.pr, self.mach, self.macl, self.gbr, self.sr, self.fpscr, self.fr.copy()
        self.mem.update({self.control + 4 * i: 0 for i in range(16)})
        self.mem.update({self.control: self.installer, self.control + 4: stack[2], self.control + 8: 1,
                         self.cell: self.initializer, self.cell + 4: self.r[4], self.cell + 8: self.r[5],
                         self.cell + 12: self.pr & 0x1FFFFFFF})
        # Include the local publisher helper immediately after its typed caller.
        payload = inspect_elf(resident[0], 0x8C004000,
            0x8c007ba0 if '_kui_vmu_native_background_manifest_bind' in resident[4] else 0x8C007800)["payload"]
        count = len(self.blob)
        self.blob += payload[self.base - 0x8C004000 + count:self.base - 0x8C004000 + count + 28]
        self.post = None

    def write(self, address, value):
        key = canonical(address)
        assert not address & 3 and (canonical(self.start_sp) - 64 <= key < canonical(self.start_sp) or
                                   self.control <= key < self.control + 64 or
                                   self.stack[0] <= key < self.stack[0] + self.stack[1]), \
            "low heap hook store leaves caller/control/private stack"
        self.mem[key] = value & MASK

    def call(self, target):
        if target == self.stop_pc:
            return False
        if target == self.symbols["_kui_vmu_observe_return_hook"][0]:
            assert self.expect_refusal and not self.calls, "unexpected owner-stack refusal"
            assert self.mem[self.control+8]==5 and self.mem[self.control+16]==11
            self.pc=self.stop_pc
            return True
        if target == self.initializer:
            assert not self.calls
            assert self.r[4:6] == self.before[0][4:6] and self.r[15] == self.start_sp
            assert self.sr == self.before[5], "original initializer inherited masked SR or wrong stack"
            self.calls.append("initializer")
            self.abi_return()
            self.mach ^= 0x12345678; self.macl ^= 0x87654321
            self.post = self.r.copy(), self.mach, self.macl, self.gbr, self.sr, self.fpscr, self.fr.copy()
            return True
        if target == self.installer:
            assert self.calls == ["initializer"] and self.post is not None
            self.calls.append("installer")
            assert self.r[15] == self.stack[2], "installer entered on departed relay or owner stack"
            assert self.sr & ~1 == (self.post[4] | 0x100000F0) & ~1
            assert not (0x8CFCFFE0 <= canonical(self.r[15]) < 0x8D000000)
            # The real allocator's first lease covers the old high relay stack.
            # Destroy any old-stack sample while the actual hook/return frame
            # executes. No valid post-initializer path may depend on it.
            self.mem[OLD_STACK - 4] = 0xDEADC0DE
            self.write(self.r[15] - 4, 0x314B5354)
            self.abi_return()
            self.mach ^= 0xABCDEF01; self.macl ^= 0x10203040
            return True
        return False

    def check(self):
        if self.expect_refusal:
            assert not self.calls and self.post is None
            assert self.mem[self.control+8]==5 and self.mem[self.control+16]==11
            assert self.mem[self.control+12]==0
            return
        registers, mach, macl, gbr, sr, fpscr, fr = self.post
        assert self.calls == ["initializer", "installer"]
        assert self.r == registers and self.r[15] == self.start_sp
        assert self.pr == self.before[1] and (self.mach, self.macl, self.gbr, self.sr, self.fpscr, self.fr) == (mach, macl, gbr, sr, fpscr, fr)
        lines = {canonical(address) for address in self.published}
        assert all((address & ~31) in lines for address in range(canonical(self.start_sp) - 52, canonical(self.start_sp), 4)), \
            "owner frame was not published before private-stack entry"
        assert self.mem[OLD_STACK - 4] == 0xDEADC0DE


def indexed_load_truth_cases():
    """Exercise every encoded register pair, source aliases and wraparound."""
    count=0
    for n in range(16):
        for m in range(16):
            for r0,rm in ((0,0x8c004000),(4,0xac004000),(0xfffffffc,0x0c004004),
                          (0x80000000,0x80000000)):
                cpu=ArmRoute.__new__(ArmRoute)
                cpu.r=[0x12345678]*16;cpu.r[0]=r0;cpu.r[m]=rm
                before=cpu.r.copy();cpu.sr=0x600000f1
                address=(before[0]+before[m])&MASK
                cpu.mem={canonical(address):0xa5937c21};cpu.callback_table=None
                cpu.plain((n<<8)|(m<<4)|0x000e,0)
                expected=before.copy();expected[n]=0xa5937c21
                assert cpu.r==expected and cpu.sr==0x600000f1
                assert cpu.mem=={canonical(address):0xa5937c21}
                count+=1
    return count


def check(stage, resident, *, worker=None, self_test=False):
    stack = typed_stack(stage)
    if stack is None:
        return {"enabled": False}
    if worker is not None:
        require(STACK_SYMBOL not in worker[4] and HELPER not in worker[4] and BOTTOM_HELPER not in worker[4],
                "temporary installer stack/helper retained in gameplay worker")
    wrappers = routes = hooks = refusals = 0
    background='_kui_vmu_native_background_manifest_bind' in resident[4]
    require(background==('_native_background_boot_region' in stage[4]),'Installer stage/reader profile mismatch')
    ip_stack='__kui_native_owner_stack_ip_hook' in resident[4]
    require(ip_stack==('__kui_native_owner_stack_ip_stage' in stage[4]),
            'Native owner-stack stage/hook profiles differ')
    if ip_stack:
        require(resident[4]['__kui_native_owner_stack_ip_hook']==(1,0,0,0xfff1) and
                stage[4]['__kui_native_owner_stack_ip_stage']==(1,0,0,0xfff1),
                'Invalid native IP owner-stack capability markers')
    owner_low=0x0c008100 if ip_stack else 0x0c00c100
    require(owner_low-52>0x0c007d00+32,'Inherited owner save frame overlaps retained low stack')
    # The separate actual mode-helper proof covers Background's refused1/3
    # before this relay can be entered. Exercise all reachable live modes.
    modes = ((0,) if '__kui_native_vmu_only_stage' in stage[4] else
             ((0,2,4,5,6) if background else (0,1,2,3,4,5,6))) if "_native_runtime_mode" in stage[4] else (0,)
    for seed in range(12):
        for mask in range(16):
            for outcome in (False, True):
                AllocationWrapper(stage, seed, mask, outcome).run().check(); wrappers += 1
            for mode in modes:
                for hit in ((False, True) if "_native_resolution_replay" in stage[4] else (False,)):
                    for normalized in (False,True):
                        ArmRoute(stage, resident, stack, seed, mask, cache_hit=hit, mode=mode,normalized=normalized).run().check(); routes += 1
                if mode!=1 and "_native_sdk_coherence_cached_admit" in stage[4]:
                    ArmRoute(stage,resident,stack,seed,mask,cache_hit=False,mode=mode,sdk_admitted=False).run().check()
                    refusals+=1
            for alias in (0, 0x80000000, 0xA0000000):
                HeapHook(resident, stage, stack, seed, mask, alias=alias).run().check(); hooks += 1
    crt_refusals=0
    if "_native_crt_bootstrap_prearm" in stage[4]:
        for mask in (0,15):
            for mode in modes:
                if mode==1: continue
                for hit in (False,True):
                    ArmRoute(stage,resident,stack,7,mask,cache_hit=hit,mode=mode,
                             crt_admitted=False).run().check()
                    crt_refusals+=1
    boundary_cases=0
    for seed in range(4):
        for mask in range(16):
            for alias in (0,0x80000000,0xa0000000):
                for sp in (owner_low,owner_low+4,0x0c00ed00,0x0c00f400):
                    HeapHook(resident,stage,stack,seed,mask,alias=alias,owner_sp=sp).run().check()
                    boundary_cases+=1
                for sp in (owner_low-4,0x0c007d00,0x0c00f404):
                    HeapHook(resident,stage,stack,seed,mask,alias=alias,owner_sp=sp,
                             expect_refusal=True).run().check()
                    boundary_cases+=1
                if ip_stack:
                    HeapHook(resident,stage,stack,seed,mask,alias=alias,owner_sp=0x0c00b3a4).run().check()
                    boundary_cases+=1
    rayman_old_bound_refusals=0
    _,owner_hook_body,_=symbol_bytes(resident,'_kui_vmu_observe_heap_hook',2)
    if ip_stack:
        _,body,_=symbol_bytes(resident,'_kui_vmu_observe_heap_hook',2)
        require(body.count(struct.pack('<I',owner_low))==1,'Missing unique linked owner-stack lower bound')
        old_body=body.replace(struct.pack('<I',owner_low),struct.pack('<I',0x0c00c100))
        for alias in (0,0x80000000,0xa0000000):
            HeapHook(resident,stage,stack,7,15,alias=alias,owner_sp=0x0c00b3a4,body=old_body,
                     expect_refusal=True).run().check()
            rayman_old_bound_refusals+=1
    mutations = 0
    selected_callbacks=stage[4].get('__kui_native_dual_low_callbacks_stage')==(1,0,0,0xfff1)
    scalar_cases=indexed_load_truth_cases() if selected_callbacks else 0
    if self_test:
        mutation_symbol=('_native_arm_heap_hook' if selected_callbacks else '_kui_retail_stage_relay')
        relay_base, relay_body, _ = symbol_bytes(stage, mutation_symbol, 2)
        expected = struct.pack("<I", stack[2]); at = relay_body.find(expected)
        require(at >= 0 and relay_body.count(expected)==1,
                "actual relay/arming helper lacks unique typed installer stack-top literal")
        changed = bytearray(relay_body); struct.pack_into("<I", changed, at, OLD_STACK - 32)
        arguments=({'helper_bodies':{mutation_symbol:bytes(changed)}} if selected_callbacks
                   else {'body':bytes(changed)})
        mutated_route=ArmRoute(stage,resident,stack,7,15,**arguments)
        try:
            mutated_route.run().check()
        except (AssertionError, ValueError, struct.error, KeyError):
            if selected_callbacks:
                require(mutated_route.callback_helper_calls==['_native_arm_heap_hook','_native_low_bindings'] and
                        mutated_route.mem.get(mutated_route.control+4)==OLD_STACK-32,
                        'Stack-top mutant did not reach publication through both actual helpers')
            mutations += 1
        else:
            raise ValueError("relay accepted old high-stack publication")
        _, body, _ = symbol_bytes(resident, "_kui_vmu_observe_heap_hook", 2)
        for opcode in (0x5F21, 0x6F83, 0x4F07):
            at = body.find(struct.pack("<H", opcode))
            require(at >= 0, "low heap hook lacks exact dynamic stack/frame instruction")
            changed = bytearray(body); struct.pack_into("<H", changed, at, 9)
            try:
                HeapHook(resident, stage, stack, 7, 15, body=bytes(changed)).run().check()
            except (AssertionError, ValueError, struct.error, KeyError):
                mutations += 1
            else:
                raise ValueError("low heap hook accepted missing stack/frame instruction")
        _, body, _ = symbol_bytes(stage, "_allocation_safe", 2)
        for target in (stage[4][allocation_helper(stage)][0], stack[4]):
            at = body.find(struct.pack("<I", target)); require(at >= 0, "allocation wrapper lacks exact helper/end literal")
            changed = bytearray(body); struct.pack_into("<I", changed, at, target + 4)
            try:
                AllocationWrapper(stage, 7, 15, True, body=bytes(changed)).run().check()
            except (AssertionError, ValueError, struct.error, KeyError):
                mutations += 1
            else:
                raise ValueError("allocation wrapper accepted stale stage boundary/helper")
    callback_metadata={}
    if selected_callbacks:
        for name,kind in (('_native_arm_heap_hook',2),('_native_low_bindings',2),
                          ('_native_low_bindings_table',1)):
            address,body,_=symbol_bytes(stage,name,kind)
            callback_metadata[name]=dict(address=hex(address),bytes=len(body),sha256=hashlib.sha256(body).hexdigest())
    return {"enabled": True, "object_address": hex(stack[0]), "object_bytes": stack[1],
            "installer_sp": hex(stack[2]), "stage_bss_end": hex(stack[4]),
            "ordinary_entry_and_relay_stack": hex(OLD_STACK), "package_stack_unchanged": True,
            "typed_zero_fill_object": True, "complete_bss_in_live_allocation_guard": True,
            "actual_allocation_wrapper_cases": wrappers, "actual_relay_publication_cases": routes,
            "actual_low_hook_cases": hooks, "mutation_rejections": mutations,
            "selected_low_callback_helpers_executed": selected_callbacks,
            "selected_callback_reader": (1 if '__kui_native_sdk_sci_route_resident' in resident[4] else 2)
                if selected_callbacks else None,
            "selected_callback_table_bytes": 48 if selected_callbacks else 0,
            "selected_callback_code_and_immutable_table": callback_metadata,
            "selected_indexed_load_scalar_truth_cases": scalar_cases,
            "selected_row_matches_actual_resident_roles": selected_callbacks,
            "selected_stack_top_mutant_executes_actual_relay_and_helpers": selected_callbacks and self_test,
            "owner_stack_physical_bounds":{"low":f"0x{owner_low:08x}","high":"0x0c00f400"},
            "owner_stack_boundary_execution_cases":boundary_cases,
            "owner_stack_hook_sha256":hashlib.sha256(owner_hook_body).hexdigest(),
            "owner_stack_hook_address":f"0x{resident[4]['_kui_vmu_observe_heap_hook'][0]:08x}",
            "owner_stack_hook_bytes":len(owner_hook_body),
            "rayman_original_startup_sp_rejected_by_old_bound_cases":rayman_old_bound_refusals,
            "inherited_52byte_frame_above_retained_low_stack":True,
            "cold_sdk_slot_refusal_before_any_arming_cases":refusals,
            "cold_crt_prearm_after_low_arming_before_owner_entry":bool(crt_refusals),
            "cold_crt_prearm_result_refusal_cases":crt_refusals,
            "full_source_verified_before_program_extent":True,
            "normal_and_shortened_extent_arming_routes":True,
            "runtime_modes": list(modes), "control_preserves_original_frame_and_allocator": 1 in modes,
            "initializer_preserves_original_stack_and_sr": True,
            "private_stack_before_allocation_or_worker_writes": True,
            "departed_relay_stack_clobber_preserves_caller": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path); parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        result = check(read_elf(args.directory / "stage.elf"), read_elf(args.directory / "resident-sci.elf"),
                       worker=read_elf(args.directory / "native-worker.elf"), self_test=args.self_test)
    except (OSError, ValueError, AssertionError, KeyError, struct.error) as error:
        parser.exit(2, "Native installer stack audit refused: " + str(error) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
