#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual VC2 mapped wrapper with adversarial CE/home-area ABIs.

Only the three external call bodies are authored adversaries. Every wrapper
instruction, branch, delay slot, literal and stack save comes from its native
linked binary. No original owner instructions are distributed by this test.
"""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

from test_armada_ce_maple_hook import CPU as BaseCPU, MASK, ROOT
from sh4_delay_slots import validate_delay_slot

BRIDGE_RVA, PATCH_RVA, RESUME_RVA = 0x7c34, 0x723c, 0x7242
SETKMODE_RVA, COMPLETION_RVA = 0x7808, 0x6f64
HELPER, FRAME = 0xacfc8000, 44


class CPU(BaseCPU):
    def __init__(self, seed, base, mode, bridge):
        super().__init__(seed, base, mode)
        self.r[8] = 0x00030a60
        self.pr = base+RESUME_RVA
        self.initial = (self.r.copy(), self.pr, self.mach, self.macl, self.gbr,
                        self.sr, self.fpscr, self.fr.copy())
        self.pc, self.bridge = base+PATCH_RVA, bridge
        self.original_mode = mode
        self.events = []
        self.original_calls = 0

    def code(self, address, size):
        if self.base+PATCH_RVA <= address and address+size <= self.base+RESUME_RVA:
            # Independently authored ISA encoding of NOP; BSR; mov r8,r4.
            data = struct.pack('<IH', 0xb4f90009, 0x6483)
            offset = address-self.base-PATCH_RVA
            return data[offset:offset+size]
        if self.base+BRIDGE_RVA <= address and address+size <= self.base+BRIDGE_RVA+len(self.bridge):
            offset = address-self.base-BRIDGE_RVA
            return self.bridge[offset:offset+size]
        raise AssertionError('Unadmitted VC2 bridge read '+hex(address))

    def write(self, address, value):
        assert address % 4 == 0
        assert self.start_sp-FRAME <= address < self.start_sp
        self.writes.append(address)
        self.mem[address] = value & MASK
        self.stack_min = min(self.stack_min, address)

    def homes(self):
        assert self.r[15] == self.start_sp-FRAME
        # A CE callee owns these four words. Deliberately destroy them on
        # every import and original completion call to catch a missing gap.
        for i in range(4):
            self.write(self.r[15]+4*i, 0xface0000+i+len(self.events))

    def api(self):
        self.homes()
        self.events.append(('mode', int(bool(self.r[4]))))
        super().api()

    def helper(self):
        assert self.mode == 1 and self.sr & 0x40000000
        assert self.r[15] == self.start_sp-FRAME
        phase = self.r[5]
        assert phase in (0, 1)
        assert self.r[4] == self.initial[0][8]
        assert self.r[6] == self.original_mode
        self.events.append(('helper', phase))
        target = self.pr
        for i in range(8):
            self.r[i] = (0xa1020304+i*0x13579bd+phase) & MASK
        self.pr ^= 0x12345678
        self.mach ^= 0x55555555
        self.macl ^= 0xaaaaaaaa
        self.gbr ^= 0x55aa55aa
        self.sr ^= 1
        self.pc = target

    def original(self):
        assert self.mode == self.original_mode
        assert bool(self.sr & 0x40000000) == bool(self.original_mode)
        assert self.r[4] == self.initial[0][8]
        assert self.r[15] == self.start_sp-FRAME
        self.homes()
        self.events.append(('original', self.original_mode))
        self.original_calls += 1
        target = self.pr
        for i in range(8):
            self.r[i] = (0xb2030405+i*0x2468ace) & MASK
        self.pr ^= 0x98765432
        self.mach ^= 0x33333333
        self.macl ^= 0xcccccccc
        self.gbr ^= 0xa55aa55a
        self.sr ^= 1
        self.pc = target

    def run(self):
        while self.pc != self.base+RESUME_RVA:
            self.steps += 1
            assert self.steps < 200
            pc = self.pc
            word = struct.unpack('<H', self.code(pc, 2))[0]
            jsr = word & 0xf0ff == 0x400b
            if word >> 12 in (0xa, 0xb) or jsr or word == 0x000b:
                if word >> 12 in (0xa, 0xb):
                    displacement = word & 4095
                    displacement -= 4096 if displacement & 2048 else 0
                    target = (pc+4+2*displacement) & MASK
                    if word >> 12 == 0xb:
                        self.pr = pc+4
                elif jsr:
                    target = self.r[(word >> 8) & 15]
                    self.pr = pc+4
                else:
                    target = self.pr
                slot = struct.unpack('<H', self.code(pc+2, 2))[0]
                validate_delay_slot(slot, pc)
                self.plain(slot, pc+2)
                if target == self.base+SETKMODE_RVA:
                    self.api()
                elif target == self.base+COMPLETION_RVA:
                    self.original()
                elif target == HELPER:
                    self.helper()
                else:
                    self.pc = target
            else:
                self.plain(word, pc)
                self.pc = pc+2
        return self


class BridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('KUI_SH_GCC') or shutil.which('sh-elf-gcc')
        if not compiler:
            candidate = ROOT.parent/'K-UI-NeXT/.deps/sh-elf/bin/sh-elf-gcc'
            compiler = str(candidate) if candidate.exists() else None
        if not compiler:
            raise unittest.SkipTest('Native SH toolchain unavailable')
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        elf = directory/'bridge.elf'
        subprocess.run([compiler, '-m4-single-only', '-ml', '-ffreestanding',
                        '-I'+str(ROOT/'include'), '-nostdlib', '-Wl,--build-id=none',
                        '-Wl,--defsym,_kui_armada_ce_maple_hook='+hex(HELPER),
                        '-T'+str(ROOT/'src/loader/vc2_ce_maple_bridge.ld'),
                        str(ROOT/'src/loader/vc2_ce_maple_bridge.S'), '-o', str(elf)],
                       check=True, capture_output=True)
        output = directory/'bridge.bin'
        objcopy = str(Path(compiler).with_name('sh-elf-objcopy'))
        subprocess.run([objcopy, '-O', 'binary', '--only-section=.vc2_maple_bridge',
                        str(elf), str(output)], check=True)
        cls.bridge = output.read_bytes()

    def test_actual_layout_and_callback_literal(self):
        self.assertEqual(len(self.bridge), 100)
        self.assertEqual(self.bridge.count(struct.pack('<I', HELPER)), 1)

    def test_modes_original_continuation_and_ce_argument_homes(self):
        cases = 0
        for base in (0x01ce0000, 0x03ce0000, 0x09ce0000):
            for mode in (0, 1):
                for seed in range(80):
                    with self.subTest(base=hex(base), mode=mode, seed=seed):
                        cpu = CPU(seed, base, mode, self.bridge).run()
                        r, pr, mach, macl, gbr, sr, fpscr, fr = cpu.initial
                        self.assertEqual(cpu.r[8:], r[8:])
                        self.assertEqual((cpu.pr, cpu.mach, cpu.macl, cpu.gbr,
                                          cpu.fpscr, cpu.fr),
                                         (pr, mach, macl, gbr, fpscr, fr))
                        self.assertEqual(cpu.sr & ~1, sr & ~1)
                        self.assertEqual(cpu.mode, mode)
                        self.assertEqual(cpu.apis, [1, mode, 1, mode])
                        self.assertEqual(cpu.original_calls, 1)
                        self.assertEqual(cpu.stack_min, r[15]-FRAME)
                        self.assertEqual(cpu.events,
                            [('mode', 1), ('helper', 0), ('mode', mode),
                             ('original', mode), ('mode', 1), ('helper', 1), ('mode', mode)])
                        cases += 1
        self.assertEqual(cases, 480)


if __name__ == '__main__':
    unittest.main()
