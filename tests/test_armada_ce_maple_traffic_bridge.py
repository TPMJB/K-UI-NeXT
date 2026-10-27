#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked traffic shim and CE bridge, replacing only the C body.

The C call deliberately clobbers caller-owned state. This checks the real
assembled entry/exit bytes and private-stack transition independently of the
traffic sampler's implementation.
"""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from test_armada_ce_maple_hook import CPU as CounterCPU, MASK, ROOT


HOOK = 0xACFCE000
STACK_BOTTOM = 0xACFCF800
STACK_TOP = 0xACFCFE00


class TrafficCPU(CounterCPU):
    def __init__(self, seed, base, oldmode, bridge, helper, symbols):
        super().__init__(seed, base, oldmode)
        self.bridge = bridge
        self.helper = helper
        self.symbols = symbols
        self.c_calls = []
        self.guest_stack_min = self.start_sp
        self.private_stack_min = STACK_TOP
        self.sr_restores = []

    def code(self, address, size):
        if self.base + 0x5B0C <= address and address + size <= self.base + 0x5B10:
            offset = address - self.base - 0x5B0C
            return struct.pack('<I', 0x0009A64A)[offset:offset + size]
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
        guest = self.start_sp - 52 <= address < self.start_sp
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
        # sts.l PR,@-Rn is used with r0 while entering the private stack.
        # The original counter model only needed the r15 encoding.
        if word & 0xF0FF == 0x4022:
            register = (word >> 8) & 15
            self.r[register] = (self.r[register] - 4) & MASK
            self.write(self.r[register], self.pr)
            return
        if word == 0x400E and self.c_calls:
            # Restore the guest bridge stack while IRQs are still masked,
            # before restoring the saved SR and its previous interrupt mask.
            assert self.r[15] == self.start_sp - 52, 'SR restored on private SP'
            assert self.sr & 0xF0 == 0xF0
            self.sr_restores.append((self.r[15], self.r[0]))
        super().plain(word, pc)

    def c_run(self):
        assert self.sr & 0x40000000, 'C body called outside kernel mode'
        assert self.sr & 0xF0 == 0xF0, 'C body called with interrupts unmasked'
        assert self.r[15] == STACK_TOP - 16
        self.c_calls.append((self.r[4:8].copy(), self.sr, self.pr))
        return_pc = self.pr
        # Preserve C ABI r9..r14, including the private r15, but challenge
        # the shim's explicit r8 save as well as every ordinary volatile GPR.
        for index in range(9):
            self.r[index] = (0xA1020304 + index * 0x13579BD) & MASK
        self.mach ^= 0x45AB67CD
        self.macl ^= 0x89EF0123
        self.gbr ^= 0xA55AA55A
        self.pr ^= 0x76543210
        self.sr ^= 1
        self.pc = return_pc

    def run(self):
        while self.pc != self.base + 0x5B10:
            self.steps += 1
            assert self.steps < 1000
            pc = self.pc
            word = struct.unpack('<H', self.code(pc, 2))[0]
            if word >> 12 in (0xA, 0xB) or word in (0x400B, 0x000B):
                if word >> 12 in (0xA, 0xB):
                    displacement = word & 4095
                    displacement -= 4096 if displacement & 2048 else 0
                    target = (pc + 4 + displacement * 2) & MASK
                    if word >> 12 == 0xB:
                        self.pr = pc + 4
                elif word == 0x400B:
                    target = self.r[0]
                    self.pr = pc + 4
                else:
                    target = self.pr
                slot = struct.unpack('<H', self.code(pc + 2, 2))[0]
                self.plain(slot, pc + 2)
                if target == self.base + 0x6374:
                    self.api()
                elif target == self.symbols['_kui_armada_ce_maple_traffic_run']:
                    self.c_run()
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


class TrafficBridgeTests(unittest.TestCase):
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
        sources = [ROOT / 'src/loader/armada_ce_maple_hook.S',
                   ROOT / 'src/loader/armada_ce_maple_traffic_entry.S']
        stub = directory / 'stub.S'
        stub.write_text('.section .text,"ax"\n.align 1\n'
                        '.global _kui_armada_ce_maple_traffic_run\n'
                        '_kui_armada_ce_maple_traffic_run:\nrts\nnop\n')
        sources.append(stub)
        objects = []
        for index, source in enumerate(sources):
            obj = directory / ('input' + str(index) + '.o')
            subprocess.run([str(gcc), *flags, '-DKUI_ARMADA_CE_MAPLE_TRAFFIC=1',
                            '-c', str(source), '-o', str(obj)], check=True)
            objects.append(str(obj))
        elf = directory / 'hook.elf'
        subprocess.run([str(gcc), *flags, '-nostdlib', '-Wl,--build-id=none',
                        '-T' + str(ROOT / 'src/loader/armada_ce_maple_traffic.ld'),
                        '-o', str(elf), *objects], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for section in ('bridge', 'hook'):
            output = directory / (section + '.bin')
            subprocess.run([str(prefix / 'sh-elf-objcopy'),
                            '--only-section=.' + section, '-O', 'binary',
                            str(elf), str(output)], check=True)
            setattr(cls, section, output.read_bytes())
        nm = subprocess.check_output([str(prefix / 'sh-elf-nm'), '-n', str(elf)],
                                     text=True)
        cls.symbols = {}
        for line in nm.splitlines():
            fields = line.split()
            if len(fields) == 3:
                cls.symbols[fields[2]] = int(fields[0], 16)

    def test_linked_exports_and_bridge_target(self):
        self.assertEqual(len(self.bridge), 92)
        self.assertEqual(struct.unpack('<I', self.bridge[-4:])[0], HOOK)
        self.assertEqual(self.symbols['_kui_armada_ce_maple_hook'], HOOK)
        self.assertEqual(self.symbols['__kui_maple_stack_bottom'], STACK_BOTTOM)
        self.assertEqual(self.symbols['__kui_maple_stack_top'], STACK_TOP)
        self.assertEqual(STACK_TOP - STACK_BOTTOM, 1536)
        self.assertLessEqual(self.symbols['__kui_maple_traffic_shim_end'],
                             self.symbols['_kui_armada_ce_maple_traffic_run'])
        self.assertLessEqual(self.symbols['__kui_maple_workspace_end'], STACK_BOTTOM)

    def test_cpu_private_stack_and_preservation(self):
        cases = 0
        for base in (0x01DD0000, 0x03DD0000, 0x09DD0000):
            for mode in (0, 1):
                for seed in range(80):
                    with self.subTest(base=hex(base), mode=mode, seed=seed):
                        cpu = TrafficCPU(seed, base, mode, self.bridge,
                                         self.hook, self.symbols).run()
                        r, pr, mach, macl, gbr, sr, fpscr, fr = cpu.initial
                        self.assertEqual(cpu.r[:15], r[:15])
                        self.assertEqual(cpu.r[15], r[15] - 8)
                        self.assertEqual((cpu.pr, cpu.mach, cpu.macl, cpu.gbr,
                                          cpu.fpscr, cpu.fr),
                                         (pr, mach, macl, gbr, fpscr, fr))
                        self.assertEqual(cpu.sr & ~1, sr & ~1)
                        self.assertEqual(cpu.mode, mode)
                        self.assertEqual(cpu.apis, [1, mode])
                        self.assertEqual(cpu.guest_stack_min, r[15] - 52)
                        self.assertEqual(cpu.private_stack_min, STACK_TOP - 16)
                        self.assertEqual((cpu.mem[r[15] - 4], cpu.mem[r[15] - 8]),
                                         (r[8], r[9]))
                        self.assertEqual(len(cpu.c_calls), 1)
                        self.assertEqual(cpu.c_calls[0][0], [r[4], r[15], pr, mode])
                        entry_sr = ((sr & ~0x40000001) | 0x40000000 |
                                    ((sr ^ 1) & 1))
                        self.assertEqual(cpu.c_calls[0][1], entry_sr | 0xF0)
                        self.assertEqual(cpu.sr_restores, [(r[15] - 52, entry_sr)])
                        self.assertEqual(cpu.mem[STACK_TOP - 4], r[15] - 52)
                        self.assertEqual(cpu.mem[STACK_TOP - 8], r[8])
                        self.assertEqual(cpu.mem[STACK_TOP - 16], entry_sr)
                        cases += 1
        self.assertEqual(cases, 480)


if __name__ == '__main__':
    unittest.main()
