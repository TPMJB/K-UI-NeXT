#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SH-4 delay-slot legality shared by linked-code audits and CPU models.

Renesas SH-4 Software Manual Rev.6, section 5.6.2(10), printed page 108,
and section 9.57, printed page 336: PC-relative MOV/MOVA in a delay slot
raises the slot illegal instruction exception; it does not load a literal.
"""


class SlotIllegalInstruction(AssertionError):
    expevt = 0x1A0

    def __init__(self, branch_pc, slot_pc, opcode, reason):
        self.branch_pc = self.spc = branch_pc & 0xFFFFFFFF
        self.slot_pc, self.opcode, self.reason = slot_pc, opcode, reason
        super().__init__(f"SH-4 slot illegal instruction EXPEVT=000001a0 "
                         f"SPC={self.spc:08x} slot={slot_pc:08x} "
                         f"opcode={opcode:04x}: {reason}")


def delayed_branch(word):
    return (word >> 12 in (0xA, 0xB) or word >> 8 in (0x8D, 0x8F) or
            word & 0xF0FF in (0x400B, 0x402B, 0x0003, 0x0023) or
            word in (0x000B, 0x002B))


def slot_illegal_reason(word):
    if word >> 12 in (0x9, 0xD) or word >> 8 == 0xC7:
        return "PC-relative MOV or MOVA"
    if (delayed_branch(word) or word >> 8 in (0x89, 0x8B, 0xC3) or
            word & 0xF0FF in (0x400E, 0x4007)):
        return "PC/control-flow modifying instruction"
    if word == 0xFFFD:
        return "defined undefined-instruction encoding"
    return None


def validate_delay_slot(word, branch_pc, slot_pc=None):
    """Reject before execution; SPC identifies the preceding branch.

    This predicate covers unconditional SH-4 slot restrictions. The caller
    retains separate privilege/FPU admission and unsupported-opcode checks.
    BF/S and BT/S use it even when their branch condition is false.
    """
    reason = slot_illegal_reason(word)
    if reason:
        raise SlotIllegalInstruction(branch_pc, branch_pc + 2 if slot_pc is None else slot_pc,
                                     word, reason)
