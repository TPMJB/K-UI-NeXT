#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the common seven-argument immutable replay cache scope.

The real replay leaf is an explicit ABI adversary. It checks all seven
forwarded arguments, writes private output/stack, and returns both bools.
The existing extent scope and all ownership checks retain separate models.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from check_vmu_native_worker_gateway import CPU, MASK, read_elf, symbol_bytes
from test_smash_native_cached_replay import ReplayGateway, CCR, RAM, END, STACK, PURGE_OPS

WRAPPER = '_kui_vmu_native_cached_resolution_replay'
HELPER = '_kui_vmu_native_resolution_cache_replay'
MARKER = '__kui_native_cached_resolution'


def require(value, message):
    if not value:
        raise ValueError(message)


class Gateway(ReplayGateway):
    def __init__(self, stage, seed, mask, ccr, result, body=None):
        begin, linked, _ = symbol_bytes(stage, WRAPPER, 2)
        CPU.__init__(self, seed, begin, 1)
        self.blob = linked if body is None else body
        self.pc, self.r[15], self.mem = begin, STACK, {}
        self.start_sp = STACK
        self.sr = (self.sr & ~0xf0) | mask << 4
        self.pr = begin + len(self.blob) + 0x100
        self.r[4:8] = [0x8c010000, 0x200000 + seed, 0x8c010000, 0x8ce08040]
        self.before = (self.r.copy(), self.pr, self.sr, self.fpscr, self.fr.copy(), self.gbr)
        self.owner_ccr, self.ccr, self.result = ccr, ccr, result
        self.calls, self.ccr_writes, self.events = [], [], []
        self.wait, self.purged, self.purge_lines, self.dirty = 0, False, 0, set()
        self.targets = {stage[4][HELPER][0]: 'proof'}
        self.output, self.output_bytes, _, _ = stage[4]['_native_cached_resolution']
        self.args = [1024, 0x8ce08018, self.output]
        self.mem.update({STACK + 4 * i: value for i, value in enumerate(self.args)})
        self.expected_output = {}
        for offset in range(0, self.output_bytes, 4):
            self.mem[self.output + offset] = (0xa5a50000 + offset + seed) & MASK

    def write(self, address, value):
        if address == CCR:
            return super().write(address, value)
        assert not address & 3 and self.start_sp - 24 <= address < self.start_sp
        self.mem[address] = value & MASK
        if self.ccr & 1:
            self.dirty.add(address)

    def api(self, target):
        assert target in self.targets and not self.wait and not self.calls
        assert target & 0xff000000 == 0x8c000000
        assert self.r[15] == self.start_sp - 24
        assert self.r[4:8] == self.before[0][4:8], 'register replay arguments changed'
        assert [self.read(self.r[15] + 4 * i) for i in range(3)] == self.args, 'stack arguments not forwarded'
        assert self.sr & 0xf0 == self.before[2] & 0xf0
        assert self.ccr == (0x101 if self.owner_ccr == 0x100 else self.owner_ccr)
        assert self.ccr_writes == ([0x909] if self.owner_ccr == 0x100 else [])
        self.calls.append('proof')
        self.events.append(('proof', self.result))
        return_pc = self.pr
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
        super().check()
        assert [self.read(STACK + 4 * i) for i in range(3)] == self.args, 'caller argument area changed'


def audit(elf, self_test=True):
    require(__debug__, 'Run linked models without Python optimization')
    stage = read_elf(elf)
    require(stage[4].get(MARKER) == (1, 0, 0, 0xfff1), 'Missing common replay cache scope')
    begin, body, _ = symbol_bytes(stage, WRAPPER, 2)
    helper, helper_body, _ = symbol_bytes(stage, HELPER, 2)
    # Exactly the authored24-byte ABI frame and original cache scope, including
    # all three outgoing stack arguments. No generic memory-copy call exists.
    ops = (0x2f86, 0x2f96, 0x4f22, 0x7ff4, 0x50f6, 0x2f02, 0x50f7, 0x1f01,
           0x50f8, 0x1f02, 0xd019, 0x402b, 9, 0xd119, 0x6012, 0x6803, 0xd218,
           0x3020, 0x8b09, 0xd018, 0x2102, 9, 9, 9, 9, 9, 9, 9, 9,
           0xd014, 0x400b, 9, 0x6903, 0xd010, 0x3800, 0x8b11, 0xd011, 0xd112,
           0x00a3, 0x7020, 0x3012, 0x8bfb, 0xd010, 0x208b, 0xd109, 0x2102,
           9, 9, 9, 9, 9, 9, 9, 9, 0x6093, 0x7f0c, 0x4f26, 0x69f6, 0x68f6, 0xb, 9, 9)
    expected = struct.pack('<62H8I', *ops, begin + 26 + 0x20000000, CCR, 0x100,
                           0x909, helper, RAM, END, 0x808)
    require(body == expected, 'Common replay scope instruction/argument/publication contract differs')
    output, count, kind, owner = stage[4]['_native_cached_resolution']
    section = stage[3][owner]
    require(kind == 1 and count == 688 and section[1] == 8 and section[2] & 7 == 3 and
            RAM <= section[3] <= output < output + count <= section[3] + section[5] <= END,
            'Replay output is not private high-stage BSS')
    cases = 0
    for seed in range(12):
        for mask in range(16):
            for ccr in (0, 1, 4, 0x20, 0x80, 0x100, 0x105, 0x109, 0x909, 0xffffffff):
                for result in (0, 1):
                    Gateway(stage, seed, mask, ccr, result).run().check()
                    cases += 1
    mutants = []
    for at, value in ((6, 0x7ff8), (8, 0x50f5), (12, 0x50f6), (16, 0x50f7),
                      (110, 0x7f08), (76, 9)):
        changed = bytearray(body)
        struct.pack_into('<H', changed, at, value)
        mutants.append(bytes(changed))
    for literal, replacement in ((0x909, 0x908), (0x808, 0x809), (0x100, 0x105),
                                  (helper, helper + 0x20000000)):
        changed = bytearray(body)
        at = changed.index(struct.pack('<I', literal))
        struct.pack_into('<I', changed, at, replacement)
        mutants.append(bytes(changed))
    for at in (*range(42, 58, 2), *range(92, 108, 2)):
        changed = bytearray(body)
        struct.pack_into('<H', changed, at, 0x6003)
        mutants.append(bytes(changed))
    rejected = 0
    if self_test:
        for changed in mutants:
            try:
                Gateway(stage, 7, 15, 0x100, 1, changed).run().check()
            except (AssertionError, struct.error, KeyError):
                rejected += 1
            else:
                raise ValueError('Common cache scope accepted an unsafe argument/cache/publication mutant')
    require(Path(elf).read_bytes() == stage[0], 'Stage changed during scope audit')
    return dict(schema='kui.native-stage.proof-cache-scope.v1', status='PASS',
        elf_sha256=hashlib.sha256(stage[0]).hexdigest(), wrapper_bytes=len(body),
        pure_leaf_sha256=hashlib.sha256(helper_body).hexdigest(),
        actual_SH_cases=cases, mutation_rejections=rejected, argument_count=7,
        caller_stack_unchanged=True, private_frame_bytes=24,
        unknown_CCR_unchanged=True, per_game_constants=False,
        limits='Actual scope instructions execute; the real immutable replay leaf is an ABI adversary. No console timing is established.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = audit(args.elf)
    rendered = json.dumps(report, indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end='')
