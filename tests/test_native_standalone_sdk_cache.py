#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the standalone's linked SDK proof scope and close its actual leaf.

The established discovery cache model supplies the SH integer interpreter and
publication oracle. The SDK finder remains a separate pure production C leaf;
this test binds its emitted closed graph and both actual fresh call sites.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
import test_native_stage_discovery_scope as model
from check_vmu_native_worker_gateway import read_elf, symbol_bytes, MASK
from check_retail_stack import load as load_graph
from check_disc_vmu_handoff import Image

WRAPPER = '_kui_native_stage_cached_sdk_family'
LEAF = '_kui_native_sdk_sci_family_find'
MARKER = '__kui_native_cached_sdk_family'
RAM, END, LOAD, STACK = model.RAM, model.END, model.LOAD, model.STACK
OPS = (
    0x2f86, 0x2f96, 0x4f22, 0xe800, 0x0002, 0xd135, 0x2019, 0x3010,
    0x8b49, 0xd134, 0x6012, 0xc801, 0x8b45, 0xd133, 0x6012, 0xd233,
    0x3020, 0x8b40, 0xd034, 0x3600, 0x8b3d, 0x3640, 0x8b3b, 0xd030,
    0x3402, 0x8b38, 0xd12f, 0x3412, 0x8935, 0x6213, 0x3248, 0x3252,
    0x8b31, 0x2558, 0x892f, 0x3702, 0x8b2d, 0x3712, 0x892b, 0x6213,
    0x3278, 0xe010, 0x3202, 0x8b26, 0x6073, 0xc803, 0x8b23, 0x3742,
    0x8b05, 0x6043, 0x305c, 0x3702, 0x8b1d, 0xa004, 0x0009, 0x6073,
    0x7010, 0x3046, 0x8917, 0xd021, 0x3f02, 0x8b14, 0x60f3, 0xc803,
    0x8b11, 0x700c, 0x3016, 0x890e, 0xe801, 0xd01d, 0x402b, 0x0009,
    0xd115, 0xd01c, 0x2102, *([9] * 8), 0xd018, 0x400b, 9, 0x6903,
    0x2888, 0x8910, 0xd00f, 0xd10f, 0x00a3, 0x7020, 0x3012, 0x8bfb,
    0xd10a, 0xd012, 0x2102, *([9] * 8), 0x6093, 0x4f26, 0x69f6,
    0x68f6, 0x000b, 9)


def require(value, message):
    if not value:
        raise ValueError(message)


class Scope(model.Scope):
    def __init__(self, *args, **kwargs):
        # The imported model's reusable integer/cache oracle has one leaf.
        # Select this scope's explicit ABI and no persistent BSS outputs.
        previous = model.WRAPPER, model.LEAF, model.private_outputs
        model.WRAPPER, model.LEAF, model.private_outputs = WRAPPER, LEAF, lambda _: {}
        try:
            super().__init__(*args, **kwargs)
        finally:
            model.WRAPPER, model.LEAF, model.private_outputs = previous
        source, size, load, output = self.args
        self.cached = (self.cached and output <= END - 16 and
                       (output + 16 <= source or output >= source + size))

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
        # An ABI adversary models a full result or a partial refusal. Invalid
        # argument cases test forwarding/cache policy, not safe dereference.
        if not self.args[3] & 3:
            for offset in range(0, 16, 4):
                if self.result != 1 and offset & 4:
                    continue
                self.leaf_write(self.args[3] + offset, 0x92345678 + offset + self.detail)
        for index in range(8):
            self.r[index] = (0x913579bd + index * 0x13579bd) & MASK
        self.r[0] = self.result
        self.mach, self.macl = 0xbad0c0de, 0xc001d00d
        self.sr ^= 1
        self.pr ^= 0x2468ace0
        self.pc = return_pc


def source_caller(stage, body=None):
    begin, linked, _ = symbol_bytes(stage, '_standalone_source_check', 2)
    body = linked if body is None else body
    require(len(body) >= 24, 'Truncated fresh SDK check')
    words = struct.unpack_from('<12H', body)
    require(words[:2] == (0x2f86, 0x4f22) and words[2] >> 8 == 0xd0 and
            words[3] == 0x7ff0 and words[4] >> 8 == 0xd6 and
            words[5] == 0x67f3 and words[6] >> 8 == 0xd1 and
            words[7:11] == (0x6463, 0x400b, 0x6512, 0x8801),
            'Fresh SDK check lost its actual four-argument call/result guard')
    def literal(at):
        op = struct.unpack_from('<H', body, at)[0]
        slot = ((begin + at + 4) & ~3) + 4 * (op & 255) - begin
        return struct.unpack_from('<I', body, slot)[0]
    require(literal(4) == stage[4][WRAPPER][0] and literal(8) == LOAD and
            literal(12) == stage[4]['_exec_bytes'][0],
            'Fresh SDK check no longer binds the complete current executable')
    return dict(function=hex(begin), actual_call=hex(begin + 16),
                image_and_load=hex(LOAD), output_bytes=16, current_exec_bytes=True)


def graph_audit(path, stage):
    directory = Path(path).parent
    reports = sorted((directory / 'stage').rglob('*.ci'))
    require(reports, 'Missing emitted standalone stage callgraphs')
    frames, calls = load_graph(reports, qualified=True)
    depth, deepest, reached = model.closed_depth(frames, calls, LEAF[1:])
    require(depth + 12 + 64 < 65536, 'SDK proof exceeds its64KiB stack headroom')
    checks = [n for n in frames if n.rsplit(':', 1)[-1] == 'standalone_source_check']
    require(len(checks) == 1, 'Missing unique emitted fresh SDK check')
    check = checks[0]
    require([callee for callee, _ in calls.get(check, [])].count(WRAPPER[1:]) == 1,
            'Fresh source check bypasses its actual cache scope')
    for caller in ('kui_retail_stage_main', 'kui_retail_stage_relay'):
        require([callee for callee, _ in calls.get(caller, [])].count(check) == 1,
                'Both initial and post-relay fresh SDK checks remain required')
    required = frames['kui_retail_stage_relay'] + frames[check] + 12 + depth + 64 + 32
    image = Image(stage[0]);resume = image.symbol('_kui_retail_game_resume')
    first, stack_load = struct.unpack('<2H', image.read(resume, 4))
    require(first == 0x68f3 and stack_load >> 8 == 0xdf, 'Relay lost its high stack')
    slot = ((resume + 6) & ~3) + 4 * (stack_load & 255)
    stack = struct.unpack('<I', image.read(slot, 4))[0]
    require(stack == STACK and stage[4]['__retail_stage_bss_end'][0] + required <= stack,
            'SDK proof stack overlaps the linked stage')
    fingerprints = {str(p.relative_to(directory)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in reports}
    return dict(actual_closed_leaf_stack_bytes=depth, full_relay_required_bytes=required,
                deepest=deepest, closed_functions=sorted(reached), callgraph_sha256=fingerprints,
                both_fresh_checks=True, no_indirect_or_live_callbacks=True)


def audit(path, old=None):
    require(__debug__, 'Run linked models without Python optimization')
    stage = read_elf(path)
    require(stage[4].get(MARKER) == (1, 0, 0, 0xfff1), 'Missing standalone SDK cache scope')
    begin, body, _ = symbol_bytes(stage, WRAPPER, 2)
    expected = struct.pack('<112H12I', *OPS, 0x400000f0, model.MMUCR, model.CCR,
                          0x100, RAM, END, LOAD, RAM + 65536,
                          begin + 144 + 0x20000000, 0x909, stage[4][LEAF][0], 0x908)
    require(body == expected, 'SDK guard/ABI/publication instructions differ')
    selected = source_caller(stage)
    graphs = graph_audit(path, stage)
    configs = model.configurations()
    configs += [dict(output=p) for p in (LOAD - 16, LOAD - 12, LOAD - 4,
                END - 16, END - 12, END - 4)]
    cases = 0
    for seed in (0, 7, 97):
        for config in configs:
            for result in (0, 1, MASK):
                Scope(stage, seed=seed, result=result, **config).run().check()
                cases += 1
    mutations = []
    # CPU, MMU, CCR, image/output/stack bounds and publication are independent
    # controls. Retargeting the leaf to P2 is also rejected by the call oracle.
    for at in (8, 16, 24, 34, 40, 84, 104, 116, 140, 182, 194):
        changed = bytearray(body);struct.pack_into('<H', changed, at, 9)
        mutations.append(bytes(changed))
    for needle, value in ((0x909, 0x908), (0x908, 0x909),
                          (stage[4][LEAF][0], stage[4][LEAF][0] + 0x20000000)):
        changed = bytearray(body);at = body.index(struct.pack('<I', needle))
        struct.pack_into('<I', changed, at, value);mutations.append(bytes(changed))
    for mutation_index, changed in enumerate(mutations):
        try:
            for config in configs:
                Scope(stage, body=changed, **config).run().check()
        except (AssertionError, KeyError, struct.error):
            continue
        raise ValueError('SDK model accepted unsafe cache mutation ' + str(mutation_index))
    _, caller, _ = symbol_bytes(stage, '_standalone_source_check', 2)
    for at in (4, 8, 12, 16, 18, 20):
        changed = bytearray(caller);struct.pack_into('<H', changed, at, 9)
        try:
            source_caller(stage, changed)
        except (ValueError, struct.error):
            continue
        raise ValueError('SDK caller accepted a broken source/size/result binding')
    old_result = None
    if old:
        old_stage = read_elf(old)
        require(MARKER not in old_stage[4] and WRAPPER not in old_stage[4],
                'Old control already has this cache scope')
        _, old_check, _ = symbol_bytes(old_stage, '_standalone_source_check', 2)
        require(struct.pack('<I', old_stage[4][LEAF][0]) in old_check,
                'Old control lacks its original direct fresh SDK finder')
        old_result = dict(elf_sha256=hashlib.sha256(old_stage[0]).hexdigest(),
                          missing_scope_refused=True, original_fresh_finder_present=True)
    require(Path(path).read_bytes() == stage[0], 'Stage changed during qualification')
    return dict(status='PASS', schema='kui.native-standalone.sdk-cache.v1',
                stage_sha256=hashlib.sha256(stage[0]).hexdigest(), wrapper_cases=cases,
                unsafe_cache_mutations_rejected=len(mutations), caller_mutations_rejected=6,
                actual_caller=selected, actual_graph=graphs, old_control=old_result,
                exact_owner_ccr_and_abi_restored=True, full_private_ram_publication=True,
                output_bound_bytes=16, independent_semantic_tests_required=True,
                console_time_or_hardware_acceptance=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', type=Path)
    parser.add_argument('--old-stage', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = audit(args.stage, args.old_stage)
    wire = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True);args.report.write_text(wire)
    print(wire)


if __name__ == '__main__':
    main()
