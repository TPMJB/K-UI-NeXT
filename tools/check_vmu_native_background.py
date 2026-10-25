#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the separately linked native Background VMU resident.

The ordinary reader's generic stack allowance is unchanged. This profile
proves its actual linked libgcc helper instead: four stack bytes, plus an
explicit 32-byte allowance, the existing 16-byte guard and 32-byte top gap.
Cold initialization/binding use the installer stack; the storage gateway and
the validated non-returning terminal entry use the worker's own stack.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

from check_loader_layout import inspect_elf
from check_retail_loader_layout import exact_profile_marker
from check_retail_instructions import audit as instruction_audit
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from vmu_maple_observe_symbols import sd_gateway_span
import check_retail_stack as graph
from sh4_delay_slots import validate_delay_slot

ROOT = Path(__file__).resolve().parents[1]
LOW, BOTTOM, TOP = 0x8C004000, 0x8C007BA0, 0x8C007D00
GUARD, TOP_GAP, EXTRA_MARGIN = 16, 32, 32
ROOTS = {"kui_retail_resident_dispatch": 0, "kui_retail_async_irq": 44,
         "kui_retail_menu_return": 0}
COLD = frozenset({"kui_vmu_native_background_init", "kui_vmu_observe_background_base_init",
                  "kui_vmu_native_background_manifest_bind", "kui_vmu_observe_sd_lease",
                  "kui_retail_native_async_loan_begin", "kui_retail_native_async_loan_end"})
CALLBACKS = {
    ("kui_retail_async_trace", "retail_async.c"): ("kui_native_loader_timeline_visit",),
    ("guest", "retail_gd.c"): ("native_background_map",),
    ("kui_retail_gd_dispatch", "retail_gd.c"): ("check_sectors",),
    ("byte", "sci_stream.c"): ("transfer",),
    ("select", "sci_stream.c"): ("select_card",),
    ("deliver", "retail_cursor.c"): ("write_out",),
    ("kui_retail_cursor_feed", "retail_cursor.c"): ("write_out",),
    ("write_out", "retail_async.c"): ("native_background_map",),
}
ESCAPES = {("kui_retail_menu_return", "retail_resident.c"): "boot-ROM menu; does not return",
           ("kui_vmu_observe_low_failure", "vmu_native_background_resident.c"):
               "validated worker terminal; private stack switch separately audited"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def typed(image, name, kind, *, size=None, writable=False):
    record = image[4].get(name)
    require(record is not None, "missing linked symbol: " + name)
    address, count, actual, owner = record
    require(actual == kind and count > 0 and owner < len(image[3]),
            "missing typed linked symbol: " + name)
    section = image[3][owner]
    require(section[2] & 2 and section[3] <= address < address + count <= section[3] + section[5],
            "typed symbol escapes its allocated section: " + name)
    require(bool(section[2] & 4) == (kind == 2), "wrong symbol execution permission: " + name)
    if size is not None:
        require(count == size, "wrong typed symbol size: " + name)
    if writable:
        require(section[1] == 8 and section[2] & 3 == 3, "pointer workspace must be zero-fill writable BSS")
    return address, count


def disassemble(path):
    tool = shutil.which("sh-elf-objdump") or str(ROOT / ".deps/sh-elf/bin/sh-elf-objdump")
    return subprocess.check_output([tool, "-d", str(path)], text=True)


def rows(disassembly):
    result = {}
    pattern = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{2})\s+([0-9a-f]{2})\s+(\S+)(?:\s+(.*))?$")
    for line in disassembly.splitlines():
        found = pattern.match(line)
        if not found:
            continue
        address, lo, hi, mnemonic, operands = found.groups()
        result[int(address, 16)] = (int(lo, 16) | int(hi, 16) << 8, mnemonic,
                                     (operands or "").split("!", 1)[0].strip())
    return result


def library_cfg(decoded, address, count):
    """Exhaust every branch of the actual integer libgcc division helper.

    Internal helpers precede its public symbol. No register/memory value is
    abstracted into a control transfer: only direct branches and RTS using
    the exact symbolic PR are admitted. Every path must return with its
    saved PR and balanced stack. A cycle, unknown operation or SP alias
    rejects the proof rather than acquiring a margin waiver.
    """
    lower, upper, sentinel = address - 64, address + count, -1
    allowed = {"extu.w", "cmp/eq", "div0u", "swap.w", "shlr16", "shll16", "div1",
               "xtrct", "rotcl", "mov", "nop"}
    visited, active = {}, set()
    maximum, exits = 0, 0

    def plain(pc, pr, stack):
        nonlocal maximum
        require(pc in decoded and lower <= pc < upper, "division helper leaves its bounded code")
        op, mnemonic, operands = decoded[pc]
        if op == 0x4F22:
            require(mnemonic == "sts.l" and operands.replace(" ", "") == "pr,@-r15",
                    "division helper PR push differs")
            stack += (pr,)
            maximum = max(maximum, len(stack) * 4)
        elif op == 0x4F26:
            require(mnemonic == "lds.l" and operands.replace(" ", "") == "@r15+,pr" and stack,
                    "division helper has an unmatched PR pop")
            pr, stack = stack[-1], stack[:-1]
        else:
            require(mnemonic in allowed and not re.search(r"\b(?:r15|pr|sr|vbr|gbr|ssr|spc|dbr)\b|@", operands),
                    "unmodeled division helper instruction: " + mnemonic + " " + operands)
        return pr, stack

    def visit(pc, pr, stack):
        nonlocal exits
        state = pc, pr, stack
        require(state not in active, "cyclic division helper control flow")
        if state in visited:
            return
        active.add(state)
        require(len(active) <= 512, "division helper path exceeds its exhaustive proof bound")
        require(pc in decoded and lower <= pc < upper, "division helper branches outside its bounded code")
        op, mnemonic, _ = decoded[pc]
        if op == 0x000B or op >> 12 in (0xA, 0xB):
            if op == 0x000B:
                target = pr
            else:
                displacement = op & 4095
                displacement -= 4096 if displacement & 2048 else 0
                target = pc + 4 + 2 * displacement
                if op >> 12 == 0xB:
                    pr = pc + 4
            require(pc + 2 in decoded, f"division helper lacks its delay slot at {pc:08x}")
            validate_delay_slot(decoded[pc + 2][0], pc)
            pr, stack = plain(pc + 2, pr, stack)
            if target == sentinel:
                require(not stack and pr == sentinel, "division helper returns with an unbalanced stack/PR")
                exits += 1
            else:
                visit(target, pr, stack)
        elif op >> 8 in (0x89, 0x8B, 0x8D, 0x8F):
            require(mnemonic in ("bt", "bf", "bt.s", "bf.s"), "division branch decode differs")
            displacement = op & 255
            displacement -= 256 if displacement & 128 else 0
            delayed = op >> 8 in (0x8D, 0x8F)
            if delayed:
                require(pc + 2 in decoded, "division conditional lacks its delay slot")
                validate_delay_slot(decoded[pc + 2][0], pc)
                pr, stack = plain(pc + 2, pr, stack)
            visit(pc + 4 + 2 * displacement, pr, stack)
            visit(pc + (4 if delayed else 2), pr, stack)
        else:
            pr, stack = plain(pc, pr, stack)
            visit(pc + 2, pr, stack)
        active.remove(state)
        visited[state] = True
        require(len(visited) <= 512, "division helper path proof exceeds its bound")

    visit(address, sentinel, ())
    require(exits and maximum == 4, "division helper must have a proved four-byte maximum")
    return {"symbol": "___udivsi3", "stack_bytes": maximum, "abstract_states": len(visited),
            "balanced_return_paths": exits, "internal_helper_window_bytes": 64,
            "unknown_instruction_or_cycle_rejected": True}


def stack_paths(frames, calls, roots=None, switched_stacks=None):
    callbacks, escapes, reached = set(), set(), set()
    memo = {}
    switched_stacks = {} if switched_stacks is None else switched_stacks

    def resolve(function):
        if function in frames:
            return [function]
        alternatives = [name for name in frames if graph.base(name) == graph.base(function)]
        require(alternatives, "call has no emitted stack frame: " + function)
        return alternatives

    def depth(function, path=()):
        require(graph.base(function) not in COLD, "installer/worker path reached the private GD stack: " + function)
        require(function not in path, "recursive private GD call graph: " + " -> ".join(path + (function,)))
        if function in memo:
            return memo[function]
        if function not in frames:
            if graph.base(function) in graph.LIBRARY:
                return 0, [function + "(separately bounded library)"]
            return max((depth(target, path) for target in resolve(function)), key=lambda answer: answer[0])
        reached.add(function)
        best = 0, []
        for target, label in calls.get(function, []):
            if target == "__indirect_call":
                key = graph.base(function), label.split(":", 1)[0]
                if key in ESCAPES:
                    escapes.add(key)
                    continue
                if key in switched_stacks:
                    # The paired read-ahead gateway first parks its exact
                    # owner frame here, then uses the independently proved
                    # auxiliary stack. This is not a zero-byte callback.
                    count=switched_stacks[key]
                    if count>best[0]:best=(count,["guarded auxiliary stack switch("+str(count)+")"])
                    callbacks.add((function,label,("separately audited auxiliary stack",)))
                    continue
                require(key in CALLBACKS, "unadmitted private GD callback: " + function + " at " + label)
                targets = [node for callback in CALLBACKS[key] for node in resolve(callback)]
                callbacks.add((function, label, tuple(targets)))
            else:
                targets = [target]
            for callee in targets:
                answer = depth(callee, path + (function,))
                if answer[0] > best[0]:
                    best = answer
        memo[function] = frames[function] + best[0], [function + "(" + str(frames[function]) + ")"] + best[1]
        return memo[function]

    result = {}
    for root, assembly in (ROOTS if roots is None else roots).items():
        candidates = [name for name in frames if graph.base(name) == root]
        require(len(candidates) == 1, "missing or duplicated private GD root: " + root)
        total, path = depth(candidates[0])
        result[root] = {"c_bytes": total, "assembly_entry_bytes": assembly,
                        "bytes": total + assembly, "path": path}
    require(any(graph.base(name) == "native_background_map" for name in reached),
            "guarded Background mapping callback was not walked")
    require(set(ESCAPES) <= escapes, "missing independently classified non-returning callback")
    return result, sorted(callbacks), sorted(escapes)


def c_body(text, signature):
    start = text.index(signature)
    begin = text.index("{", start)
    depth, end = 1, begin + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def compiled_tail_copy(stage, resident, *, self_test=False, lease_bytes=0x30000,witness=False):
    """Replay the linked installer tail through its first worker relocation.

    The already audited prefix proves the native allocation and normalizes
    its load address. This closed replay begins at its final lease-size
    check, preserves every actual branch, OCBP loop and argument instruction,
    executes the real P2 cache publisher, and supplies hostile ABI callees
    only for memcpy, address admission, the separately tested low binder,
    and display helpers. Invalid binder results must reach the real fatal
    report call without reaching worker relocation.
    """
    sys.path.insert(0, str(ROOT / "tests"))
    from test_vmu_native_heap_execution import SH, MASK, signed
    begin, body, _ = symbol_bytes(stage, "_native_passive_run", 2)
    symbols = stage[4]
    words = list(struct.unpack("<" + "H" * (len(body) // 2), body))
    def literal(index):
        op = words[index]
        if op >> 12 != 0xD:
            return None
        address = ((begin + index * 2 + 4) & ~3) + 4 * (op & 255)
        if not begin <= address <= begin + len(body) - 4:
            return None
        return struct.unpack_from("<I", body, address - begin)[0]
    if witness:
        # This is a separate, complete immutable function profile. Cold
        # display calls changed its register allocation; do not make the old
        # compiler-shape search accept arbitrary new lease-check forms.
        require(stage[4].get('__kui_native_cold_progress_stage')==
                stage[4].get('__kui_native_dual_readers_stage')==(1,0,0,0xfff1),
                'Background witness tail requires exact dual/progress markers')
        require(len(body)==1996 and digest(body)==
                '51da99254ae17780acfbf3c56915f58e3107b64e559767570281545d3e2eaaeb',
                'Background witness passive-run instruction/literal pin changed')
        index=0x134//2
        require(words[index:index+4]==[0x51a8,0x31b0,0x8f08,0x1c0f] and
                literal(index+4)==0x20000,
                'Background witness exact lease-size entry differs')
        start=begin+0x134
    else:
        require('__kui_native_cold_progress_stage' not in stage[4],
                'Background progress stage lacks its complete witness worker proof')
        starts = [begin + 2 * index for index in range(len(words) - 5)
                  if words[index:index + 2] == [0x51C8, 0x31A0] and
                  words[index + 2] >> 8 == 0x8F and words[index + 3] == 0x1B0F and
                  literal(index + 4) == 0x20000]
        require(len(starts) == 1, "missing unique compiled Background lease-size/tail-copy entry")
        start = starts[0]
    manifest = symbols["_manifest"][0]
    workspace = symbols["_native_background_boot_region"][0]
    binder = resident["symbols"]["_kui_vmu_native_background_manifest_bind"]
    publisher = symbols["_kui_native_passive_publish"][0]
    copy = symbols["_memcpy"][0]
    relocate = symbols["_kui_vmu_native_relocate"][0]
    admission = symbols["_kui_vmu_native_background_map_address"][0]
    report = symbols["_native_report"][0]
    worker_bytes = symbols["__native_worker_blob_end"][0] - symbols["__native_worker_blob_begin"][0]

    class Replay(SH):
        def __init__(self, raw, outcome, changes=(),*,lease_value=None,admissions=(1,1)):
            super().__init__(b"")
            for kind, offset, address, _, count, _, _, _ in stage[2]:
                if kind == 1 and count:
                    self.store_block(address, stage[0][offset:offset + count])
            self.raw, self.load, self.outcome = raw, (raw & 0x1FFFFFFF) | 0x80000000, outcome
            self.pc, self.r[15] = start, 0x8CDD0000
            self.lease = 0x8CDF0000
            if witness:
                self.r[8],self.r[9],self.r[10],self.r[11],self.r[12]=\
                    copy,self.load,self.lease,lease_bytes,self.r[15]+0x900
            else:
                self.r[8], self.r[9], self.r[10], self.r[11], self.r[12] = copy, self.load, lease_bytes, self.r[15] + 0x904, self.lease
            self.lease_value=lease_bytes if lease_value is None else lease_value
            self.admissions,self.admission_calls=admissions,0
            self.early_failure=('lease' if self.lease_value!=lease_bytes else
                                'map' if not admissions[0] else 'region' if not admissions[1] else None)
            self.write(self.lease + 32, self.lease_value)
            self.write(self.lease + 40, raw)
            self.write(self.r[15] + 32, self.lease - 64)
            self.write(self.r[15] + 28, worker_bytes)
            self.map_pattern = bytes((i * 17 + 3) & 255 for i in range(1020))
            self.region_pattern = bytes((i * 23 + 7) & 255 for i in range(1888))
            self.store_block(manifest, self.map_pattern)
            self.store_block(workspace, self.region_pattern)
            for address, value, size in changes:
                self.write(address, value, size)
            self.events, self.purges, self.witness, self.length_literals = [], [], {}, {}
            self.volatile_calls, self.ccr, self.ccr_writes = 0, 0x105, []
            self.in_publisher = False
            self.publisher_return = None
            self.stopped = None
            self.display_events=[]

        def read(self, address, count=4):
            if address == 0xFF00001C:
                require(count == 4 and self.in_publisher, "cache state read outside the exact publisher")
                return self.ccr
            return super().read(address, count)

        def write(self, address, value, count=4):
            if address == 0xFF00001C:
                require(count == 4 and self.in_publisher, "cache state write outside the exact publisher")
                self.ccr_writes.append(value)
                self.ccr = value
                return
            super().write(address, value, count)

        def plain(self, op, pc):
            n, m = op >> 8 & 15, op >> 4 & 15
            if op & 0xF0FF == 0x00A3:
                self.purges.append((pc, self.r[n], self.in_publisher))
                return
            if op >> 8 == 0x88:
                self.flag(self.r[0] == signed(op & 255, 8) & MASK)
                return
            if op & 0xF0FF == 0x4010:
                self.r[n] = (self.r[n] - 1) & MASK
                self.flag(self.r[n] == 0)
                return
            if op & 0xF00F == 0x0006:
                self.write(self.r[0] + self.r[n], self.r[m])
                return
            if op >> 12 == 9 and n == 6:
                self.length_literals["current"] = pc + 4 + 2 * (op & 255)
            super().plain(op, pc)

        def abi_return(self, result=0):
            self.volatile_calls += 1
            for index in range(8):
                self.r[index] = (0x13570000 + index * 0x10203 + self.volatile_calls) & MASK
            self.r[0] = result & MASK
            self.pc = self.pr

        def purged(self, first, count, publication=False):
            actual = [address for _, address, inside in self.purges if inside == publication and first <= address < first + count]
            expected = list(range(first, first + count, 32))
            require(actual == expected, "Background copy/publication lacks its exact ordered cache-line purge")

        def call(self, target, pc):
            if target == admission:
                require(self.r[4:6] == [self.raw, self.load + 0x20000], "tail admission changed its exact native allocation/map")
                require(self.admission_calls<2,'Background tail acquired an extra address admission')
                result=self.admissions[self.admission_calls]
                self.admission_calls+=1
                self.abi_return(result)
            elif target == copy:
                index = len([event for event in self.events if event.startswith("copy_")])
                name, offset, source, count, pattern = (("map", 0x20000, manifest, 1020, self.map_pattern) if index == 0 else
                    ("region", 0x20400, workspace, 1888, self.region_pattern))
                require(index < 2 and self.r[4:7] == [self.load + offset + 0x20000000, source, count],
                        "compiled Background memcpy length/source/P2 destination changed")
                self.purged(self.load + offset, count)
                require(not self.in_publisher and (index == 0 or self.events == ["copy_map"]), "Background tail copies are out of order")
                self.store_block(self.r[4], self.block(source, count))
                require(self.block(self.load + offset, count) == pattern, "Background retained snapshot differs")
                self.events.append("copy_" + name)
                self.witness["copy_" + name] = pc
                self.length_literals[name] = self.length_literals["current"]
                self.abi_return()
            elif target == publisher:
                require(self.events == ["copy_map", "copy_region"] and self.r[4:6] == [self.load + 0x20400, self.load + 0x20B60],
                        "region publication must follow both exact tail copies")
                self.witness["publish"] = pc
                self.in_publisher = True
                self.publisher_return = self.pr
                self.pc = target
            elif target == binder:
                require(self.events == ["copy_map", "copy_region", "publish"] and self.r[4:6] == [manifest, self.load + 0x20000],
                        "typed low binder must follow complete vector publication")
                require(self.block(self.load + 0x20000, 1020) == self.map_pattern and
                        self.block(self.load + 0x20400, 1888) == self.region_pattern, "binder sees changed snapshots")
                self.events.append("bind")
                self.witness["bind"] = pc
                self.witness["bind_success"] = pc + 4
                self.abi_return(self.outcome)
            elif target in (symbols["_retail_display_restore"][0], symbols["_retail_display_line"][0]):
                if witness:
                    expected=(('line',b'COLD 30 READER MAP BIND BEGIN',[]),
                              ('line',b'COLD 31 READER MAP BOUND',["copy_map","copy_region","publish","bind"]),
                              ('restore',None,["copy_map","copy_region","publish","bind"]),
                              ('line',b'RELOCATING PASSIVE MAPLE WORKER',["copy_map","copy_region","publish","bind"]),
                              ('line',b'COLD 32 WORKER RELOCATION BEGIN',["copy_map","copy_region","publish","bind"]))
                    require(len(self.display_events)<len(expected),'extra Background cold display boundary')
                    kind,line,events=expected[len(self.display_events)]
                    require(self.events==events and (not events or self.outcome==1),
                            'Background cold progress passed a failed or incomplete binding')
                    if kind=='restore':
                        require(target==symbols['_retail_display_restore'][0] and
                                self.r[4]==symbols['_display'][0],
                                'Background cold display restore argument differs')
                    else:
                        require(target==symbols['_retail_display_line'][0] and
                                self.block(self.r[4],len(line)+1)==line+b'\0',
                                'Background cold progress line/order differs')
                    self.display_events.append(kind)
                else:
                    require(self.outcome == 1 and self.events[-1] == "bind", "installer continued after failed binding")
                self.abi_return()
            elif target == relocate:
                require(self.outcome == 1 and self.events == ["copy_map", "copy_region", "publish", "bind"],
                        "worker relocation reached before successful typed binding")
                require(self.r[4] == symbols["__native_worker_blob_begin"][0] and self.r[5] == worker_bytes and
                        self.r[6] == symbols["__native_worker_reloc_begin"][0] and
                        self.r[7] == symbols["__native_worker_reloc_end"][0] - symbols["__native_worker_reloc_begin"][0],
                        "worker relocation inputs changed")
                require([self.read(self.r[15] + 4 * index) for index in range(3)] ==
                        [self.load + 0x20000000, 0x20000, self.load], "worker relocation overwrites the retained map/region tail")
                self.purged(self.load, 0x20000)
                require(not witness or len(self.display_events)==5,
                        'Background relocation omitted ordered cold progress')
                self.witness["relocate"] = pc
                self.stopped = "relocate"
            elif target == report:
                if self.early_failure:
                    expected_events=['copy_map'] if self.early_failure=='region' else []
                    expected_admissions={'lease':0,'map':1,'region':2}[self.early_failure]
                    require(self.events==expected_events and self.admission_calls==expected_admissions and
                            self.r[4:6]==[8,0x4a if self.early_failure=='region' else 0x48],
                            'Background guard refusal executed the wrong effects/report')
                    require(not self.purges if not expected_events else len(self.purges)==32,
                            'Background guard refusal purged a later retained region')
                else:
                    require(self.outcome != 1 and self.events == ["copy_map", "copy_region", "publish", "bind"],
                            "valid Background binding failed before relocation")
                self.stopped = "report"
            else:
                raise ValueError(f"unadmitted call in Background tail replay: {target:08x}")

        def run(self):
            for _ in range(100000):
                if self.stopped:
                    require(self.stopped == ("relocate" if self.outcome == 1 and not self.early_failure else "report"), "binder success predicate changed")
                    return self
                if self.in_publisher and self.pc == self.publisher_return:
                    self.purged(self.load + 0x20400, 1888, publication=True)
                    require(self.ccr_writes == [0x905], "vector publisher changed owner CCR or omitted I-cache invalidation")
                    self.in_publisher = False
                    self.events.append("publish")
                pc, op = self.pc, self.read(self.pc, 2)
                target, delayed, call = None, False, False
                if op >> 12 in (0xA, 0xB):
                    target, delayed = pc + 4 + 2 * signed(op & 4095, 12), True
                    call = op >> 12 == 0xB
                elif op & 0xF0FF in (0x400B, 0x402B):
                    target, delayed = self.r[op >> 8 & 15], True
                    call = op & 0xF0FF == 0x400B
                elif op == 0x000B:
                    target, delayed = self.pr, True
                elif op >> 8 in (0x89, 0x8B, 0x8D, 0x8F):
                    taken = bool(self.sr & 1) == (op >> 8 in (0x89, 0x8D))
                    delayed = op >> 8 in (0x8D, 0x8F)
                    if delayed:
                        slot = self.read(pc + 2, 2); validate_delay_slot(slot, pc); self.plain(slot, pc + 2)
                    self.pc = pc + 4 + 2 * signed(op & 255, 8) if taken else pc + (4 if delayed else 2)
                    continue
                if delayed:
                    if call:
                        self.pr = pc + 4
                    slot = self.read(pc + 2, 2); validate_delay_slot(slot, pc); self.plain(slot, pc + 2)
                    if call:
                        self.call(target, pc)
                    else:
                        self.pc = target & MASK
                else:
                    self.plain(op, pc)
                    self.pc += 2
            raise ValueError("unbounded Background tail-copy replay")

    cases, example = 0, None
    for raw in (0x0C100000, 0x8C300000, 0xAC900000):
        for outcome in (1, 0, 2, MASK):
            completed = Replay(raw, outcome).run()
            if outcome == 1:
                example = completed
            cases += 1
        if witness:
            for invalid in (0,0x30000,0x60004,MASK):
                Replay(raw,1,lease_value=invalid).run();cases+=1
            for admissions in ((0,1),(1,0)):
                Replay(raw,1,admissions=admissions).run();cases+=1
    require(example is not None, "missing successful tail-copy replay")
    rejected = []
    pin_mutations=0
    if self_test:
        def pool(value):
            found = [begin + offset for offset in range(0, len(body) - 3, 2)
                     if body[offset:offset + 4] == struct.pack("<I", value)]
            require(len(found) == 1, "tail mutation requires a unique literal")
            return found[0]
        mutations = {
            "map_copy_wrong_length": [(example.length_literals["map"], 1016, 2)],
            "region_copy_wrong_length": [(example.length_literals["region"], 1884, 2)],
            "map_copy_wrong_alias": [(pool(0x20020000), 0x20000, 4)],
            "region_copy_wrong_offset": [(pool(0x20020400), 0x20020800, 4)],
            "region_copy_wrong_source": [(pool(workspace), manifest, 4)],
            "missing_map_purge": [(next(pc for pc, address, inside in example.purges if address == example.load + 0x20000 and not inside), 9, 2)],
            "missing_region_publication": [(example.witness["publish"], 9, 2)],
            "binding_before_publication": [(((example.witness["publish"] - 4 + 4) & ~3) +
                4 * (words[(example.witness["publish"] - 4 - begin) // 2] & 255), binder, 4)],
            "binder_accepts_zero": [(example.witness["bind_success"], 0x8800, 2)],
        }
        for name, changes in mutations.items():
            try:
                Replay(0x8C300000, 1, changes).run()
                Replay(0x8C300000, 0, changes).run()
            except (ValueError, AssertionError):
                rejected.append(name)
            else:
                raise ValueError("compiled tail-copy replay accepted mutation: " + name)
        if witness:
            for offset in range(len(body)):
                changed=bytearray(body);changed[offset]^=1
                require(digest(changed)!='51da99254ae17780acfbf3c56915f58e3107b64e559767570281545d3e2eaaeb',
                        'Background witness passive-run pin accepted changed instructions/literals')
                pin_mutations+=1
    return {"actual_sh_cases": cases, "entry": hex(start), "lease_bytes":lease_bytes,
            "controller_failure_witness_profile":witness,
            "complete_passive_run_pin":({'bytes':len(body),'sha256':digest(body)} if witness else None),
            "actual_ordered_cold_progress_calls":witness,
            "early_lease_and_both_address_guard_refusal_cases":18 if witness else 0,
            "complete_passive_run_pin_mutation_rejections":pin_mutations,
            "witnesses": {name: hex(address) for name, address in example.witness.items()},
            "map_copy_bytes": 1020, "region_copy_bytes": 1888, "map_lines_before_p2_copy": 32,
            "region_lines_before_p2_copy": 59, "actual_p2_publisher_executed": True,
            "binder_result_must_equal_one": True, "worker_relocation_preserves_tail": True,
            "hostile_abi_callees": True, "refused_mutations": rejected}


def source_contract(readahead=False,wide_lease=False):
    paths = ("include/kui/vmu_native_background.h", "include/kui/vmu_native_async_loan.h",
             "src/loader/vmu_native_background_resident.c", "src/loader/retail_resident.c",
             "src/loader/retail_async.c", "src/loader/retail_async.h", "src/loader/retail_resident.S",
             "src/loader/sci_sd_bus.c", "src/loader/vmu_native_passive_stage.inc",
             "src/loader/vmu_native_heap_probe_stage.c", "src/loader/retail_stage.c")
    sources = {path: (ROOT / path).read_text() for path in paths}
    constants = {}
    for name, value in re.findall(r"#define (KUI_VMU_NATIVE_BACKGROUND_\w+) UINT32_C\((0x[0-9a-fA-F]+|\d+)\)",
                                  sources[paths[0]]):
        constants[name] = int(value, 0)
    expected = {"KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES": 0x20000,
                "KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES": 0x30000,
                "KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES": 1020,
                "KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET": 0x20400,
                "KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES": 0x760}
    require(all(constants.get(key) == value for key, value in expected.items()),
            "Background workspace geometry changed")
    if readahead or wide_lease:
        # Preserve the old192KiB default. The dual module admits its legacy
        # engine to the exact same384KiB geometry only when the linked
        # terminal/map guard instructions independently prove that span.
        tail='\n#define KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES KUI_NATIVE_RA_LEASE_BYTES\n#else\n#define KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES UINT32_C(0x30000)\n#endif'
        old='#if KUI_NATIVE_READAHEAD'+tail
        dual='#if KUI_NATIVE_READAHEAD || KUI_NATIVE_DUAL_READERS'+tail
        require(old in sources[paths[0]] or dual in sources[paths[0]],
                'read-ahead did not isolate the selected owned lease')
        if dual in sources[paths[0]]:
            header=(ROOT/'include/kui/native_dual_reader.h').read_text()
            require('#ifndef KUI_NATIVE_DUAL_READERS\n#define KUI_NATIVE_DUAL_READERS 0\n#endif' in header and
                    '#if KUI_NATIVE_DUAL_READERS != 0 && KUI_NATIVE_DUAL_READERS != 1' in header,
                    'dual lease must remain an explicit boolean opt-in')
        layout=(ROOT/'include/kui/native_readahead_layout.h').read_text()
        require('#define KUI_NATIVE_RA_LEASE_BYTES UINT32_C(0x60000)' in layout,
                'read-ahead selected lease geometry changed')
        expected['KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES']=0x60000
    wrapper = sources["src/loader/vmu_native_background_resident.c"]
    guard = wrapper[:wrapper.index("#endif")]
    for flag in ("KUI_VMU_NATIVE_BACKGROUND", "KUI_VMU_NATIVE_PERSISTENT_PROBE", "KUI_RETAIL_ASYNC",
                 "KUI_RETAIL_LOW_RESIDENT", "KUI_GENERAL_SCI_RESUME_FIX", "KUI_RETAIL_NATIVE_DRIVE_STATE",
                 "KUI_RETAIL_NATIVE_DISC_CHECK"):
        require("!" + flag in guard, "Background marker lacks its mandatory source gate: " + flag)
    terminal = c_body(wrapper, "void kui_vmu_observe_low_failure(void) {")
    require(terminal.count("c->terminal") == 1 and "if(native_resident_terminal(c))" in terminal and
            'for(;;) __asm__ volatile("nop");' in terminal and
            "retail_display_restore(&display);" in terminal,
            "terminal escape/fallback contract changed")
    stage = sources["src/loader/vmu_native_passive_stage.inc"]
    fragments = ("memcpy((void *)(uintptr_t)(map_address+UINT32_C(0x20000000)),&manifest,",
                 "memcpy((void *)(uintptr_t)(region_address+UINT32_C(0x20000000)),native_background_boot_region,",
                 "kui_native_passive_publish(region_address,region_address+KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES);",
                 "if(bind_map(&manifest,(const void *)(uintptr_t)map_address)!=1)")
    positions = [stage.index(fragment) for fragment in fragments]
    require(positions == sorted(positions), "stage must copy map/region and publish executable vectors before binding")
    before_map = stage[:positions[0]]
    before_region = stage[positions[0]:positions[1]]
    require('volatile("ocbp @%0"' in before_map and 'volatile("ocbp @%0"' in before_region,
            "P1 lines must be purged before both P2 copies")
    heap = sources["src/loader/vmu_native_heap_probe_stage.c"]
    require("mode==KUI_VMU_NATIVE_MODE_CONTROL || mode==KUI_VMU_NATIVE_MODE_LEASE_ONLY" in heap and
            'stopped("BACKGROUND VMU REQUIRES LIVE WORKER RESERVATION"' in heap,
            "retained workspaces require the live native allocation")
    retail_stage = sources["src/loader/retail_stage.c"]
    require("int initialized=init(&manifest,&card,firmware,&display,native_background_boot_region);" in retail_stage and
            "const struct kui_retail_storage *,uint32_t,const struct retail_display_state *,void *);" in retail_stage,
            "stage must pass the protected workspace as the native initializer's fifth argument")
    return {"geometry": {key.removeprefix("KUI_VMU_NATIVE_BACKGROUND_").lower(): value
                          for key, value in expected.items()},
            "map_and_region_p2_copy_before_vector_publication_before_bind": True,
            "control_and_lease_only_modes_refused": True,
            "stage_supplies_fifth_workspace_argument": True,
            "sha256": {path: digest(text.encode()) for path, text in sources.items()}}


def stage_contract(directory, resident, *, self_test=False,read_ahead_proof=None):
    path = directory / "stage.elf"
    if not path.is_file():
        return {"available": False, "complete_module_audited": False}
    data = path.read_bytes()
    inspected = inspect_elf(data, 0x8CE00000, 0x8CFE0000)
    image = read_elf(path)
    progress=image[4].get('__kui_native_cold_progress_stage')
    witness=False
    if progress is not None:
        from check_native_controller_failure_witness import witness_companion
        worker=read_elf(directory/'native-worker.elf')
        witness=witness_companion(worker,read_ahead_proof)
        require(witness and progress==(1,0,0,0xfff1) and
                image[4].get('__kui_native_dual_readers_stage')==(1,0,0,0xfff1),
                'Background cold progress pins require the complete matching dual witness worker proof')
    require(exact_profile_marker(data, "__kui_vmu_native_background_stage"), "missing Background stage marker")
    workspace, _ = typed(image, "_native_background_boot_region", 1, size=0x760, writable=True)
    # Stage wire decoding owns 160 slots. The validated native Background map
    # retains only its first 64 slots (1020 bytes) in the already proved lease.
    manifest, _ = typed(image, "_manifest", 1, size=2172, writable=True)
    installer, _ = typed(image, "_native_installer_stack", 1, size=0x10000, writable=True)
    typed(image, "_native_passive_run", 2)
    # This authored assembly entry is an executable NOTYPE label; its exact
    # cache-publication byte sequence is pinned by the primary native audit.
    publisher = image[4].get("_kui_native_passive_publish")
    require(publisher is not None and publisher[2] == 0 and publisher[3] < len(image[3]) and
            image[3][publisher[3]][2] & 6 == 6 and
            image[3][publisher[3]][3] <= publisher[0] <
            image[3][publisher[3]][3] + image[3][publisher[3]][5],
            "missing executable stage cache-publication entry")
    require(not workspace & 31 and not manifest & 3 and
            not installer & 31 and (workspace + 0x760 <= manifest or manifest + 2172 <= workspace),
            "stage map/vector workspaces overlap or have wrong alignment")
    generated = (directory / "vmu_maple_observe_symbols.h").read_text()
    bindings = re.findall(r"^#define KUI_VMU_NATIVE_BACKGROUND_BIND (0x[0-9a-fA-F]+)[uU]?$", generated, re.M)
    binder = resident["symbols"]["_kui_vmu_native_background_manifest_bind"]
    require(len(bindings) == 1 and int(bindings[0], 16) == binder,
            "stage generated binder address differs from the actual low binder")
    _, installer_code, _ = symbol_bytes(image, "_native_passive_run", 2)
    require(struct.pack("<I", binder) in installer_code,
            "actual installer lacks the admitted low binder constant")
    embedded = {}
    for transport in ("scif", "sci", "ide", "scia"):
        begin = image[4].get("__retail_resident_" + transport + "_blob_start")
        end = image[4].get("__retail_resident_" + transport + "_blob_end")
        require(begin is not None and end is not None and end[0] - begin[0] == len(resident["payload"]),
                "stage embedded resident size differs: " + transport)
        offset = begin[0] - 0x8CE00000
        require(inspected["payload"][offset:offset + len(resident["payload"])] == resident["payload"],
                "stage embedded bytes differ from the inspected Background resident: " + transport)
        embedded[transport] = hex(begin[0])
    return {"available": True, "complete_module_audited": False,
            "stage_sha256": digest(data), "workspace": hex(workspace), "workspace_bytes": 0x760,
            "map": hex(manifest), "stage_map_object_bytes": 2172, "retained_map_prefix_bytes": 1020,
            "installer_stack": hex(installer), "installer_stack_bytes": 0x10000,
            "generated_binder": hex(binder), "embedded_resident_copies": embedded,
            "tail_copy_replay": compiled_tail_copy(image, resident, self_test=self_test,
                lease_bytes=0x60000 if '__kui_native_readahead_stage' in image[4] else 0x30000,
                witness=witness),
            "memory_end": hex(inspected["memory_end"]),
            "scope": "stage bounds/profile/workspaces; full envelope and worker audited separately"}


def check(directory, *, self_test=False, require_stage=False):
    directory = Path(directory)
    path = directory / "resident-sci.elf"
    data = path.read_bytes()
    inspected = inspect_elf(data, LOW, BOTTOM)
    image = read_elf(path)
    symbols = inspected["symbols"]
    require(exact_profile_marker(data, "__kui_vmu_native_background_resident"),
            "missing exact repaired native Background resident profile")
    require(symbols.get("__retail_hook_stack_bottom") == BOTTOM and symbols.get("__retail_hook_stack") == TOP,
            "native Background must retain the exact 352-byte stack and guard boundaries")
    require(symbols.get("__retail_resident_bss_end") == inspected["memory_end"] <= BOTTOM and
            LOW + len(inspected["payload"]) <= symbols.get("__retail_resident_bss_begin", 0),
            "resident load/BSS boundaries changed")
    pointer, _ = typed(image, "_kui_retail_async_region_storage", 1, size=4, writable=True)
    require(not pointer & 3 and symbols["__retail_resident_bss_begin"] <= pointer < pointer + 4 <= BOTTOM,
            "relocatable region pointer escapes low BSS")
    require("_kui_retail_async_region" not in symbols, "Background retained a fixed low vector region")
    for function in (*ROOTS, "kui_vmu_native_background_init", "kui_vmu_native_background_manifest_bind",
                     "kui_vmu_observe_low_failure", "native_background_map"):
        typed(image, "_" + function, 2)
    # Reuse the existing exact one-function section proof: SH relaxation may
    # retain a larger declared ST_SIZE than the allocated gateway span.
    lease, lease_bytes, declared_lease_bytes = sd_gateway_span(data, inspected)
    require(symbols.get("__vmu_sd_lease_begin") <= lease < lease + lease_bytes <= symbols.get("__vmu_sd_lease_end"),
            "worker-only storage gateway escapes its exported section")
    require(struct.unpack_from("<I", inspected["payload"], 32)[0] ==
            symbols["_kui_vmu_native_background_init"],
            "low entry must name the separate five-argument Background initializer")
    binary = directory / "resident-sci.bin"
    if binary.is_file():
        require(binary.read_bytes() == inspected["payload"], "resident binary differs from the inspected ELF")
    disassembly = disassemble(path)
    instructions = instruction_audit("native-background-resident", disassembly)
    helper_names = [name for name, record in image[4].items() if record[2] == 2 and name.startswith("___")]
    require(helper_names == ["___udivsi3"], "unmodeled compiler-library functions: " + repr(helper_names))
    helper_address, helper_bytes = typed(image, "___udivsi3", 2)
    library = library_cfg(rows(disassembly), helper_address, helper_bytes)
    reports = sorted(directory.glob("resident-sci.elf.ltrans*.ci"))
    if not reports:
        reports = sorted((directory / "resident/lto").glob("*.ci"))
    require(reports, "missing actual LTO call-graph reports")
    if '__kui_native_loader_timeline_resident' in image[4]:
        # The only new low-stack callback is pinned to the retained trace
        # entry. Load its actual emitted frames and all pure callees; the
        # same352B guard and margins continue to bound the combined path.
        reports += [directory/'native/src/loader/vmu_native_passive.ci',
                    directory/'native/src/core/native_loader_timeline.ci',
                    directory/'native/src/core/vmu_native_profile.ci',
                    directory/'native/src/loader/minic.ci']
        require(all(p.is_file() for p in reports),'missing timeline callback stack graphs')
    frames, calls = graph.load(reports,qualified='__kui_native_loader_timeline_resident' in image[4])
    roots=dict(ROOTS)
    if '__kui_native_sdk_sci_route_resident' in image[4]:
        # The separately instruction-modelled SDK RTS gateway saves its
        # owner frame on the SDK stack, then enters C at private stack top.
        # Walk this actual additional C dispatcher, not just its IRQ callee.
        roots['kui_retail_sdk_sci_dispatch']=0
    readahead='__kui_native_readahead_resident' in image[4]
    companion={'enabled':False}
    if readahead:
        from check_vmu_native_readahead import check as check_read_ahead
        companion=check_read_ahead(directory,self_test=self_test)
        require(companion.get('enabled') and companion.get('paired_markers') and
                companion.get('gateway',{}).get('parked_owner_stack_bytes')==20,
                'Background read-ahead stack boundary lacks its complete companion proof')
    switched={('deliver','retail_async.c'):20} if readahead else None
    paths, callbacks, escapes = stack_paths(frames, calls, roots,switched)
    worst = max(item["bytes"] for item in paths.values())
    total = worst + library["stack_bytes"] + EXTRA_MARGIN + GUARD + TOP_GAP
    require(total <= TOP - BOTTOM,
            f"Background private stack needs {total} bytes including its guard/top gap; available {TOP-BOTTOM}")
    stage = stage_contract(directory, inspected, self_test=self_test,read_ahead_proof=companion)
    require(not require_stage or stage["available"], "complete-profile audit requires the linked stage")
    # A legacy engine in the dual module also owns384KiB, while retaining
    # raw vectors. Do not infer192KiB merely from absence of the RA marker:
    # pin every linked refusal/terminal guard before selecting the wide span.
    from check_vmu_native_map_refusal import signature as guard_signature,READAHEAD_PINS
    wide_lease=guard_signature(image,disassembly,'_native_resident_terminal')[:2]==READAHEAD_PINS['_native_resident_terminal']
    lease_guards={}
    if wide_lease:
        for name,expected in READAHEAD_PINS.items():
            actual=guard_signature(image,disassembly,name)
            require(actual[:2]==expected,'wide Background lease guard instructions changed: '+name)
            lease_guards[name]={'sha256':actual[0],'bytes':actual[1]}
    require(not readahead or wide_lease,'read-ahead lost its exact384KiB low guards')
    source = source_contract(readahead,wide_lease)
    result = {"schema": "kui.native.background.audit.v1", "console_execution_verified": False,
              "resident_sha256": digest(data), "payload_bytes": len(inspected["payload"]),
              "memory_end": hex(inspected["memory_end"]), "low_limit": hex(BOTTOM),
              "selected_guarded_lease_bytes":0x60000 if wide_lease else 0x30000,
              "wide_lease_linked_guard_pins":lease_guards,
              "region_pointer": {"address": hex(pointer), "bytes": 4, "fixed_region_absent": True},
              "worker_stack_gateway": {"address": hex(lease), "allocated_bytes": lease_bytes,
                                       "declared_function_bytes": declared_lease_bytes},
              "profile": {"native_background": True, "sci_resume_fix": True,
                          "logical_drive_state": True, "mounted_disc_check": True},
              "linked_integer_instructions": instructions,
              "private_stack": {"available_bytes": TOP - BOTTOM, "worst_c_and_entry_bytes": worst,
                                "linked_library": library, "additional_margin_bytes": EXTRA_MARGIN,
                                "generic64_byte_library_margin_used": False,
                                "guard_bytes": GUARD, "top_gap_bytes": TOP_GAP,
                                "conservative_total_bytes": total, "remaining_bytes": TOP - BOTTOM - total,
                                "worst_total_bytes": total,
                                "paths": paths, "callback_sites": len(callbacks),
                                "nonreturning_boundaries": [{"function": key[0], "source": key[1],
                                                             "contract": ESCAPES[key]} for key in escapes],
                                "installer_and_worker_roots_excluded": sorted(COLD)},
              "source_contract": source, "stage": stage,"read_ahead_companion":companion}
    if self_test:
        rejected = []
        def refuse(name, callback):
            try:
                callback()
            except (ValueError, AssertionError):
                rejected.append(name)
            else:
                raise ValueError("Background audit accepted negative mutation: " + name)
        altered = dict(frames)
        key=next(name for name in altered if graph.base(name)=="native_background_map")
        altered[key] += 128
        def oversized():
            checks, _, _ = stack_paths(altered, calls, roots,switched)
            need = max(item["bytes"] for item in checks.values()) + 4 + EXTRA_MARGIN + GUARD + TOP_GAP
            require(need <= TOP - BOTTOM, "oversized private stack")
        refuse("guarded_map_stack_growth", oversized)
        unknown = {name: list(edges) for name, edges in calls.items()}
        unknown["kui_retail_async_irq"].append(("__indirect_call", "unknown_callback.c:1:1"))
        refuse("unknown_irq_callback", lambda: stack_paths(frames, unknown, roots,switched))
        escaped = {name: list(edges) for name, edges in calls.items()}
        escaped["kui_retail_async_irq"].append(("kui_vmu_observe_sd_lease", "retail_async.c:1:1"))
        refuse("worker_gateway_on_gd_stack", lambda: stack_paths(frames, escaped, roots,switched))
        recursive = {name: list(edges) for name, edges in calls.items()}
        recursive["kui_retail_async_irq"].append(("kui_retail_async_irq", "retail_async.c:1:1"))
        refuse("recursive_irq_path", lambda: stack_paths(frames, recursive, roots,switched))
        decoded = rows(disassembly)
        mutated = dict(decoded)
        mutated[helper_address + 2] = (0x60F3, "mov", "r15,r0")
        refuse("hidden_library_stack_alias", lambda: library_cfg(mutated, helper_address, helper_bytes))
        mutated = dict(decoded)
        mutated[helper_address] = (0x0009, "nop", "")
        refuse("unbalanced_library_pop", lambda: library_cfg(mutated, helper_address, helper_bytes))
        mutated = dict(decoded)
        mutated[helper_address + 2] = (0xAFFE, "bra", "self")
        refuse("cyclic_library_helper", lambda: library_cfg(mutated, helper_address, helper_bytes))
        mutated = dict(decoded)
        mutated[helper_address + 2] = (0xA7FF, "bra", "outside")
        refuse("library_branch_outside_window", lambda: library_cfg(mutated, helper_address, helper_bytes))
        result["self_test"] = {"refused_mutations": rejected}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--require-stage", action="store_true")
    options = parser.parse_args()
    print(json.dumps(check(options.directory, self_test=options.self_test, require_stage=options.require_stage), indent=2))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, AssertionError) as error:
        sys.exit("Native Background audit failed: " + str(error))
