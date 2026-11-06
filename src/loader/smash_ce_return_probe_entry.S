/* SPDX-License-Identifier: GPL-3.0-only */
    .section .entry,"ax"
    .align 2
    .global _kui_smash_ce_return_probe_entry
    .type _kui_smash_ce_return_probe_entry,@function
_kui_smash_ce_return_probe_entry:
    /* Original E0 mode/stack are observations, never storage-worker authority.
     * Keep BL as supplied; raise only IMASK before using the private P2 stack. */
    stc sr,r6
    sts pr,r5
    mov r15,r7
    mov r6,r1
    mov.l .Lmask,r0
    or r0,r1
    ldc r1,sr
    mov.l .Lstack,r15
    add #-16,r15
    mov.l .Lmain,r0
    jmp @r0
    nop
    .align 2
.Lmask: .long 0x000000f0
.Lstack: .long __smash_ce_return_stack_top
.Lmain: .long _kui_smash_ce_return_probe_main
    .size _kui_smash_ce_return_probe_entry,.-_kui_smash_ce_return_probe_entry
