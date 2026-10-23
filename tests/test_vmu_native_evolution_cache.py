#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute Evolution's external Sofdec cache-mode setter without retail bytes.

This bounded integer model checks the *executed* register/MMIO side effects of
the original routine. It does not model hardware cache coherency or claim that
the routine causes the console's FMV refusal. Supply the exact owner-provided
Evolution startup with --startup (or KUI_NATIVE_EVOLUTION1_STARTUP).
"""
import argparse
import hashlib
import os
from pathlib import Path
import sys
import unittest

from test_vmu_native_heap_execution import MASK, SH, physical

STARTUP = os.environ.get("KUI_NATIVE_EVOLUTION1_STARTUP")
DIGEST = "ffb5a8e5ff5547026d93649e1f9058b7ee5e29d8d0ff8a89335701f7e9647f35"
CCR = 0xFF00001C
TAG_BEGIN, TAG_END = 0xF4000000, 0xF4004000


class CacheModeSH(SH):
    """Admit only the extra instructions and MMIO used by this actual path."""

    def __init__(self, image, ccr):
        super().__init__(image)
        self.ccr = ccr
        self.mmio_writes = []
        self.sr_writes = []

    def read(self, address, count=4):
        if address == CCR:
            assert count == 4
            return self.ccr
        return super().read(address, count)

    def write(self, address, value, count=4):
        if address == CCR or TAG_BEGIN <= address < TAG_END:
            assert count == 4
            if address != CCR:
                assert not address & 31
            self.mmio_writes.append((self.executing, address, value & MASK))
            if address == CCR:
                # The SH-4 cache invalidation command bits self-clear.
                self.ccr = value & ~0x808
            return
        return super().write(address, value, count)

    def plain(self, op, pc):
        if op & 0xFF00 == 0xC700:  # mova @(disp,PC),R0
            self.executing = pc
            self.instructions += 1
            self.r[0] = ((pc + 4) & ~3) + (op & 255) * 4
        elif op & 0xFF00 == 0x8100:  # mov.w R0,@(disp,Rn)
            self.executing = pc
            self.instructions += 1
            self.write(self.r[(op >> 4) & 15] + (op & 15) * 2, self.r[0], 2)
        elif op & 0xFF00 == 0x8500:  # mov.w @(disp,Rm),R0
            self.executing = pc
            self.instructions += 1
            value = self.read(self.r[(op >> 4) & 15] + (op & 15) * 2, 2)
            self.r[0] = value if value < 0x8000 else value | 0xFFFF0000
        elif op & 0xFF00 == 0x8800:  # cmp/eq #imm,R0
            self.executing = pc
            self.instructions += 1
            value = op & 255
            self.flag(self.r[0] == (value if value < 128 else value | 0xFFFFFF00))
        else:
            super().plain(op, pc)
        if op & 0xF0FF == 0x400E:
            self.sr_writes.append((pc, self.sr))


class EvolutionCacheModeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not STARTUP:
            raise unittest.SkipTest("Supply the external Evolution startup")
        cls.image = Path(STARTUP).read_bytes()
        if hashlib.sha256(cls.image).hexdigest() != DIGEST:
            raise AssertionError("Evolution startup differs from the analyzed executable")

    def test_actual_cache_setter_side_effects(self):
        for alias in (0x80000000, 0xA0000000):
            for old_ccr in (0x105, 0x185, 0x125, 0x1A5):
                with self.subTest(alias=hex(alias), old_ccr=hex(old_ccr)):
                    cpu = CacheModeSH(self.image, old_ccr)
                    sr = cpu.sr = 0x600003F1
                    stack = alias | 0x0C00F000
                    _, writes, steps = cpu.run(alias | 0x0C162AC8, [0x185], stack)
                    self.assertLess(steps, 5000)
                    self.assertEqual(cpu.ccr, 0x185)
                    ccr_writes = [(pc, value) for pc, address, value in cpu.mmio_writes
                                  if address == CCR]
                    self.assertEqual([value for _, value in ccr_writes], [0x185, 0x985, 0x18D])
                    self.assertEqual([physical(pc) for pc, _ in ccr_writes],
                                     [0x0C162AEE, 0x0C162AFA, 0x0C162B06])
                    tags = [(address, value) for _, address, value in cpu.mmio_writes
                            if address != CCR]
                    offsets = (list(range(0, 0x1000, 32)) + list(range(0x2000, 0x3000, 32))
                               if old_ccr & 0x20 else list(range(0, 0x4000, 32)))
                    self.assertEqual(tags, [(TAG_BEGIN + at, 0) for at in offsets])
                    # Original code changes only its own 16-byte stack window
                    # and the enumerated cache controls. Any FPSCR/FPUL/MMU
                    # control instruction is unsupported and fails execution.
                    self.assertTrue(writes)
                    self.assertTrue(all(physical(stack) - 16 <= physical(at) and
                                        physical(at) + count <= physical(stack)
                                        for _, at, _, count in writes))
                    self.assertEqual(cpu.sr & ~1, sr & ~1)
                    self.assertTrue(all((value & ~0xF1) == (sr & ~0xF1)
                                        for _, value in cpu.sr_writes))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--startup", default=STARTUP)
    args, rest = parser.parse_known_args()
    STARTUP = args.startup
    unittest.main(argv=[sys.argv[0], *rest])
