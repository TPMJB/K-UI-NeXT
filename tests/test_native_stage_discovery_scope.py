#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked four-argument fresh-discovery operand-cache scope.

The real immutable discovery leaf has separate semantic equivalence tests.
Here an ABI adversary writes every declared private output, a deep private
stack word, and either success or a partial-refusal result. Only the pinned
four-instruction whole-RAM OCBP loop is folded. No device timing is modeled.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

ROOT = Path(os.environ.get('KUI_DISCOVERY_AUDIT_ROOT', Path(__file__).resolve().parents[1]))
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from check_vmu_native_worker_gateway import CPU, MASK, read_elf, symbol_bytes
from check_vmu_native_cache_gateway import CacheGateway
from check_retail_stack import load as load_graph, NODE
from check_disc_vmu_handoff import Image
from check_native_bottom_heap import records as qualified_records

WRAPPER = '_kui_native_stage_cached_discover_image'
LEAF = '_kui_native_stage_discover_image'
MARKER = '__kui_native_cached_discovery'
CCR, MMUCR = 0xff00001c, 0xff000010
RAM, END, STACK, LOAD = 0x8c000000, 0x8d000000, 0x8cff0000, 0x8c010000
PURGE_OPS = struct.pack('<4H', 0x00a3, 0x7020, 0x3012, 0x8bfb)
OUTPUTS = ('_native_driver', '_passive_layout', '_native_heap', '_native_call', '_native_exits')
OPTIONAL_OUTPUTS = ('_native_fixed_hook', '_native_fixed', '_native_arena')
OPS = (
    0x2f86, 0x2f96, 0x4f22, 0xe800, 0x0002, 0xd132, 0x2019, 0x3010,
    0x8b42, 0xd131, 0x6012, 0xc801, 0x8b3e, 0xd130, 0x6012, 0xd230,
    0x3020, 0x8b39, 0xd031, 0x3600, 0x8b36, 0x3640, 0x8b34, 0xd02d,
    0x3402, 0x8b31, 0xd12c, 0x3412, 0x892e, 0x6213, 0x3248, 0x3252,
    0x8b2a, 0x2558, 0x8928, 0x3702, 0x8b26, 0x3712, 0x8924, 0x6273,
    0x7204, 0x3216, 0x8920, 0x6073, 0xc803, 0x8b1d, 0x3742, 0x8b03,
    0x6043, 0x305c, 0x3702, 0x8b17, 0xd021, 0x3f02, 0x8b14, 0x60f3,
    0xc803, 0x8b11, 0x700c, 0x3016, 0x890e, 0xe801, 0xd01d, 0x402b, 9,
    0xd116, 0xd01c, 0x2102, *([9] * 8), 0xd018, 0x400b, 9, 0x6903,
    0x2888, 0x8910, 0xd00f, 0xd110, 0x00a3, 0x7020, 0x3012, 0x8bfb,
    0xd10a, 0xd013, 0x2102, *([9] * 8), 0x6093, 0x4f26, 0x69f6,
    0x68f6, 0x000b, 9, 9)


def require(value, message):
    if not value:
        raise ValueError(message)


def wrapper_binding(body, begin, leaf):
    expected = struct.pack('<106H12I', *OPS, 0x400000f0, MMUCR, CCR, 0x100, RAM, END,
                           LOAD, RAM + 65536, begin + 130 + 0x20000000, 0x909, leaf, 0x908)
    require(body == expected, 'Fresh-discovery wrapper instructions/literals changed')


def private_outputs(stage):
    result = {}
    for name in (*OUTPUTS, *OPTIONAL_OUTPUTS):
        if name not in stage[4] and name in OPTIONAL_OUTPUTS:
            continue
        address, size, kind, owner = stage[4][name]
        section = stage[3][owner]
        require(kind == 1 and size > 0 and not (address | size) & 3 and
                section[1] == 8 and section[2] & 7 == 3 and
                RAM <= section[3] <= address < address + size <= section[3] + section[5] <= END,
                'Discovery output is not private stage RAM BSS: ' + name)
        result[name] = (address, size)
    spans = list(result.values())
    require(all(a + n <= b or b + m <= a for i, (a, n) in enumerate(spans)
                for b, m in spans[:i]), 'Discovery private outputs overlap')
    return result


class Scope(CacheGateway):
    def __init__(self, stage, *, seed=7, mask=15, md=True, ccr=0x100, mmu=0,
                 source=LOAD, size=1848288, load=LOAD, output=0x8cfe0000,
                 stack=STACK, result=1, detail=0, body=None):
        begin, linked, _ = symbol_bytes(stage, WRAPPER, 2)
        CPU.__init__(self, seed, begin, 1)
        self.blob = linked if body is None else body
        self.pc, self.r[15], self.mem = begin, stack, {}
        self.start_sp = stack
        self.sr = (self.sr & ~0x400000f0) | int(md) << 30 | mask << 4
        self.r[4:8] = [source, size, load, output]
        self.args = self.r[4:8].copy()
        self.pr = begin + len(self.blob) + 0x100
        self.before = (self.r.copy(), self.pr, self.sr, self.fpscr, self.fr.copy(), self.gbr)
        self.owner_ccr = self.ccr = ccr
        self.mmu, self.result, self.detail = mmu, result, detail
        self.cached = (md and mask == 15 and ccr == 0x100 and not mmu & 1 and
                       source == load == LOAD and 0 < size <= END - source and
                       RAM <= output <= END - 4 and not output & 3 and
                       (output < source or output >= source + size) and
                       RAM + 65548 <= stack <= END and not stack & 3)
        self.targets = {stage[4][LEAF][0]: 'discovery'}
        self.outputs = private_outputs(stage)
        self.calls, self.ccr_writes, self.events, self.controls = [], [], [], []
        self.wait, self.purged, self.purge_lines, self.dirty = 0, False, 0, set()
        self.expected_output = {}

    def code(self, address, size):
        offset = (address - self.base) & MASK
        if offset >= 0x20000000:
            offset -= 0x20000000
        assert 0 <= offset < offset + size <= len(self.blob), 'scope left linked body'
        return self.blob[offset:offset + size]

    def read(self, address):
        if address in (CCR, MMUCR):
            assert self.sr & 0x400000f0 == 0x400000f0, 'controls read before CPU admission'
            self.controls.append(address)
            return self.ccr if address == CCR else self.mmu
        if address in self.mem:
            assert self.ccr & 1 or address not in self.dirty, 'private data not published'
            return self.mem[address]
        return struct.unpack('<I', self.code(address, 4))[0]

    def write(self, address, value):
        if address == CCR:
            assert self.base + 0x20000000 <= self.pc < self.base + 0x20000000 + len(self.blob)
            assert self.cached and not self.wait and len(self.ccr_writes) < 2
            assert value == (0x908 if self.ccr_writes else 0x909), 'unexpected cache profile'
            if self.ccr_writes:
                assert self.purged and not self.dirty, 'cache disabled before publication'
            self.ccr_writes.append(value)
            self.events.append(('ccr', value))
            self.ccr, self.wait = value & ~0x808, 8
            return
        assert not address & 3 and self.start_sp - 12 <= address < self.start_sp
        self.mem[address] = value & MASK
        if self.ccr & 1:
            self.dirty.add(address)

    def plain(self, word, pc):
        if self.wait:
            assert word == 9, 'cache update lacks eight following NOPs'
            self.wait -= 1
        assert word >> 12 != 0xf, 'discovery scope introduced FPU instructions'
        if word == 0x0002:
            self.r[0] = self.sr
            return
        if word & 0xf00f == 0x3008:
            n, m = word >> 8 & 15, word >> 4 & 15
            self.r[n] = (self.r[n] - self.r[m]) & MASK
            return
        if word & 0xf00f == 0x300c:
            n, m = word >> 8 & 15, word >> 4 & 15
            self.r[n] = (self.r[n] + self.r[m]) & MASK
            return
        if word == 0x00a3:
            assert self.code(pc, 8) == PURGE_OPS, 'whole-RAM publication loop changed'
            assert self.r[:2] == [RAM, END] and self.calls == ['discovery']
            assert self.cached and self.ccr_writes == [0x909] and not self.purged
            assert all(RAM <= address < END for address in self.dirty)
            self.purge_lines = (END - RAM) // 32
            self.r[0] = END - 32
            self.purged = True
            self.dirty.clear()
            self.events.append(('publish', self.purge_lines))
            return
        super().plain(word, pc)

    def leaf_write(self, address, value):
        self.mem[address] = value & MASK
        self.expected_output[address] = value & MASK
        if self.ccr & 1:
            self.dirty.add(address)

    def api(self, target):
        assert target in self.targets and target & 0xff000000 == RAM
        assert not self.wait and not self.calls and self.r[4:8] == self.args
        assert self.r[15] == self.start_sp - 12
        assert self.sr & 0x400000f0 == self.before[2] & 0x400000f0
        assert self.ccr == (0x101 if self.cached else self.owner_ccr)
        assert self.ccr_writes == ([0x909] if self.cached else [])
        self.calls.append('discovery')
        self.events.append(('discovery', self.result))
        return_pc = self.pr
        for index in (*range(1, 33), 16384):
            address = (self.r[15] - 4 * index) & MASK
            self.mem[address] = (0x13579bdf + 7 * index) & MASK
            if self.ccr & 1:
                self.dirty.add(address)
        for address, size in self.outputs.values():
            for offset in range(0, size, 4):
                if not self.result and offset & 12:
                    continue
                self.leaf_write(address + offset, 0x92345678 + offset * 13 + self.detail)
        # Invalid argument cases model only the cache policy and exact ABI
        # forwarding; they do not grant a safe dereference of a bad pointer.
        if not self.args[3] & 3:
            self.leaf_write(self.args[3], self.detail)
        for i in range(8):
            self.r[i] = (0x913579bd + i * 0x13579bd) & MASK
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
        assert self.calls == ['discovery'] and self.ccr == self.owner_ccr and not self.wait
        assert self.ccr_writes == ([0x909, 0x908] if self.cached else [])
        assert self.purged == self.cached and self.purge_lines == (524288 if self.cached else 0)
        assert all(self.read(address) == value for address, value in self.expected_output.items())
        assert self.events == ([('ccr', 0x909), ('discovery', self.result), ('publish', 524288),
                                ('ccr', 0x908)] if self.cached else [('discovery', self.result)])
        expected_controls = ([MMUCR, CCR] if sr & 0x400000f0 == 0x400000f0 and not self.mmu & 1
                             else [MMUCR] if sr & 0x400000f0 == 0x400000f0 else [])
        assert self.controls == expected_controls


def configurations():
    configs = [dict(mask=mask) for mask in range(16)]
    configs += [dict(ccr=ccr) for ccr in (0, 1, 4, 0x20, 0x80, 0x105, 0x109, 0x909, MASK)]
    configs += [dict(mmu=mmu) for mmu in (1, 0x201, MASK, 0x200, 0x400)]
    configs += [dict(md=False)]
    configs += [dict(source=source, size=size, load=load) for source, size, load in (
        (LOAD, 1, LOAD), (LOAD, END - LOAD, LOAD), (LOAD, END - LOAD + 1, LOAD),
        (LOAD, 0, LOAD), (LOAD, MASK, LOAD), (LOAD + 4, 4096, LOAD + 4),
        (LOAD, 4096, LOAD + 4), (LOAD - 4, 4096, LOAD),
        (0xac010000, 4096, LOAD), (0x0c010000, 4096, LOAD), (0, 1, LOAD),
        (MASK, 2, MASK))]
    configs += [dict(output=output) for output in (
        RAM, RAM + 1, LOAD - 4, LOAD, LOAD + 4, LOAD + 1848284, LOAD + 1848288,
        END - 4, END - 3, END, RAM - 4, 0xacfe0000, 0x0cfe0000, 0, MASK)]
    configs += [dict(stack=stack) for stack in (
        END, END + 4, STACK + 28, RAM + 65548, RAM + 65544, RAM + 12,
        0xacff0000, 0x0cff0000)]
    return configs


def closed_depth(frames, calls, root):
    """Pure C leaves need no indirect targets or borrowed live callback set."""
    seen, memo = set(), {}
    forbidden = ('native_sdk_coherence_', 'native_passive_capture_windows', 'native_report',
                 'stopped', 'retail_display_', 'kui_retail_storage_', 'native_publish')
    def walk(node, path):
        require(node not in path, 'Recursive immutable discovery graph: ' + node)
        require(node != '__indirect_call', 'Immutable discovery reached an indirect callback: ' + ' -> '.join(path))
        require(node in frames, 'Missing emitted immutable discovery frame: ' + node)
        short = node.rsplit(':', 1)[-1].split('.', 1)[0]
        require(not any(short.startswith(prefix) for prefix in forbidden),
                'Live SDK, display, I/O or publication entered immutable discovery: ' + node)
        if node in memo:
            return memo[node]
        seen.add(node)
        best = (0, [])
        for callee, _ in calls.get(node, []):
            child = walk(callee, (*path, node))
            if child[0] > best[0]:
                best = child
        result = (frames[node] + best[0], [node, *best[1]])
        memo[node] = result
        return result
    depth, path = walk(root, ())
    return depth, path, seen


def graph_controls():
    frames = {'root': 16, 'leaf': 40}
    cases = (
        ({'root': [('missing', '')]}, frames),
        ({'root': [('root', '')]}, frames),
        ({'root': [('__indirect_call', '')]}, frames),
        ({'root': [('native_publish', '')]}, {**frames, 'native_publish': 8}),
        ({'root': [('native_sdk_coherence_slot', '')]}, {**frames, 'native_sdk_coherence_slot': 8}),
        ({'root': [('kui_retail_storage_read_run', '')]}, {**frames, 'kui_retail_storage_read_run': 8}),
    )
    require(closed_depth(frames, {'root': [('leaf', '')]}, 'root')[:2] ==
            (56, ['root', 'leaf']), 'Stack depth failed its independent sum oracle')
    for calls, rows in cases:
        try:
            closed_depth(rows, calls, 'root')
        except ValueError:
            continue
        raise ValueError('Immutable stack graph accepted a missing, recursive or live callback')
    return len(cases)


def immutable_read_callback(stage, calls):
    """Close only Direct32's actual, unchanged immutable-image callback."""
    source_pins = {
        'src/core/vmu_native_heap.c': '7d99629da1954d1332c1da653a67b9d7139ee488c0cc33e78946f207f88594e2',
        'src/core/vmu_native_heap_bottom_code.inc': 'fb014152c9e2d69b73e40ab497e68dad4a49b4a84b770b8d1c37895b33e9c0c0',
    }
    for path, expected in source_pins.items():
        require(hashlib.sha256((ROOT / path).read_bytes()).hexdigest() == expected,
                'Reprove the cold immutable read callback after changing ' + path)
    records = qualified_records(stage)
    callback, body, kind, _ = records['_image_word@vmu_native_heap.c']
    index = records['_index@vmu_native_heap.c'][0]
    require(kind == 2 and len(body) == 96 and
            hashlib.sha256(body[:-4]).hexdigest() == '393adb95a2f28b0bd6d74784ec5168d9a07550b79962f6dd459c0b2092d1720a' and
            struct.unpack_from('<I', body, 92)[0] == index,
            'Cold read callback no longer has its exact bounded image-word implementation')
    begin, discover, _ = symbol_bytes(stage, '_kui_vmu_native_heap_discover', 2)
    validator = stage[4]['_kui_vmu_native_heap_bottom_code_valid'][0]
    windows = []
    for at in range(0, len(discover) - 7, 2):
        ops = struct.unpack_from('<4H', discover, at)
        if ops[0] >> 8 != 0xd0 or ops[1] >> 8 != 0xd5 or ops[2:] != (0x400b, 0x64d3):
            continue
        validator_slot = ((begin + at + 4) & ~3) + 4 * (ops[0] & 255) - begin
        callback_slot = ((begin + at + 6) & ~3) + 4 * (ops[1] & 255) - begin
        if not (0 <= validator_slot <= len(discover) - 4 and 0 <= callback_slot <= len(discover) - 4):
            continue
        if (struct.unpack_from('<I', discover, validator_slot)[0],
                struct.unpack_from('<I', discover, callback_slot)[0]) == (validator, callback):
            windows.append(begin + at)
    require(len(windows) == 1, 'Heap discovery lacks its unique actual image-word callback binding')
    closed = {name: list(edges) for name, edges in calls.items()}
    sites = []
    for name in ('src/core/vmu_native_heap.c:read_is', 'src/core/vmu_native_heap.c:heap_bottom_halfword'):
        edges = closed.get(name, [])
        require(sum(target == '__indirect_call' for target, _ in edges) == 1,
                'Immutable image reader callback site changed: ' + name)
        closed[name] = [('src/core/vmu_native_heap.c:image_word' if target == '__indirect_call' else target, label)
                        for target, label in edges]
        sites.append(name)
    return closed, dict(callback_address=hex(callback), caller_window=hex(windows[0]),
        exact_readonly_callback_bytes=96, closed_callback_sites=sites, source_sha256=source_pins)


def selected_caller(stage, body=None):
    begin, original, _ = symbol_bytes(stage, '_kui_retail_stage_relay', 2)
    body = original if body is None else body
    wrapper = stage[4][WRAPPER][0]
    finish = stage[4]['_native_passive_prepare_finish'][0]
    size_address = stage[4]['_exec_bytes'][0]
    def literal(at, reg):
        op = struct.unpack_from('<H', body, at)[0]
        if op >> 8 != 0xd0 | reg:
            return None
        slot = ((begin + at + 4) & ~3) + 4 * (op & 255) - begin
        return struct.unpack_from('<I', body, slot)[0] if 0 <= slot <= len(body) - 4 else None
    found = []
    for at in range(0, len(body) - 27, 2):
        ops = struct.unpack_from('<14H', body, at)
        if literal(at, 0) != wrapper:
            continue
        require(ops[1] == 0x67f3 and literal(at + 4, 6) == LOAD and ops[3] == 0x7708 and
                ops[4] & 0xff0f == 0x6502 and ops[5] & 0xff0f == 0x1f02 and
                ops[6:9] == (0x400b, 0x6463, 0x2008) and ops[9] >> 8 == 0x8d and
                ops[10] == 0x55f2 and literal(at + 22, 1) == finish and ops[12:] == (0x410b, 9),
                'Actual fresh-discovery call lost its four-argument or success-only continuation window')
        size_reg = ops[4] >> 4 & 15
        zero_reg = ops[5] >> 4 & 15
        require(size_reg >= 8 and zero_reg >= 8 and size_reg != zero_reg,
                'Discovery source size/detail initialization lost its nonvolatile owners')
        require(any(literal(before, size_reg) == size_address for before in range(0, at, 2)),
                'Actual relay no longer binds current executable size')
        miss_guards = []
        replay = stage[4]['_native_resolution_replay'][0]
        for before in range(0, at - 11, 2):
            guard = struct.unpack_from('<6H', body, before)
            if literal(before, 0) != replay or guard[1:4] != (0x400b, 9, 0x2008) or \
                    guard[4] >> 8 != 0x8d or guard[5] != (0x6003 | zero_reg << 8):
                continue
            jump = guard[4] & 255
            jump -= 256 if jump & 128 else 0
            if before + 12 + 2 * jump == at - 12:
                miss_guards.append(before)
        require(len(miss_guards) == 1,
                'Discovery call no longer follows an actual saved-mapping miss with zero refusal detail')
        displacement = ops[9] & 255
        displacement -= 256 if displacement & 128 else 0
        failure = begin + at + 22 + 2 * displacement
        require(not begin + at + 22 <= failure <= begin + at + 26,
                'Discovery refusal falls into the live coherence continuation')
        found.append(dict(window=hex(begin + at), call=hex(begin + at + 12),
            source_and_load=hex(LOAD), size_object=hex(size_address), detail_stack_offset=8,
            failure_branch=hex(failure), separate_live_continuation=hex(finish),
            live_continuation_only_after_success=True))
    require(len(found) == 1, 'Actual native relay lacks one uniquely selected discovery call')
    return found[0]


def caller_controls(stage, selected):
    begin, body, _ = symbol_bytes(stage, '_kui_retail_stage_relay', 2)
    at = int(selected['window'], 16) - begin
    mutations = []
    for offset, opcode in ((4, 0x6503), (8, 0x6593), (12, 9), (14, 0x6453),
                           (18, 0x8f00), (24, 9)):
        changed = bytearray(body)
        struct.pack_into('<H', changed, at + offset, opcode)
        mutations.append(bytes(changed))
    for name in (WRAPPER, '_native_passive_prepare_finish'):
        address = stage[4][name][0]
        needle = struct.pack('<I', address)
        require(body.count(needle) == 1, 'Actual callsite needs its unique typed literal')
        changed = bytearray(body)
        struct.pack_into('<I', changed, body.index(needle), address + 2)
        mutations.append(bytes(changed))
    for changed in mutations:
        try:
            selected_caller(stage, changed)
        except ValueError:
            continue
        raise ValueError('Actual discovery callsite accepted unsafe argument/continuation mutation')
    return len(mutations)


def stack_bound(directory, stage):
    directory = Path(directory)
    reports = sorted((directory / 'stage').rglob('*.ci'))
    require(reports, 'Fresh discovery requires its actual emitted stage callgraphs')
    report_hashes = {}
    su_rows = {}
    origins = {}
    for report in reports:
        text = report.read_text()
        require(text.rstrip().endswith('\n}'), 'Truncated emitted stack graph: ' + str(report))
        report_hashes[str(report.relative_to(directory))] = hashlib.sha256(report.read_bytes()).hexdigest()
        su = report.with_suffix('.su')
        require(su.is_file(), 'Compiler callgraph has no paired stack-usage report: ' + str(report))
        report_hashes[str(su.relative_to(directory))] = hashlib.sha256(su.read_bytes()).hexdigest()
        for row in su.read_text().splitlines():
            definition, amount, kind = row.split('\t')
            source, _, _, name = definition.rsplit(':', 3)
            key = (source, name.split('.', 1)[0])
            require(kind == 'static', 'Dynamic frame in stage stack usage: ' + definition)
            su_rows[key] = max(su_rows.get(key, 0), int(amount))
        for title, label in NODE.findall(text):
            fields = label.split(r'\n')
            if len(fields) >= 3 and 'bytes (static)' in fields[2]:
                origins[title] = fields[1].rsplit(':', 2)[0]
    frames, calls = load_graph(reports, qualified=True)
    calls, callback = immutable_read_callback(stage, calls)
    depth, deepest, reached = closed_depth(frames, calls, LEAF[1:])
    for node in reached:
        require(node in origins, 'Reached discovery frame has no source-qualified origin: ' + node)
        key = (origins[node], node.rsplit(':', 1)[-1].split('.', 1)[0])
        require(key in su_rows and su_rows[key] >= frames[node],
                'Reached callgraph frame lacks a consistent .su frame: ' + node)
    relay = 'kui_retail_stage_relay'
    require(relay in frames and any(callee == WRAPPER[1:] for callee, _ in calls.get(relay, [])),
            'Actual emitted relay does not call the fresh discovery scope')
    # The four-argument assembly scope has one independently modeled12B
    # integer frame; unlike a C function it has no emitted .su/.ci report.
    required = frames[relay] + 12 + depth + 64 + 32
    require(required < 65536, 'Fresh discovery exceeds the existing cold high-stack allowance')
    image = Image(stage[0])
    resume = image.symbol('_kui_retail_game_resume')
    first, stack_load = struct.unpack('<2H', image.read(resume, 4))
    require(first == 0x68f3 and stack_load >> 8 == 0xdf,
            'Actual relay no longer selects its independent high stack')
    slot = ((resume + 6) & ~3) + 4 * (stack_load & 255)
    stack = struct.unpack('<I', image.read(slot, 4))[0]
    bss_end = stage[4]['__retail_stage_bss_end'][0]
    require(stack == STACK and bss_end <= 0x8cfe0000 and bss_end + required <= stack,
            'Fresh discovery stack can overlap the complete linked stage BSS')
    finish = [node for node in frames if node.rsplit(':', 1)[-1] == 'native_passive_prepare_finish']
    require(len(finish) == 1 and any(callee == finish[0] for callee, _ in calls.get(relay, [])),
            'Live coherence/capture continuation is not a separate actual relay call')
    require(finish[0] not in reached and
            not any('native_sdk_coherence_' in node or 'native_passive_capture_windows' in node for node in reached),
            'Live coherence or capture is inside the immutable cache scope')
    selected = selected_caller(stage)
    return dict(immutable_leaf_C_bytes=depth, relay_C_frame_bytes=frames[relay],
        assembly_scope_frame_bytes=12, library_allowance_bytes=64, guard_bytes=32,
        complete_changed_path_bound_bytes=required, existing_cold_stack_top=hex(stack),
        linked_stage_bss_end=hex(bss_end), available_above_BSS=stack - bss_end,
        no_reservation_or_worker_stack_increase=True, deepest_path=deepest,
        reached_C_functions=len(reached), unresolved_indirect_calls=0, recursion=False,
        actual_immutable_read_callback=callback,
        paired_SU_CI_reports=len(reports), report_sha256=report_hashes,
        live_coherence_capture_outside_scope=True, actual_selected_C_caller=selected,
        negative_graph_controls=graph_controls(), unsafe_caller_mutation_rejections=caller_controls(stage, selected))


def linked_sdk_preservation(stage, baseline):
    old = read_elf(baseline)
    current_address, current, _ = symbol_bytes(stage, '_native_sdk_coherence_install', 2)
    old_address, previous, _ = symbol_bytes(old, '_native_sdk_coherence_install', 2)
    require(len(current) == len(previous), 'Later live SDK installer code size changed')
    def targets(image, value):
        p1 = value - 0x20000000 if value >> 24 == 0xac else value
        result = set()
        for name, (address, size, kind, _) in image[4].items():
            if kind in (1, 2) and size and address <= p1 < address + size:
                result.add((name, p1 - address, value - p1))
        return result
    normalized = 0
    new_copy, old_copy = bytearray(current), bytearray(previous)
    # Only actual PC-relative literal words may vary. All instruction bytes,
    # internal branches, constants and literal offsets remain byte-identical.
    for at in range(0, len(current) - 1, 2):
        left, right = struct.unpack_from('<H', previous, at)[0], struct.unpack_from('<H', current, at)[0]
        if left >> 12 != 0xd:
            continue
        require(left == right, 'Live SDK installer PC-relative load changed')
        old_slot = ((old_address + at + 4) & ~3) + 4 * (left & 255) - old_address
        new_slot = ((current_address + at + 4) & ~3) + 4 * (right & 255) - current_address
        if not (0 <= old_slot <= len(previous) - 4 and 0 <= new_slot <= len(current) - 4):
            continue
        require(old_slot == new_slot, 'Live SDK installer literal offset changed')
        before, after = struct.unpack_from('<I', previous, old_slot)[0], struct.unpack_from('<I', current, new_slot)[0]
        if before == after:
            continue
        require(targets(old, before) & targets(stage, after),
                'Live SDK installer changed more than a linked symbol address')
        old_copy[old_slot:old_slot + 4] = bytes(4)
        new_copy[new_slot:new_slot + 4] = bytes(4)
        normalized += 1
    require(old_copy == new_copy, 'Later live SDK installer instructions/semantics changed')
    return dict(baseline_elf_sha256=hashlib.sha256(old[0]).hexdigest(),
        previous_function_sha256=hashlib.sha256(previous).hexdigest(),
        current_function_sha256=hashlib.sha256(current).hexdigest(), bytes=len(current),
        normalized_link_address_literals=normalized, complete_live_install_body_preserved=True)


def audit(path, baseline=None, *, require_stack=True):
    require(__debug__, 'Run linked models without Python optimization')
    stage = read_elf(path)
    require(stage[4].get(MARKER) == (1, 0, 0, 0xfff1), 'Missing fresh-discovery cache scope')
    begin, body, _ = symbol_bytes(stage, WRAPPER, 2)
    leaf, leaf_body, _ = symbol_bytes(stage, LEAF, 2)
    wrapper_binding(body, begin, leaf)
    outputs = private_outputs(stage)
    _, relay, _ = symbol_bytes(stage, '_kui_retail_stage_relay', 2)
    require(struct.pack('<I', begin) in relay, 'Native relay does not select discovery cache scope')
    cases = 0
    configs = configurations()
    for seed in (0, 7, 97):
        for config in configs:
            for result, detail in ((1, 0), (0, 10), (0, 13), (0, MASK)):
                Scope(stage, seed=seed, result=result, detail=detail, **config).run().check()
                cases += 1
    mutations = []
    # These are behavioral controls: CPU/cache guards, argument preservation,
    # private-output publication, restore profile and its required NOPs.
    for at, replacement in ((16, 9), (24, 9), (34, 9), (40, 9), (44, 9),
                            (64, 9), (68, 9), (72, 9), (90, 9), (102, 9),
                            (108, 9), (120, 9), (156, 0x6473), (168, 9),
                            (174, 9), (198, 0x6003), (200, 0x69f6), (204, 0x69f6)):
        changed = bytearray(body)
        struct.pack_into('<H', changed, at, replacement)
        mutations.append(bytes(changed))
    for at in (*range(136, 152, 2), *range(182, 198, 2)):
        changed = bytearray(body)
        struct.pack_into('<H', changed, at, 0x6003)
        mutations.append(bytes(changed))
    for literal, replacement in ((0x909, 0x908), (0x908, 0x909),
                                  (leaf, leaf + 0x20000000), (RAM + 65536, RAM + 65532)):
        changed = bytearray(body)
        at = body.find(struct.pack('<I', literal))
        require(at >= 0, 'Missing mutation target')
        struct.pack_into('<I', changed, at, replacement)
        mutations.append(bytes(changed))
    rejected = 0
    for index, changed in enumerate(mutations):
        try:
            for config in configs:
                Scope(stage, body=changed, **config).run().check()
        except (AssertionError, KeyError, struct.error):
            rejected += 1
        else:
            raise ValueError('Discovery model accepted unsafe wrapper mutation ' + str(index))
    # Pin all bytes as well, including redundant guards and literals whose
    # individual removal need not weaken the remaining behavioral guards.
    binding_rejections = 0
    for at in range(len(body)):
        changed = bytearray(body)
        changed[at] ^= 1
        try:
            wrapper_binding(bytes(changed), begin, leaf)
        except ValueError:
            binding_rejections += 1
        else:
            raise ValueError('Exact wrapper binding accepted a changed byte')
    require(Path(path).read_bytes() == stage[0], 'Stage changed during discovery scope audit')
    stack = stack_bound(Path(path).parent, stage) if require_stack else None
    sdk = linked_sdk_preservation(stage, baseline) if baseline else None
    return dict(schema='kui.native-stage.discovery-cache-scope.v1', status='PASS' if stack else 'DEVELOPMENT_PASS',
        elf_sha256=hashlib.sha256(stage[0]).hexdigest(), wrapper_bytes=len(body),
        immutable_leaf_sha256=hashlib.sha256(leaf_body).hexdigest(), argument_count=4,
        actual_SH_cases=cases, unsafe_mutation_rejections=rejected,
        exact_wrapper_byte_rejections=binding_rejections,
        private_outputs={name: dict(address=hex(address), bytes=size) for name, (address, size) in outputs.items()},
        saved_integer_frame_bytes=12, admitted_leaf_stack_headroom=65536,
        ram_publication_lines=524288, unknown_SR_MMU_CCR_and_bounds_use_original_leaf=True,
        partial_refusal_outputs_published=True, per_game_constants=False,
        actual_C_leaf_stack=stack, linked_live_SDK_install_preservation=sdk,
        limits='Actual linked wrapper instructions execute against an ABI-conforming discovery adversary. Compiler-emitted graphs bound the changed C path; immutable leaf semantics have separate C tests. No device timing or console speed is established.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--baseline', type=Path, help='Preserved previous cohort stage ELF for the later live SDK installer')
    args = parser.parse_args()
    report = audit(args.elf, args.baseline)
    rendered = json.dumps(report, indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end='')
