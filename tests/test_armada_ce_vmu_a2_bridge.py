#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual A2-selected worker bridge and private P2 shim.

The established fence CPU model supplies strict PRE/original/POST mode,
stack and ABI adversaries. This test assembles the A2 flag and linker variant,
including its distinct 24 KiB reservation, without changing the fence test.
"""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from test_armada_ce_maple_hook import CPU as CounterCPU, ROOT
from test_armada_ce_maple_fence_bridge import (
    FenceCPU, MASK, WORKER_PATCH, CE_FRAME, STACK_BOTTOM, STACK_TOP,
)

HOOK = 0xACFCA000


class A2CPU(FenceCPU):
    def code(self, address, size):
        if self.base + 0x5DB0 <= address and address + size <= self.base + 0x5DB6:
            patch = struct.pack('<IH', WORKER_PATCH, 0x6483)
            offset = address - self.base - 0x5DB0
            return patch[offset:offset + size]
        if self.base + 0x67A4 <= address and address + size <= self.base + 0x6800:
            offset = address - self.base - 0x67A4
            return self.bridge[offset:offset + size]
        if HOOK <= address and address + size <= HOOK + len(self.helper):
            assert self.sr & 0x40000000, 'A2 P2 entered before SetKMode(true)'
            offset = address - HOOK
            return self.helper[offset:offset + size]
        raise AssertionError('Unadmitted A2 code read ' + hex(address))

    def plain(self, word, pc):
        # The private shim is unchanged, but is linked at A000 for A2.
        if word & 0xF0FF == 0x4022:
            register = (word >> 8) & 15
            self.r[register] = (self.r[register] - 4) & MASK
            self.write(self.r[register], self.pr)
            return
        if word == 0x400E and pc == HOOK + 0x24:
            assert self.r[15] == self.start_sp - CE_FRAME, 'A2 SR restored on private SP'
            assert self.sr & 0xF0 == 0xF0
            self.sr_restores.append((self.r[15], self.r[0]))
        CounterCPU.plain(self, word, pc)


class A2BridgeTests(unittest.TestCase):
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
                            '-DKUI_ARMADA_CE_MAPLE_A2=1',
                            '-c', str(source), '-o', str(obj)], check=True)
            objects.append(str(obj))
        elf = directory / 'hook.elf'
        subprocess.run([str(gcc), *flags, '-nostdlib', '-Wl,--build-id=none',
                        '-T' + str(ROOT / 'src/loader/armada_ce_vmu_a2.ld'),
                        '-o', str(elf), *objects], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for section in ('bridge', 'hook'):
            output = directory / (section + '.bin')
            selected = ['--only-section=.' + section]
            if section == 'hook':
                selected.append('--only-section=.uncached')
            subprocess.run([str(prefix / 'sh-elf-objcopy'),
                            *selected, '-O', 'binary',
                            str(elf), str(output)], check=True)
            setattr(cls, section, output.read_bytes())
        nm = subprocess.check_output([str(prefix / 'sh-elf-nm'), '-n', str(elf)], text=True)
        cls.symbols = {}
        for line in nm.splitlines():
            fields = line.split()
            if len(fields) == 3:
                cls.symbols[fields[2]] = int(fields[0], 16)

    def test_a2_linked_variant_and_private_ranges(self):
        self.assertEqual(len(self.bridge), 92)
        self.assertEqual(self.symbols['_kui_armada_ce_maple_bridge'], 0x01DD67A4)
        self.assertEqual(self.symbols['__kui_maple_bridge_end'], 0x01DD6800)
        self.assertEqual(self.symbols['_kui_armada_ce_maple_hook'], HOOK)
        self.assertEqual(self.symbols['__kui_maple_stack_bottom'], STACK_BOTTOM)
        self.assertEqual(self.symbols['__kui_maple_stack_top'], STACK_TOP)
        self.assertEqual(STACK_TOP - STACK_BOTTOM, 1536)
        self.assertEqual(self.symbols['__kui_maple_cached_start'],
                         self.symbols['__kui_maple_uncached_end'] - 0x20000000)
        self.assertGreaterEqual(self.symbols[self.c_root],
                                self.symbols['__kui_maple_cached_start'])
        self.assertLess(self.symbols[self.c_root],
                        self.symbols['__kui_maple_cached_end'])
        self.assertEqual(self.symbols[self.c_root] & 0xE0000000, 0x80000000)
        self.assertEqual(len(self.hook),
                         self.symbols['__kui_maple_hook_binary_end'] - HOOK)
        self.assertLessEqual(self.symbols['__kui_maple_workspace_end'], STACK_BOTTOM)
        self.assertEqual(self.symbols['_kui_armada_ce_vmu_a2_log'], 0xACFCFF00)
        self.assertEqual(0xACFD0000 - HOOK, 24 * 1024)

    def test_a2_pre_original_post_order_and_abi(self):
        cases = 0
        for base in (0x01DD0000, 0x03DD0000, 0x09DD0000):
            for mode in (0, 1):
                for seed in range(80):
                    with self.subTest(base=hex(base), mode=mode, seed=seed):
                        cpu = A2CPU(seed, base, mode, self.bridge, self.hook,
                                    self.symbols, self.c_root).run()
                        r, pr, mach, macl, gbr, sr, fpscr, fr = cpu.initial
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
