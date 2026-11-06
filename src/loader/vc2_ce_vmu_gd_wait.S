/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_gd_wait.h"
#include "kui/armada_ce_vmu_layout.h"
    .section .gd_wait.entry,"ax"
    .align 2
    .global _kui_vc2_ce_vmu_gd_wait
    .type _kui_vc2_ce_vmu_gd_wait,@function
_kui_vc2_ce_vmu_gd_wait:
    /* Only REQUEST needs a real token. Keep every legacy non-FN0 result. */
    mov r7,r1
    tst r1,r1
    bt .Lrequest
    jmp @r3
    nop
.Lrequest:
    mov r0,r11
    mov r2,r12
    mov.l .Lp2,r1
    or r1,r11
    add #-32,r15
    mov.l r4,@r15
    mov.l r5,@(4,r15)
    mov.l r6,@(8,r15)
    mov.l r7,@(12,r15)
    stc gbr,r0
    mov.l r0,@(16,r15)
    sts mach,r0
    mov.l r0,@(20,r15)
    sts macl,r0
    mov.l r0,@(24,r15)
    mov.l @(28,r8),r4
    mov.l @(32,r8),r5
    mov.l @r15,r6
    mov r9,r7
    mov.l .Ladmit,r0
    add #-16,r15
    jsr @r0
    nop
    add #16,r15
    mov r0,r13
    tst r13,r13
    bt .Ladmission_fault
    mov.l @(16,r15),r0
    ldc r0,gbr
    mov.w .Llimit,r14
.Lpoll:
    mov.l @r11,r0
    tst r0,r0
    bt .Lretry
    cmp/eq #KUI_VC2_CE_VMU_GD_LOAN_FLAG,r0
    bf .Lstate_fault
    dt r14
    bt .Ltimeout
    /* The live CE caller owns all this stack; do not yield on the fixed
     * low/GD or VMU stacks. Sleep supplies scheduler progress for Maple. */
    add #-16,r15
    mov #1,r4
    mov.l @(32,r8),r0
    ldc r0,sr
    jsr @r13
    nop
    add #16,r15
    stc sr,r0
    or #0xf0,r0
    ldc r0,sr
    mov.l @(16,r15),r0
    ldc r0,gbr
    bra .Lpoll
    nop
.Lretry:
    mov.l @(16,r15),r0
    ldc r0,gbr
    mov.l @(20,r15),r0
    lds r0,mach
    mov.l @(24,r15),r0
    lds r0,macl
    mov.l @(0,r15),r4
    mov.l @(4,r15),r5
    mov.l @(8,r15),r6
    mov.l @(12,r15),r7
    add #32,r15
    jmp @r12
    nop
.Ladmission_fault:
    mov #1,r4
    bra .Lfatal
    mov r13,r5
.Lstate_fault:
    mov #3,r4
    bra .Lfatal
    mov r0,r5
.Ltimeout:
    mov #2,r4
    mov.w .Llimit,r5
.Lfatal:
    mov.l @(28,r8),r6
    mov.l @(32,r8),r7
    /* Terminal capture never resumes either owner. Its private stack may
     * replace the parked proof frame only after every argument is copied. */
    mov.l .Lpanic_stack,r15
    mov.l .Lfault,r0
    add #-16,r15
    jsr @r0
    nop
.Lhalt:
    bra .Lhalt
    nop
    .align 2
.Llimit: .word KUI_VC2_CE_VMU_GD_WAIT_LIMIT
    .align 2
.Lp2: .long 0x20000000
.Ladmit: .long _kui_vc2_ce_vmu_gd_wait_admit
.Lfault: .long _kui_vc2_ce_vmu_gd_wait_fault
.Lpanic_stack: .long KUI_ARMADA_CE_VMU_STACK_TOP
    .size _kui_vc2_ce_vmu_gd_wait,.-_kui_vc2_ce_vmu_gd_wait
