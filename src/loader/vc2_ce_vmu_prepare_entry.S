/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_handoff.h"
/* The trusted low gate enters only after the initialized helper bytes have
 * matched their immutable-code CRC with IRQs masked and no active DMA. Native
 * startup may have used the uninitialized tail. Clear that complete tail
 * through P2 before creating any high stack frame or reading helper state. */
    .section .prepare.entry,"ax"
    .align 2
    .global _kui_vc2_ce_vmu_prepare
    .type _kui_vc2_ce_vmu_prepare,@function
_kui_vc2_ce_vmu_prepare:
    .global __kui_vc2_prepare_clear_begin
__kui_vc2_prepare_clear_begin:
    mov.l .Lclear_from,r0
    mov.l .Lclear_to,r1
    mov #0,r2
.Lclear:
    mov.l r2,@r0
    add #4,r0
    cmp/hs r1,r0
    bf .Lclear
    .global __kui_vc2_prepare_clear_end
    .global _kui_vc2_ce_vmu_prepare_zeroed
__kui_vc2_prepare_clear_end:
_kui_vc2_ce_vmu_prepare_zeroed:
#if KUI_VC2_VMU_PERSIST
    /* Lower BSS is reserved only now, after native startup has finished. */
    mov.l .Lworkspace_from,r0
    mov.l .Lworkspace_to,r1
.Lworkspace_clear:
    mov.l r2,@r0
    add #4,r0
    cmp/hs r1,r0
    bf .Lworkspace_clear
#endif
    mov.l .Lstack,r0
    mov.l r15,@-r0
    mov.l r8,@-r0
    sts pr,r1
    mov.l r1,@-r0
    stc gbr,r1
    mov.l r1,@-r0
    sts mach,r1
    mov.l r1,@-r0
    sts macl,r1
    mov.l r1,@-r0
    mov r0,r15
    /* The prior native program could have left I-cache lines for these P1
     * physical aliases. The staged CRC checks RAM, not instruction-cache
     * contents. Invalidate through this P2 wrapper before calling any C. */
    mov.l .Lccr,r0
    mov.l @r0,r1
    mov.l .Lici,r2
    or r2,r1
    mov.l r1,@r0
    .rept 8
    nop
    .endr
    mov.l .Lrun,r0
    /* SH C callees may spill R4..R7 into their caller-provided homes.
     * Keep these16 bytes below, and disjoint from, our saved native frame. */
    add #-16,r15
    jsr @r0
    nop
    add #16,r15
    lds.l @r15+,macl
    lds.l @r15+,mach
    ldc.l @r15+,gbr
    lds.l @r15+,pr
    mov.l @r15+,r8
    mov.l @r15,r1
    mov r1,r15
    rts
    nop
    .align 2
.Lstack: .long __kui_maple_stack_top
.Lrun: .long _kui_vc2_ce_vmu_prepare_run
.Lccr: .long 0xff00001c
.Lici: .long 0x00000800
.Lclear_from: .long __kui_maple_hook_binary_end
.Lclear_to: .long KUI_VC2_VMU_RESERVE_END+0x20000000
#if KUI_VC2_VMU_PERSIST
.Lworkspace_from: .long __kui_maple_workspace_start
.Lworkspace_to: .long __kui_maple_workspace_end
#endif
    .size _kui_vc2_ce_vmu_prepare,.-_kui_vc2_ce_vmu_prepare
