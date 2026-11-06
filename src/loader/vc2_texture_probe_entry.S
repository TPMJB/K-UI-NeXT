/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_texture_probe.h"
#include "kui/armada_ce_vmu_layout.h"
    .global __kui_vc2_texture_probe
    .set __kui_vc2_texture_probe,1
    .section .texture_probe.entry,"ax"
    .align 2
    .global _kui_vc2_texture_probe_entry
    .type _kui_vc2_texture_probe_entry,@function
_kui_vc2_texture_probe_entry:
    /* Preserve the native frame without writing it. No cache, SR, MMU,
     * banked-register or FPU state is changed by this ordinary C wrapper. */
    mov.l .Lstack,r0
    mov.l r15,@-r0
    mov.l r8,@-r0
    sts.l pr,@-r0
    stc.l gbr,@-r0
    sts.l mach,@-r0
    sts.l macl,@-r0
    mov r0,r15
    add #-16,r15
    mov.l .Lrun,r0
    jsr @r0
    nop
    add #16,r15
    lds.l @r15+,macl
    lds.l @r15+,mach
    ldc.l @r15+,gbr
    lds.l @r15+,pr
    mov.l @r15+,r8
    mov.l @r15,r0
    mov r0,r15
    rts
    nop
    .align 2
.Lstack: .long KUI_ARMADA_CE_VMU_WORKER_STACK_TOP
.Lrun: .long _kui_vc2_texture_probe_run
    .size _kui_vc2_texture_probe_entry,.-_kui_vc2_texture_probe_entry
