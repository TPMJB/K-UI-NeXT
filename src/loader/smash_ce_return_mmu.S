/* SPDX-License-Identifier: GPL-3.0-only */
    .text
    .align 2
    .global _kui_smash_ce_return_mmu_off
_kui_smash_ce_return_mmu_off:
    /* P2 controller, P2 stack and physical-only state have been admitted;
     * all old producers are terminally quiescent before translation ends. */
    mov.l .Lmmucr,r0
    mov #4,r1
    mov.l r1,@r0
    .rept 8
    nop
    .endr
    rts
    nop
    .align 2
.Lmmucr: .long 0xff000010
