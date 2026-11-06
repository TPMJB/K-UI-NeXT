/* SPDX-License-Identifier: GPL-3.0-only */
/* VC2's source-bound ordinary-worker completion wrapper. PRE and POST run
 * through an explicitly privileged P2 helper; the original completion runs
 * on the ordinary CE stack in its previous mode. The helper must honor the
 * native integer C ABI. It is responsible for masking before private-stack
 * use and restoring SR before return. No Maple register or guest RX is used
 * by this bridge. All CE calls receive sixteen writable argument-home bytes
 * below the saved frame, independently of whether a callee actually uses it.
 * Incoming T is dead at the original call, and caller-volatile GPRs/results
 * remain caller-volatile, just as at the unchanged original completion. */
#include "kui/vc2_ce_maple_profile.h"
    .section .vc2_maple_bridge,"ax"
    .align 1
    .global _kui_vc2_ce_maple_bridge
_kui_vc2_ce_maple_bridge:
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    sts.l pr,@-r15
    sts.l mach,@-r15
    sts.l macl,@-r15
    stc.l gbr,@-r15
    add #-16,r15
    mov r4,r8
    mov #1,r4
    bsr _kui_vc2_ce_maple_bridge+(KUI_VC2_CE_MAPLE_SETKMODE_RVA-KUI_VC2_CE_MAPLE_BRIDGE_RVA)
    nop
    mov r0,r9
    mov.l .Lhelper,r10
    mov #0,r5
    mov r9,r6
    jsr @r10
    mov r8,r4
    mov r9,r4
    bsr _kui_vc2_ce_maple_bridge+(KUI_VC2_CE_MAPLE_SETKMODE_RVA-KUI_VC2_CE_MAPLE_BRIDGE_RVA)
    nop
    bsr _kui_vc2_ce_maple_bridge+(KUI_VC2_CE_MAPLE_COMPLETION_RVA-KUI_VC2_CE_MAPLE_BRIDGE_RVA)
    mov r8,r4
    mov #1,r4
    bsr _kui_vc2_ce_maple_bridge+(KUI_VC2_CE_MAPLE_SETKMODE_RVA-KUI_VC2_CE_MAPLE_BRIDGE_RVA)
    nop
    mov #1,r5
    mov r0,r6
    jsr @r10
    mov r8,r4
    mov r9,r4
    bsr _kui_vc2_ce_maple_bridge+(KUI_VC2_CE_MAPLE_SETKMODE_RVA-KUI_VC2_CE_MAPLE_BRIDGE_RVA)
    nop
    add #16,r15
    ldc.l @r15+,gbr
    lds.l @r15+,macl
    lds.l @r15+,mach
    lds.l @r15+,pr
    mov.l @r15+,r10
    mov.l @r15+,r9
    rts
    mov.l @r15+,r8
    .align 2
    .global __kui_vc2_maple_bridge_helper_literal
__kui_vc2_maple_bridge_helper_literal:
.Lhelper:
    .long _kui_armada_ce_maple_hook
    .global __kui_vc2_maple_bridge_code_end
__kui_vc2_maple_bridge_code_end:
    .fill (KUI_VC2_CE_MAPLE_BRIDGE_BYTES-(.-_kui_vc2_ce_maple_bridge))/2,2,0x0009
    .global __kui_vc2_maple_bridge_end
__kui_vc2_maple_bridge_end:
