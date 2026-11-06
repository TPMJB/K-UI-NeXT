#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked immutable-CRC cache scope and independently check its CRC.

The exact CRC leaf is separately executed against zlib. The wrapper uses an
ABI-conforming adversary so its result and every volatile integer register may
change. Only the pinned four-instruction private-stack OCBP loop is folded; no cache
timing, hardware latency or console loading-time improvement is modeled.
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
from check_vmu_native_cache_gateway import CacheGateway
from test_smash_native_fast_crc import audit as crc_audit

WRAPPER = '_kui_native_stage_cached_crc32'
LEAF = '_kui_retail_stage_crc32_leaf'
CCR, MMUCR = 0xff00001c, 0xff000010
RAM, END, STACK = 0x8c000000, 0x8d000000, 0x8cff0000
PURGE_OPS = struct.pack('<4H', 0x00a3, 0x7020, 0x3012, 0x8bfb)
OPS = (
    0x2f86, 0x2f96, 0x4f22, 0xe800, 0x0002, 0xd129, 0x2019, 0x3010, 0x8b2d,
    0xd128, 0x6012, 0xc801, 0x8b29, 0xd127, 0x6012, 0xd227, 0x3020, 0x8b24,
    0xd026, 0x3502, 0x8b21, 0xd126, 0x3512, 0x891e, 0x6213, 0x3258, 0x3262,
    0x8b1a, 0x2668, 0x8918, 0x707f, 0x7001, 0x3f02, 0x8b14, 0x60f3, 0xc803,
    0x8b11, 0x700c, 0x3016, 0x890e, 0xe801, 0xd01d, 0x402b, 0x0009, 0xd117,
    0xd01c, 0x2102, *([9] * 8), 0xd018, 0x400b, 9, 0x6903, 0x2888, 0x8914,
    0x60f3, 0x7080, 0xe2e0, 0x2029, 0x61f3, 0x710c,
    0x00a3, 0x7020, 0x3012, 0x8bfb, 0xd10a, 0xd010, 0x2102,
    *([9] * 8), 0x6093, 0x4f26, 0x69f6, 0x68f6, 0x000b, 9)


def require(value, message):
    if not value:
        raise ValueError(message)


class Scope(CacheGateway):
    def __init__(self, stage, *, seed=7, mask=15, md=True, ccr=0x100, mmu=0,
                 source=0x8c010000, size=1848288, stack=STACK, result=0x92345678, body=None):
        begin, linked, _ = symbol_bytes(stage, WRAPPER, 2)
        CPU.__init__(self, seed, begin, 1)
        self.blob = linked if body is None else body
        self.pc, self.r[15], self.mem = begin, stack, {}
        self.start_sp = stack
        self.sr = (self.sr & ~0x400000f0) | (int(md) << 30) | (mask << 4)
        self.r[4:7] = [seed * 0x1234567 & MASK, source, size]
        self.args = self.r[4:7].copy()
        self.pr = begin + len(self.blob) + 0x100
        self.before = (self.r.copy(), self.pr, self.sr, self.fpscr, self.fr.copy(), self.gbr)
        self.owner_ccr = self.ccr = ccr
        self.mmu, self.result = mmu, result
        self.cached = (md and mask == 15 and ccr == 0x100 and not mmu & 1 and
                       RAM <= source < END and 0 < size <= END - source and
                       RAM + 140 <= stack <= END and not stack & 3)
        self.targets = {stage[4][LEAF][0]: 'crc'}
        self.calls, self.ccr_writes, self.events, self.controls = [], [], [], []
        self.wait, self.purged, self.purge_lines, self.dirty = 0, False, 0, set()

    def code(self, address, size):
        offset = (address - self.base) & MASK
        if offset >= 0x20000000:
            offset -= 0x20000000
        assert 0 <= offset < offset + size <= len(self.blob), 'scope left exact linked body'
        return self.blob[offset:offset + size]

    def read(self, address):
        if address in (CCR, MMUCR):
            assert self.sr & 0x400000f0 == 0x400000f0, 'controls read before CPU admission'
            self.controls.append(address)
            return self.ccr if address == CCR else self.mmu
        if address in self.mem:
            assert self.ccr & 1 or address not in self.dirty, 'private stack not published'
            return self.mem[address]
        return struct.unpack('<I', self.code(address, 4))[0]

    def write(self, address, value):
        if address == CCR:
            assert self.base + 0x20000000 <= self.pc < self.base + 0x20000000 + len(self.blob)
            assert self.cached and not self.wait and len(self.ccr_writes) < 2
            expected = 0x908 if self.ccr_writes else 0x909
            assert value == expected, 'unexpected scope cache profile'
            if self.ccr_writes:
                assert self.purged and not self.dirty, 'cache disabled before stack publication'
            self.ccr_writes.append(value)
            self.events.append(('ccr', value))
            self.ccr, self.wait = value & ~0x808, 8
            return
        assert not address & 3 and self.start_sp - 140 <= address < self.start_sp
        self.mem[address] = value & MASK
        if self.ccr & 1:
            self.dirty.add(address)

    def plain(self, word, pc):
        if self.wait:
            assert word == 9, 'cache write lacks eight following NOPs'
            self.wait -= 1
        assert word >> 12 != 0xf, 'scope introduced FPU instructions'
        if word == 0x0002:  # STC SR,R0, no SR write or bank change.
            self.r[0] = self.sr
            return
        if word & 0xf00f == 0x3008:  # SUB Rm,Rn.
            n, m = word >> 8 & 15, word >> 4 & 15
            self.r[n] = (self.r[n] - self.r[m]) & MASK
            return
        if word == 0x00a3:
            assert self.code(pc, 8) == PURGE_OPS, 'private-stack publication shape changed'
            first = (self.start_sp - 140) & ~31
            assert self.r[:2] == [first, self.start_sp] and self.calls == ['crc']
            assert self.cached and self.ccr_writes == [0x909] and not self.purged
            self.purge_lines = ((self.start_sp + 31) & ~31) - first
            self.purge_lines //= 32
            assert RAM <= first < first + self.purge_lines * 32 <= END
            assert all(first <= address < first + self.purge_lines * 32 for address in self.dirty)
            self.r[0] = first + (self.purge_lines - 1) * 32
            self.purged = True
            self.dirty.clear()
            self.events.append(('publish', self.purge_lines))
            return
        super().plain(word, pc)

    def api(self, target):
        assert target in self.targets and target & 0xff000000 == RAM
        assert not self.wait and not self.calls and self.r[4:7] == self.args
        assert self.r[15] == self.start_sp - 12
        assert self.ccr == (0x101 if self.cached else self.owner_ccr)
        assert self.ccr_writes == ([0x909] if self.cached else [])
        self.calls.append('crc')
        self.events.append(('crc', self.result))
        return_pc = self.pr
        # Larger private frame than the actual 8-byte leaf: every byte still
        # lies within the scope's admitted 128-byte stack headroom.
        for i in range(1, 33):
            self.write(self.r[15] - i * 4, (0x13579bdf + i * 7) & MASK)
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
        assert self.calls == ['crc'] and self.ccr == self.owner_ccr and not self.wait
        assert self.ccr_writes == ([0x909, 0x908] if self.cached else [])
        lines = (((self.start_sp + 31) & ~31) - ((self.start_sp - 140) & ~31)) // 32
        assert self.purged == self.cached and self.purge_lines == (lines if self.cached else 0)
        assert self.events == ([('ccr', 0x909), ('crc', self.result), ('publish', lines),
                                ('ccr', 0x908)] if self.cached else [('crc', self.result)])
        assert self.controls == ([MMUCR, CCR] if sr & 0x400000f0 == 0x400000f0 and not self.mmu & 1
                                 else [MMUCR] if sr & 0x400000f0 == 0x400000f0 else [])


def audit(path):
    require(__debug__, 'Run the linked scope model without Python optimization')
    stage = read_elf(path)
    require(stage[4].get('__kui_native_cached_boot_crc') == (1, 0, 0, 0xfff1), 'CRC scope marker differs')
    begin, body, _ = symbol_bytes(stage, WRAPPER, 2)
    leaf, leaf_body, _ = symbol_bytes(stage, LEAF, 2)
    crc = stage[4]['_kui_stage_crc32'][0]
    expected = struct.pack('<88H10I', *OPS, 0x400000f0, MMUCR, CCR, 0x100, RAM, END,
                           begin + 88 + 0x20000000, 0x909, leaf, 0x908)
    require(body == expected, 'Actual CRC scope instructions/literals differ')
    require(leaf_body == struct.pack('<4HI', 0xd001, 0x402b, 9, 9, crc),
            'Pure leaf is not the exact no-stack tail call to the separately tested CRC')
    _, crc_body, _ = symbol_bytes(stage, '_kui_stage_crc32', 2)
    # This complete 140-byte instruction body has only R8/R9 pushes/pops,
    # byte reads, table reads and local branches. Its last four bytes are
    # solely the relocated table pointer; the CRC audit binds that table.
    pure_body_sha = '294108d4ffa7b693a386472b10378c6f10a87c6dc0cb52cf9480c7c6929873d9'
    require(len(crc_body) == 144 and hashlib.sha256(crc_body[:-4]).hexdigest() == pure_body_sha,
            'CRC instruction body changed: reprove its 8-byte private write set and no external calls')
    instructions = struct.unpack('<70H', crc_body[:-4])
    require([i * 2 for i, word in enumerate(instructions) if word & 0xf00f == 0x2006] == [4, 14] and
            [instructions[i] for i in (2, 7)] == [0x2f86, 0x2f96] and
            all(word >> 12 != 0xb and word & 0xf0ff not in (0x400b, 0x402b)
                for word in instructions),
            'CRC must retain only its two nonvolatile-register pushes and no outgoing calls')
    _, relay, _ = symbol_bytes(stage, '_kui_native_heap_base_relay', 2)
    require(struct.pack('<I', begin) in relay, 'Base relay does not bind the scope')
    cases = 0
    configs = [dict(mask=mask) for mask in range(16)]
    configs += [dict(ccr=ccr) for ccr in (0, 1, 4, 0x20, 0x80, 0x105, 0x109, 0x909, MASK)]
    configs += [dict(mmu=mmu) for mmu in (1, 0x201, MASK, 0x200, 0x400)]
    configs += [dict(md=False)]
    configs += [dict(source=source, size=size) for source, size in (
        (RAM, 1), (RAM + 1, 4097), (END - 1, 1), (END - 1, 2), (END, 1),
        (RAM - 1, 1), (0xac010000, 4096), (0x0c010000, 4096), (0, 1),
        (0xffffffff, 2), (RAM, 0), (RAM, MASK), (RAM, END - RAM), (RAM, END - RAM + 1))]
    configs += [dict(stack=stack) for stack in (END, END + 4, STACK + 28, RAM + 140, RAM + 136,
                                               RAM + 12, 0xacff0000, 0x0cff0000)]
    for seed in (0, 7, 97):
        for config in configs:
            for result in (0, 1, 0x92345678, MASK):
                Scope(stage, seed=seed, result=result, **config).run().check()
                cases += 1
    mutations = []
    for at, replacement in ((8, 9), (16, 9), (24, 9), (34, 9), (78, 9),
                            (84, 9), (134, 9), (146, 9)):
        changed = bytearray(body)
        struct.pack_into('<H', changed, at, replacement)
        mutations.append(bytes(changed))
    for needle, replacement in ((struct.pack('<I', 0x909), struct.pack('<I', 0x908)),
                                (struct.pack('<I', 0x908), struct.pack('<I', 0x909)),
                                (struct.pack('<I', leaf), struct.pack('<I', leaf + 0x20000000))):
        changed = bytearray(body)
        at = body.find(needle)
        require(at >= 0, 'Missing mutation target')
        changed[at:at + 4] = replacement
        mutations.append(bytes(changed))
    rejected = 0
    for changed in mutations:
        try:
            for config in configs:
                Scope(stage, body=changed, **config).run().check()
        except (AssertionError, KeyError, struct.error):
            rejected += 1
        else:
            raise ValueError('Scope model accepted an unsafe mutation')
    leaf_rejections = 0
    for i in range(len(crc_body) - 4):
        changed = bytearray(crc_body)
        changed[i] ^= 1
        require(hashlib.sha256(changed[:-4]).hexdigest() != pure_body_sha,
                'Pure CRC write-set binding accepted an instruction mutation')
        leaf_rejections += 1
    for i in range(len(leaf_body)):
        changed = bytearray(leaf_body)
        changed[i] ^= 1
        require(changed != struct.pack('<4HI', 0xd001, 0x402b, 9, 9, crc),
                'Pure leaf binding accepted a tail-call mutation')
        leaf_rejections += 1
    actual_crc = crc_audit(path, relay=None, stack_limit=8)
    require(Path(path).read_bytes() == stage[0], 'Stage changed during scope qualification')
    return dict(schema='kui.native-stage.cached-crc.v1', status='PASS',
                elf_sha256=hashlib.sha256(stage[0]).hexdigest(), wrapper=hex(begin), wrapper_bytes=len(body),
                actual_SH_wrapper_cases=cases, unsafe_mutation_rejections=rejected,
                actual_SH_crc=actual_crc, source_bytes_unchanged=True,
                pure_leaf_instruction_mutation_rejections=leaf_rejections,
                bound_leaf_outgoing_calls=0, bound_leaf_stack_store_offsets=[4, 14],
                fallback_arguments_and_result_unchanged=True, published_private_stack_lines=[5, 6],
                private_stack_headroom=128, bound_leaf_stack_bytes=8, ccr_write_nops=[8, 8],
                worker_or_low_resident_changes=False, console_timing_verified=False,
                limits=['The wrapper leaf is an ABI adversary; the exact bound CRC instructions are separately executed against zlib.',
                        'The exact private-stack publication loop is folded after checking all four instructions, its complete address span, and every dirty write.',
                        'This privileged cold-stage scope does not establish userspace invocation, hardware cache timing or console speed.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True, type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = audit(args.elf)
    text = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text)
    print(text, end='')
