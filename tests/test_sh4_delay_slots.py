#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SH-4 slot rules, including a negative replay of the shipped cold gateway.

Optional exact-release checks:
  python3 tests/test_sh4_delay_slots.py --failed-build build/sdk-coherence-final \
    --startup /path/to/original/Evolution/1ST_READ.BIN
No original executable instructions are embedded in this file.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from sh4_delay_slots import SlotIllegalInstruction, delayed_branch, validate_delay_slot
from check_retail_instructions import audit
from check_vmu_native_stage_stack import Machine
from test_vmu_native_heap_execution import SH

OPTIONS = argparse.Namespace(failed_build=None, startup=None)


class SmallMachine(Machine):
    def __init__(self, words, r3=0, t=0):
        self.base = self.pc = 0x8c010000
        self.blob = struct.pack('<' + 'H' * len(words), *words)
        self.stop_pc = self.base + len(self.blob)
        self.r = [0] * 16
        self.r[2], self.r[3] = 0x12345678, r3
        self.sr = 0x600000f0 | t
        self.mem, self.calls = {}, []


class SlotRules(unittest.TestCase):
    def test_forbidden_instruction_families_and_exception_state(self):
        words = [0x9000 | n << 8 for n in range(16)]
        words += [0xd000 | n << 8 for n in range(16)]
        words += [0xc700, 0xc7ff, 0xa000, 0xb000, 0x8900, 0x8b00,
                  0x8d00, 0x8f00, 0xc300, 0xfffd, 0x000b, 0x002b]
        for n in range(16):
            words += [0x400b | n << 8, 0x402b | n << 8, 0x0003 | n << 8,
                      0x0023 | n << 8, 0x400e | n << 8, 0x4007 | n << 8]
        for word in words:
            with self.subTest(word=hex(word)):
                with self.assertRaises(SlotIllegalInstruction) as caught:
                    validate_delay_slot(word, 0xacfdbb70)
                error = caught.exception
                self.assertEqual((error.expevt, error.spc, error.slot_pc, error.opcode),
                                 (0x1a0, 0xacfdbb70, 0xacfdbb72, word))

    def test_legal_register_operations_and_nop(self):
        for word in (0x0009, 0x6323, 0x6233, 0xe004, 0x7004, 0x2fd6, 0x5e01):
            validate_delay_slot(word, 0x8c010000)
        self.assertTrue(delayed_branch(0x8d00))
        self.assertTrue(delayed_branch(0x8f00))
        self.assertFalse(delayed_branch(0x8900))

    def test_machine_rejects_slot_even_when_conditional_branch_not_taken(self):
        for op, t in ((0x8d01, 0), (0x8f01, 1)):
            with self.assertRaises(SlotIllegalInstruction):
                SmallMachine([op, 0x902b, 0x0009], t=t).run()

    def test_heap_cpu_rejects_slot_before_literal_read(self):
        for op, t in ((0x432b, 0), (0x8d01, 0), (0x8f01, 1)):
            cpu = SH(struct.pack('<3H', op, 0x902b, 0x0009))
            cpu.r[3] = 0xdeadbeee
            cpu.sr = (cpu.sr & ~1) | t
            before = list(cpu.writes)
            with self.assertRaises(SlotIllegalInstruction) as caught:
                cpu.run(0x8c010000, [], 0x8cff0000)
            self.assertEqual(caught.exception.spc, 0x8c010000)
            self.assertEqual(cpu.writes, before)

    def test_jump_target_is_captured_before_legal_register_restore_slot(self):
        cpu = SmallMachine([0x432b, 0x6323, 0x0009], r3=0x8c010004)
        cpu.run()
        self.assertEqual(cpu.pc, cpu.stop_pc)
        self.assertEqual(cpu.r[3], 0x12345678)

    def test_static_audit_rejects_real_slot_but_preserves_literal_classification(self):
        bad = ' 8c010000: 2b 40 jmp @r0\n 8c010002: 2b 90 mov.w 8c01005c,r0\n'
        with self.assertRaises(SystemExit):
            audit('worker', bad)
        # The latter pair is a referenced four-byte constant, not code.
        data = (' 8c010000: 02 d1 mov.l 8c01000c,r1\n'
                ' 8c010002: 07 a0 bra 8c010014\n'
                ' 8c010004: 09 00 nop\n'
                ' 8c01000c: 2b 40 jmp @r0\n'
                ' 8c01000e: 2b 90 mov.w 8c010068,r0\n'
                ' 8c010014: 09 00 nop\n')
        self.assertEqual(audit('worker', data), 4)


class ReleasedGatewayRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if OPTIONS.failed_build is None:
            raise unittest.SkipTest('Supply --failed-build for the exact shipped-artifact negative check')

    def test_failed_release_linked_gateway_raises_before_original_continuation(self):
        from check_vmu_native_sdk_coherence import Gate
        from check_vmu_native_worker_gateway import read_elf
        worker = read_elf(OPTIONS.failed_build / 'native-worker.elf')
        cpu = Gate(worker, 0)
        with self.assertRaises(SlotIllegalInstruction) as caught:
            cpu.check()
        error = caught.exception
        self.assertEqual((error.spc, error.slot_pc, error.opcode),
                         (0xac10bb70, 0xac10bb72, 0x902b))
        self.assertEqual(error.expevt, 0x1a0)
        self.assertNotEqual(cpu.pc, cpu.resume)

    def test_failed_release_static_audit(self):
        tool = ROOT / '.deps/sh-elf/bin/sh-elf-objdump'
        dis = subprocess.check_output([str(tool), '-d', str(OPTIONS.failed_build / 'native-worker.elf')], text=True)
        with self.assertRaises(SystemExit) as caught:
            audit('native-worker', dis)
        self.assertIn('branch 8c10bb70, slot 8c10bb72, opcode 902b', str(caught.exception))

    def test_actual_relocated_stage_and_sdk_handoff_raise_exact_cpu_exception(self):
        if OPTIONS.startup is None:
            self.skipTest('Supply --startup for the actual relocated integration replay')
        from profile_vmu_native_sdk_cache_sh import make_handoff, replay
        with self.assertRaises(SlotIllegalInstruction) as caught:
            replay(OPTIONS.startup, OPTIONS.failed_build, make_handoff(), cached_stack=True,
                   oix=False, pre_tas_clean=False, actual_stage=True, pressure=True)
        error = caught.exception
        self.assertEqual((error.expevt, error.spc, error.slot_pc, error.opcode),
                         (0x1a0, 0xacfdbb70, 0xacfdbb72, 0x902b))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--failed-build', type=Path)
    parser.add_argument('--startup', type=Path)
    OPTIONS, rest = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *rest])
