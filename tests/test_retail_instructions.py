# SPDX-License-Identifier: GPL-3.0-only
"""The linked-code FPU audit (tools/check_retail_instructions.py) on objdump
text: literal pools are data, the stage's one FPSCR setup is the only FPU
instruction allowed, and a pointer in a literal pool cannot hide code."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import check_retail_instructions as tool

SETUP = 0x8ce00098


def row(address, opcode, text):
    """One objdump -d line: address, the two bytes in memory order, text."""
    return f"{address:08x}:\t{opcode & 0xff:02x} {opcode >> 8:02x}       \t{text}"


def stage(bss_end_low=0x3564, bss_begin=0x8ce08400, setup=True, extra=()):
    """A stage like the linked one: the entry loads the BSS bounds from its
    literal pool (0x8ce00040 and 0x8ce00044), then .text holds the boot
    FPSCR setup at 0x8ce00098."""
    lines = ["", "Disassembly of section .entry:", "", "8ce00000 <_start>:",
             row(0x8ce00022, 0xd007, "mov.l\t8ce00040 <_start+0x40>,r0"),
             row(0x8ce00024, 0xd107, "mov.l\t8ce00044 <_start+0x44>,r1"),
             row(0x8ce00026, 0x0009, "nop\t")]
    for address in range(0x8ce00028, 0x8ce00040, 2):
        lines.append(row(address, 0x0009, "nop\t"))
    pool = ((0x8ce00040, bss_begin & 0xffff), (0x8ce00042, bss_begin >> 16),
            (0x8ce00044, bss_end_low), (0x8ce00046, 0x8ce1))
    for address, half in pool:
        # objdump decodes pool halfwords as whatever they spell.
        if half & 0xf000 == 0x9000:
            target = address + 4 + (half & 0xff) * 2
            text = f"mov.w\t{target:08x} <_start+0x{target - 0x8ce00000:x}>,r{(half >> 8) & 15}"
        elif half & 0xf000 == 0xf000:
            text = "fadd\tfr0,fr15"
        else:
            text = f".word 0x{half:04x}"
        lines.append(row(address, half, text))
    lines += ["", "Disassembly of section .text:", "",
              "8ce00060 <kui_retail_bootstrap_enter>:"]
    for address in range(0x8ce00060, SETUP, 2):
        lines.append(row(address, 0x0009, "nop\t"))
    if setup:
        lines += [f"{SETUP:08x} <__retail_boot_fpscr_init>:", row(SETUP, 0x406a, "lds\tr0,fpscr")]
    lines.append(row(SETUP + 2, 0x000b, "rts\t"))
    lines.append(row(SETUP + 4, 0x0009, "nop\t"))
    lines += list(extra)
    return "\n".join(lines) + "\n"


class RetailInstructionAudit(unittest.TestCase):
    def failure(self, name, text):
        with self.assertRaises(SystemExit) as caught:
            tool.audit(name, text)
        message = str(caught.exception)
        self.assertTrue(message.startswith("Retail instruction audit failed: "), message)
        return message

    def test_linked_stage_passes(self):
        self.assertGreater(tool.audit("stage", stage()), 0)

    def test_pool_pointer_cannot_hide_the_boot_setup(self):
        # A BSS end of 0x8ce19128: its low half reads as MOV.W @(0x50,PC),R1
        # at 0x8ce00044, "loading" 0x8ce00098, the FPSCR setup. That word is
        # pool data (the entry's MOV.L loads it), so it marks nothing.
        self.assertEqual(0x8ce00044 + 4 + 0x28 * 2, SETUP)
        self.assertGreater(tool.audit("stage", stage(bss_end_low=0x9128)), 0)

    def test_fpu_bits_in_a_literal_are_data(self):
        self.assertGreater(tool.audit("stage", stage(bss_begin=0xff000000)), 0)

    def test_fpu_use_is_rejected(self):
        extra = [row(SETUP + 6, 0xf210, "fadd\tfr1,fr2")]
        self.assertIn("FPU use in stage", self.failure("stage", stage(extra=extra)))
        extra = [row(SETUP + 6, 0x4062, "sts.l\tfpul,@-r0")]
        self.assertIn("FPU use in stage", self.failure("stage", stage(extra=extra)))

    def test_a_second_fpscr_write_is_rejected(self):
        extra = [row(SETUP + 6, 0x406a, "lds\tr0,fpscr")]
        self.assertIn("FPU use in stage", self.failure("stage", stage(extra=extra)))

    def test_the_setup_is_required_once(self):
        self.assertIn("missing unique bootstrap FPSCR setup",
                      self.failure("stage", stage(setup=False)))

    def test_residents_get_no_waiver(self):
        self.assertIn("FPU use in resident-sci", self.failure("resident-sci", stage()))

    def test_empty_disassembly_is_rejected(self):
        self.assertIn("no decoded instructions", self.failure("entry", "\n"))


if __name__ == "__main__":
    unittest.main()
