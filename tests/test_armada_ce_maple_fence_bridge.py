#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual worker wrapper and private shim in a closed SH model.

Only the authored C PRE/POST body and original CE completion body are replaced
with ABI adversaries. The assembled wrapper must restore the normal CE stack
and prior mode for that original body, and use private P2 stack for C phases.
"""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from test_armada_ce_maple_hook import CPU as CounterCPU, MASK, ROOT


HOOK = 0xACFCD000
STACK_BOTTOM = 0xACFCF800
STACK_TOP = 0xACFCFE00
WORKER_PATCH = 0xB4F70009
CE_FRAME = 28


class FenceCPU(CounterCPU):
    def __init__(self, seed, base, oldmode, bridge, helper, symbols, c_root):
        super().__init__(seed, base, oldmode)
        self.r[8] = 0x00030A60
        self.sr |= (seed & 15) << 4
        self.initial = (self.r.copy(), self.pr, self.mach, self.macl, self.gbr,
                        self.sr, self.fpscr, self.fr.copy())
        self.pc = base + 0x5DB0
        self.bridge = bridge
        self.helper = helper
        self.symbols = symbols
        self.c_root = c_root
        self.original_mode = oldmode
        self.guest_stack_min = self.start_sp
        self.private_stack_min = STACK_TOP
        self.c_calls = []
        self.original_calls = []
        self.events = []
        self.sr_restores = []

    def code(self, address, size):
        if self.base + 0x5DB0 <= address and address + size <= self.base + 0x5DB6:
            # The third halfword is the original worker delay instruction.
            patch = struct.pack('<IH', WORKER_PATCH, 0x6483)
            offset = address - self.base - 0x5DB0
            return patch[offset:offset + size]
        if self.base + 0x67A4 <= address and address + size <= self.base + 0x6800:
            offset = address - self.base - 0x67A4
            return self.bridge[offset:offset + size]
        if HOOK <= address and address + size <= HOOK + len(self.helper):
            assert self.sr & 0x40000000, 'P2 entered before SetKMode(true)'
            offset = address - HOOK
            return self.helper[offset:offset + size]
        raise AssertionError('Unadmitted code read ' + hex(address))

    def write(self, address, value):
        assert address % 4 == 0
        guest = self.start_sp - CE_FRAME <= address < self.start_sp
        private = STACK_TOP - 16 <= address < STACK_TOP
        assert guest or private, 'Unadmitted data write ' + hex(address)
        self.writes.append(address)
        self.mem[address] = value & MASK
        if guest:
            self.guest_stack_min = min(self.guest_stack_min, address)
        if private:
            assert self.sr & 0x40000000
            assert self.sr & 0xF0 == 0xF0, 'Private stack written before IRQ mask'
            self.private_stack_min = min(self.private_stack_min, address)

    def plain(self, word, pc):
        if word & 0xF0FF == 0x4022:
            register = (word >> 8) & 15
            self.r[register] = (self.r[register] - 4) & MASK
            self.write(self.r[register], self.pr)
            return
        if word == 0x400E and pc == HOOK + 0x24:
            assert self.r[15] == self.start_sp - CE_FRAME, 'SR restored on private SP'
            assert self.sr & 0xF0 == 0xF0
            self.sr_restores.append((self.r[15], self.r[0]))
        super().plain(word, pc)

    def api(self):
        assert self.r[15] == self.start_sp - CE_FRAME
        self.events.append(('mode', int(bool(self.r[4]))))
        super().api()

    def c_run(self):
        assert self.sr & 0x40000000, 'C phase called outside kernel mode'
        assert self.sr & 0xF0 == 0xF0, 'C phase called with interrupts unmasked'
        assert self.r[15] == STACK_TOP - 16
        phase = self.r[5]
        assert phase == len(self.c_calls), 'PRE/POST phase order changed'
        assert self.r[4] == self.initial[0][8]
        assert self.r[6] == self.original_mode, 'Previous-mode argument changed'
        self.c_calls.append((phase, self.r[4], self.r[15], self.sr))
        self.events.append(('phase', phase))
        return_pc = self.pr
        # The P2 C ABI preserves r9..r15. Challenge the shim's explicit r8
        # save in addition to all ordinary caller-owned registers/state.
        for index in range(9):
            self.r[index] = (0xA1020304 + index * 0x13579BD + phase) & MASK
        self.mach ^= 0x45AB67CD
        self.macl ^= 0x89EF0123
        self.gbr ^= 0xA55AA55A
        self.pr ^= 0x76543210
        self.sr ^= 1
        self.pc = return_pc

    def original_completion(self):
        assert not self.original_calls, 'Original CE completion called twice'
        assert self.r[4] == self.initial[0][8]
        assert self.mode == self.original_mode
        assert bool(self.sr & 0x40000000) == bool(self.original_mode)
        assert self.r[15] == self.start_sp - CE_FRAME, 'CE body on private SP'
        assert self.sr & 0xF0 == self.initial[5] & 0xF0, 'CE body IRQ mask changed'
        self.original_calls.append((self.r[4], self.mode, self.r[15], self.pr))
        self.events.append(('original', self.original_mode))
        return_pc = self.pr
        # The original C ABI may clobber r0..r7, PR, MAC registers and T.
        # Its callee-saved registers, SP and FPU state remain intact.
        for index in range(8):
            self.r[index] = (0xB2030405 + index * 0x2468ACE) & MASK
        self.mach ^= 0x98AB76CD
        self.macl ^= 0x543210FE
        self.gbr ^= 0xC3C3C3C3
        self.pr ^= 0xFEDCBA98
        self.sr ^= 1
        self.pc = return_pc

    def run(self):
        while self.pc != self.base + 0x5DB6:
            self.steps += 1
            assert self.steps < 1000
            pc = self.pc
            word = struct.unpack('<H', self.code(pc, 2))[0]
            jsr = word & 0xF0FF == 0x400B
            if word >> 12 in (0xA, 0xB) or jsr or word == 0x000B:
                if word >> 12 in (0xA, 0xB):
                    displacement = word & 4095
                    displacement -= 4096 if displacement & 2048 else 0
                    target = (pc + 4 + displacement * 2) & MASK
                    if word >> 12 == 0xB:
                        self.pr = pc + 4
                elif jsr:
                    target = self.r[(word >> 8) & 15]
                    self.pr = pc + 4
                else:
                    target = self.pr
                self.plain(struct.unpack('<H', self.code(pc + 2, 2))[0], pc + 2)
                if target == self.base + 0x6374:
                    self.api()
                elif target == self.symbols[self.c_root]:
                    self.c_run()
                elif target == self.base + 0x5B0C:
                    self.original_completion()
                else:
                    self.pc = target
            elif word >> 8 in (0x89, 0x8B):
                displacement = word & 255
                displacement -= 256 if displacement & 128 else 0
                take = bool(self.sr & 1) == (word >> 8 == 0x89)
                self.pc = (pc + 4 + displacement * 2 if take else pc + 2) & MASK
            else:
                self.plain(word, pc)
                self.pc = (pc + 2) & MASK
        return self


class FenceBridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        prefix = ROOT / '.deps/sh-elf/bin'
        gcc = prefix / 'sh-elf-gcc'
        if not gcc.exists():
            raise unittest.SkipTest('Native SH toolchain unavailable')
        flags = ['-m4-single-only', '-ml', '-ffreestanding', '-I'+str(ROOT/'include')]
        sources = [ROOT / 'src/loader/armada_ce_maple_fence_bridge.S',
                   ROOT / 'src/loader/armada_ce_maple_traffic_entry.S']
        stub = directory / 'stub.S'
        cls.c_root = '_kui_armada_ce_maple_traffic_run'
        stub.write_text('.section .text,"ax"\n.align 1\n.global ' + cls.c_root +
                        '\n' + cls.c_root + ':\nrts\nnop\n')
        sources.append(stub)
        objects = []
        for index, source in enumerate(sources):
            obj = directory / ('input' + str(index) + '.o')
            subprocess.run([str(gcc), *flags, '-DKUI_ARMADA_CE_MAPLE_FENCE=1',
                            '-c', str(source), '-o', str(obj)], check=True)
            objects.append(str(obj))
        elf = directory / 'hook.elf'
        subprocess.run([str(gcc), *flags, '-nostdlib', '-Wl,--build-id=none',
                        '-T' + str(ROOT / 'src/loader/armada_ce_maple_fence.ld'),
                        '-o', str(elf), *objects], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for section in ('bridge', 'hook'):
            output = directory / (section + '.bin')
            subprocess.run([str(prefix / 'sh-elf-objcopy'),
                            '--only-section=.' + section, '-O', 'binary',
                            str(elf), str(output)], check=True)
            setattr(cls, section, output.read_bytes())
        nm = subprocess.check_output([str(prefix / 'sh-elf-nm'), '-n', str(elf)], text=True)
        cls.symbols = {}
        for line in nm.splitlines():
            fields = line.split()
            if len(fields) == 3:
                cls.symbols[fields[2]] = int(fields[0], 16)

    def test_linked_exports_and_helper_target(self):
        self.assertEqual(len(self.bridge), 92)
        self.assertEqual(self.symbols['_kui_armada_ce_maple_bridge'], 0x01DD67A4)
        self.assertEqual(self.symbols['__kui_maple_bridge_end'], 0x01DD6800)
        self.assertEqual(self.symbols['_kui_armada_ce_maple_hook'], HOOK)
        self.assertEqual(self.symbols['__kui_maple_stack_bottom'], STACK_BOTTOM)
        self.assertEqual(self.symbols['__kui_maple_stack_top'], STACK_TOP)
        self.assertEqual(STACK_TOP - STACK_BOTTOM, 1536)
        self.assertLessEqual(self.symbols['__kui_maple_traffic_shim_end'],
                             self.symbols[self.c_root])
        self.assertLessEqual(self.symbols['__kui_maple_workspace_end'], STACK_BOTTOM)

    def test_pre_original_post_modes_stacks_and_preservation(self):
        cases = 0
        for base in (0x01DD0000, 0x03DD0000, 0x09DD0000):
            for mode in (0, 1):
                for seed in range(80):
                    with self.subTest(base=hex(base), mode=mode, seed=seed):
                        cpu = FenceCPU(seed, base, mode, self.bridge, self.hook,
                                       self.symbols, self.c_root).run()
                        r, pr, mach, macl, gbr, sr, fpscr, fr = cpu.initial
                        # Worker caller-owned GPRs/output r0 are intentionally
                        # excluded; the reviewed worker replaces r0 next.
                        self.assertEqual(cpu.r[8:15], r[8:15])
                        self.assertEqual(cpu.r[15], r[15])
                        self.assertEqual((cpu.pr, cpu.mach, cpu.macl, cpu.gbr,
                                          cpu.fpscr, cpu.fr),
                                         (pr, mach, macl, gbr, fpscr, fr))
                        self.assertEqual(cpu.sr & ~1, sr & ~1)
                        self.assertEqual(cpu.mode, mode)
                        self.assertEqual(cpu.apis, [1, mode, 1, mode])
                        self.assertEqual(cpu.guest_stack_min, r[15] - CE_FRAME)
                        self.assertEqual(cpu.private_stack_min, STACK_TOP - 16)
                        self.assertEqual([call[0] for call in cpu.c_calls], [0, 1])
                        self.assertEqual(len(cpu.original_calls), 1)
                        self.assertEqual(cpu.events,
                                         [('mode', 1), ('phase', 0), ('mode', mode),
                                          ('original', mode), ('mode', 1),
                                          ('phase', 1), ('mode', mode)])
                        self.assertEqual(len(cpu.sr_restores), 2)
                        self.assertTrue(all(sp == r[15] - CE_FRAME
                                            for sp, _ in cpu.sr_restores))
                        cases += 1
        self.assertEqual(cases, 480)


if __name__ == '__main__':
    unittest.main()
