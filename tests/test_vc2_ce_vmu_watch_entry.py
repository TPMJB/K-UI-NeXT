#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the native-linked terminal UBC capture entry with hostile owner state.

This models integer instruction semantics and already-entered exception state;
it does not model UBC trigger timing or assert physical console acceptance.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

from test_armada_ce_maple_hook import CPU as BaseCPU, MASK, ROOT
from sh4_delay_slots import validate_delay_slot
from check_vmu_native_worker_gateway import read_elf, symbol_bytes

ENTRY, MAP, STACK, STOP = 0xac006000, 0x8c007b48, 0xac008000, 0xac004100
SNAPSHOT, MAP_BYTES = MAP + 0x20000000, 828
BBRA, BBRB, BRCR = 0xff200008, 0xff200014, 0xff200020
MMUCR, CCR, EXPEVT = 0xff000010, 0xff00001c, 0xff000024
RESIDENT_IMAGE = None
RESIDENT_PATH = None


class WatchCPU(BaseCPU):
    def __init__(self, code, seed, owner_bank, bad_sp, flags):
        super().__init__(seed, ENTRY, 1)
        rng = random.Random(seed ^ 0x87654321)
        owner = [rng.getrandbits(32) for _ in range(15)]
        self.owner_low = owner[:8]
        inactive = [rng.getrandbits(32) for _ in range(8)]
        self.r = (owner[:8] if owner_bank else inactive) + owner[8:] + [bad_sp]
        self.other = inactive if owner_bank else owner[:8]
        self.control = dict(sgr=bad_sp, spc=rng.getrandbits(32) & ~1,
                            ssr=(rng.getrandbits(32) & ~0x20000000) | (owner_bank << 29),
                            dbr=ENTRY)
        self.sr = self.control['ssr'] | 0x70000000
        original_dbr = rng.getrandbits(32)
        self.expected = (self.r[:15] + [bad_sp] + self.other +
                         [self.control['spc'], self.control['ssr'], self.pr,
                          self.gbr, self.mach, self.macl, 0x1e0, flags,
                          rng.getrandbits(32), rng.getrandbits(32), ENTRY, original_dbr])
        self.mem = {BBRA: 0x28, BBRB: 0x28, BRCR: flags,
                    EXPEVT: 0x1e0, CCR: self.expected[32], MMUCR: self.expected[33]}
        # Dirty P1 aliases deliberately disagree with physical P2 contents.
        # Capture must use that canonical alias, then publish every map line.
        for address in range(MAP & ~31, (MAP + MAP_BYTES + 31) & ~31, 4):
            self.mem[address] = rng.getrandbits(32)
            self.mem[address + 0x20000000] = rng.getrandbits(32)
        self.mem[MAP + 64] = original_dbr
        self.purges = []
        self.blob, self.pc = code, ENTRY
        self.reads, self.writes, self.stores = [], [], []
        self.steps = self.wait_nops = 0
        self.last_disable = None
        self.initial_specials = (self.pr, self.gbr, self.mach, self.macl,
                                 self.sr, self.fpscr, self.fr.copy(), self.control.copy())

    def code(self, address, size):
        assert ENTRY <= address and address + size <= ENTRY + len(self.blob)
        offset = address - ENTRY
        return self.blob[offset:offset + size]

    def read(self, address):
        self.reads.append(address)
        if address in self.mem:
            if address == BRCR:
                assert self.last_disable is not None
                assert self.wait_nops >= 11
            return self.mem[address]
        return struct.unpack('<I', self.code(address, 4))[0]

    def write(self, address, value):
        assert address % 4 == 0
        assert MAP <= address < MAP + 36 * 4, hex(address)
        assert not self.purges, 'Snapshot changed after publication began'
        self.writes.append(address)
        self.stores.append((address, value & MASK, 4))
        self.mem[address] = value & MASK

    def plain(self, word, pc):
        n, m = (word >> 8) & 15, (word >> 4) & 15
        if word & 0xf0ff in (0x400e, 0x4007, 0x402e, 0x4027):
            raise AssertionError('Entry must not write SR or VBR')
        if word & 0xf00f == 0x2001:  # MOV.W Rm,@Rn: only the two UBC channels.
            address = self.r[n]
            assert address in (BBRA, BBRB) and self.r[m] & 0xffff == 0
            assert [store[0] for store in self.stores if store[2] == 4] == [MAP, MAP + 4]
            self.mem[address] = 0
            self.stores.append((address, 0, 2))
            self.last_disable, self.wait_nops = self.steps, 0
            return
        if word & 0xf00f == 0x6001:  # MOV.W @Rm,Rn
            value = self.read(self.r[m]) & 0xffff
            self.r[n] = (value - 0x10000 if value & 0x8000 else value) & MASK
            return
        if word & 0xf00f == 0x600d:  # EXTU.W Rm,Rn
            self.r[n] = self.r[m] & 0xffff
            return
        if word & 0xf00f == 0x2009:  # AND Rm,Rn
            self.r[n] &= self.r[m]
            return
        if word & 0xf0ff == 0x00a3:  # OCBP @Rn
            address = self.r[n]
            assert address == (MAP & ~31) + 32 * len(self.purges)
            assert address < MAP + MAP_BYTES
            self.purges.append(address)
            for word_address in range(address, address + 32, 4):
                self.mem[word_address + 0x20000000] = self.mem[word_address]
            return
        if word & 0xf08f == 0x0082:  # STC Rm_BANK,Rn, inactive bank under RB1.
            assert self.sr & 0x20000000
            self.r[n] = self.other[(word >> 4) & 7]
            return
        controls = {0x003a: 'sgr', 0x0042: 'spc', 0x0032: 'ssr', 0x00fa: 'dbr'}
        operation = word & 0xf0ff
        if operation in controls:
            self.r[n] = self.control[controls[operation]]
            return
        special = {0x002a: 'pr', 0x0012: 'gbr', 0x000a: 'mach', 0x001a: 'macl'}
        if operation in special:
            self.r[n] = getattr(self, special[operation])
            return
        if word == 9 and self.last_disable is not None:
            self.wait_nops += 1
        super().plain(word, pc)

    def run(self):
        while self.pc != STOP:
            self.steps += 1
            assert self.steps < 300
            pc = self.pc
            word = struct.unpack('<H', self.code(pc, 2))[0]
            if word & 0xf0ff == 0x402b:  # JMP @Rn; exactly one terminal transfer.
                target = self.r[(word >> 8) & 15]
                assert target == STOP
                slot = struct.unpack('<H', self.code(pc + 2, 2))[0]
                validate_delay_slot(slot, pc)
                self.plain(slot, pc + 2)
                self.pc = target
            elif word >> 8 == 0x8b:  # BF publication loop, no delay slot.
                displacement = word & 255
                displacement -= 256 if displacement & 128 else 0
                self.pc = pc + 2 if self.sr & 1 else pc + 4 + displacement * 2
            else:
                self.plain(word, pc)
                self.pc = pc + 2
        return self


class WatchEntryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('KUI_SH_GCC') or shutil.which('sh-elf-gcc')
        if not compiler:
            candidate = ROOT.parent / 'K-UI-NeXT/.deps/sh-elf/bin/sh-elf-gcc'
            compiler = str(candidate) if candidate.exists() else None
        if not compiler:
            if RESIDENT_IMAGE is not None:
                raise RuntimeError('Native SH toolchain required for resident entry proof')
            raise unittest.SkipTest('Native SH toolchain unavailable')
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        elf, binary = directory / 'watch.elf', directory / 'watch.bin'
        subprocess.run([compiler, '-m4-single', '-ml', '-mrelax', '-ffreestanding', '-nostdlib',
                        '-Wl,--build-id=none', '-Wl,--relax', '-Wl,-Ttext=' + hex(ENTRY),
                        '-Wl,-e,_kui_vc2_ce_vmu_watch_entry',
                        '-Wl,--defsym,_manifest=' + hex(MAP),
                        '-Wl,--defsym,__retail_hook_stack=' + hex(STACK - 0x20000000),
                        '-Wl,--defsym,_kui_vc2_ce_vmu_watch_stop=' + hex(STOP - 0x20000000),
                        str(ROOT / 'src/loader/vc2_ce_vmu_watch_entry.S'), '-o', str(elf)],
                       check=True, capture_output=True)
        objcopy = str(Path(compiler).with_name('sh-elf-objcopy'))
        subprocess.run([objcopy, '-O', 'binary', '--only-section=.text',
                        str(elf), str(binary)], check=True, capture_output=True)
        cls.code = binary.read_bytes()
        if RESIDENT_IMAGE is not None:
            _, actual, _ = symbol_bytes(RESIDENT_IMAGE, '_kui_vc2_ce_vmu_watch_entry', 2)
            if cls.code != actual:
                raise AssertionError('Resident watch entry differs from complete authored native entry')
            cls.code = actual
        cls.cases_executed = 0

    def test_entry_layout(self):
        self.assertLessEqual(len(self.code), 280)
        self.assertEqual(struct.unpack_from('<H', self.code)[0], 0x6f03)
        for pointer in (MAP, MAP + MAP_BYTES, STACK, STOP):
            self.assertEqual(self.code.count(struct.pack('<I', pointer)), 1)

    def test_both_banks_bad_stacks_and_latched_channel_flags(self):
        cases = 0
        for owner_bank in (0, 1):
            for bad_sp in (0, 0xffffffff, 0x02000003, 0x7c000000, 0xacfc4000):
                for flags in (0x8001, 0x4001, 0xc001):
                    for seed in range(12):
                        with self.subTest(bank=owner_bank, sp=hex(bad_sp), flags=hex(flags), seed=seed):
                            cpu = WatchCPU(self.code, seed, owner_bank, bad_sp, flags).run()
                            self.assertEqual([cpu.mem[SNAPSHOT + 4 * i] for i in range(36)], cpu.expected)
                            self.assertEqual(cpu.writes, [MAP + 4 * i for i in
                                             [*range(16), 35, *range(16, 35)]])
                            self.assertEqual([row for row in cpu.stores if row[2] == 2],
                                             [(BBRA, 0, 2), (BBRB, 0, 2)])
                            self.assertEqual((cpu.mem[BBRA], cpu.mem[BBRB], cpu.mem[BRCR]), (0, 0, flags))
                            self.assertEqual(cpu.r[15], STACK)
                            self.assertEqual(cpu.purges, list(range(MAP & ~31, MAP + MAP_BYTES, 32)))
                            for address in range(MAP & ~31, (MAP + MAP_BYTES + 31) & ~31, 4):
                                self.assertEqual(cpu.mem[address], cpu.mem[address + 0x20000000])
                            self.assertEqual((cpu.pr, cpu.gbr, cpu.mach, cpu.macl,
                                              cpu.sr & ~1, cpu.fpscr, cpu.fr, cpu.control),
                                             (*cpu.initial_specials[:4], cpu.initial_specials[4] & ~1,
                                              *cpu.initial_specials[5:]))
                            # The interrupted owner bank is selected by SSR.RB, not exception SR.RB.
                            captured = cpu.expected[:8] if owner_bank else cpu.expected[16:24]
                            self.assertEqual(captured, cpu.owner_low)
                            cases += 1
        self.assertEqual(cases, 360)
        type(self).cases_executed = cases


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, add_help=False)
    parser.add_argument('--resident', type=Path)
    parser.add_argument('--output', type=Path)
    args, remaining = parser.parse_known_args()
    if args.output and not args.resident:
        parser.error('--output requires --resident')
    if args.resident:
        RESIDENT_PATH = args.resident.resolve()
        RESIDENT_IMAGE = read_elf(RESIDENT_PATH)
        symbols = RESIDENT_IMAGE[4]
        ENTRY = symbols['_kui_vc2_ce_vmu_watch_entry'][0] + 0x20000000
        MAP = symbols['_manifest'][0]
        if symbols['_manifest'][1] != MAP_BYTES:
            raise ValueError('Resident manifest must retain its828-byte capture/publication span')
        SNAPSHOT = MAP + 0x20000000
        STACK = symbols['__retail_hook_stack'][0] + 0x20000000
        STOP = symbols['_kui_vc2_ce_vmu_watch_stop'][0] + 0x20000000
    result = unittest.main(argv=[sys.argv[0], *remaining], exit=False).result
    if not result.wasSuccessful():
        sys.exit(1)
    if args.resident:
        if result.skipped or WatchEntryTests.cases_executed != 360:
            raise RuntimeError('Resident entry proof requires all360 modeled executions')
        receipt = dict(schema='kui.vc2.vmu-watch.entry-model.v1', status='PASS',
                       artifacts_sha256={'resident-sci.elf': hashlib.sha256(RESIDENT_IMAGE[0]).hexdigest()},
                       native_entry_bytes=len(WatchEntryTests.code), cases=360,
                       full_entry_bytes_match=True, stack_off_owner=True,
                       no_sr_or_vbr_writes=True, manifest_publication=True,
                       channels_disabled=True, both_register_banks=True,
                       original_dbr_preserved=True, hardware_executed=False,
                       addresses=dict(entry=hex(ENTRY), manifest=hex(MAP),
                                      snapshot=hex(SNAPSHOT), stack=hex(STACK), stop=hex(STOP)),
                       limits=['Executes the complete linked authored entry after UBC exception entry.',
                               'The integer model covers snapshots, channel disable, cache-line publication and terminal handoff.',
                               'Does not model UBC trigger timing, exception acceptance, physical caches or console execution.'])
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(receipt, indent=2) + '\n')
        print(json.dumps(receipt, sort_keys=True))
