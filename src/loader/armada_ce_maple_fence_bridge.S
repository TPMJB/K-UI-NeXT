/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact-source ordinary-worker call wrapper, installed before NK starts.
 * Worker RVA5db0 becomes NOP; BSR at5db2 reaches this mapped bridge and
 * retains PR5db6 and the original mov r8,r4 delay instruction. Original
 * completion RVA5b0c is unchanged. PRE/POST helper phases run in P2 with
 * explicit kernel-mode transitions. The original CE body runs between them
 * on this ordinary CE stack and in the exact previous mode, never private SP.
 * The helper must honor the C ABI, including r8/r9/r10 and the remaining
 * callee-saved registers. MACH/MACL/GBR are additionally preserved here.
 * No byte in this bridge reads or writes Maple registers or a reply buffer. */
#include "kui/armada_ce_vmu_layout.h"
    .section .bridge,"ax"
    .align 1
    .global _kui_armada_ce_maple_bridge
_kui_armada_ce_maple_bridge:
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    sts.l pr,@-r15
    sts.l mach,@-r15
    sts.l macl,@-r15
    stc.l gbr,@-r15
    mov r4,r8
    mov #1,r4
    bsr _kui_armada_ce_maple_bridge+(0x6374-0x67a4)
    nop
    mov r0,r9
    mov.l .Lhelper,r10
    mov #0,r5
    mov r9,r6
    jsr @r10
    mov r8,r4
    mov r9,r4
    bsr _kui_armada_ce_maple_bridge+(0x6374-0x67a4)
    nop
    bsr _kui_armada_ce_maple_bridge+(0x5b0c-0x67a4)
    mov r8,r4
    mov #1,r4
    bsr _kui_armada_ce_maple_bridge+(0x6374-0x67a4)
    nop
    mov #1,r5
    mov r0,r6
    jsr @r10
    mov r8,r4
    mov r9,r4
    bsr _kui_armada_ce_maple_bridge+(0x6374-0x67a4)
    nop
    ldc.l @r15+,gbr
    lds.l @r15+,macl
    lds.l @r15+,mach
    lds.l @r15+,pr
    mov.l @r15+,r10
    mov.l @r15+,r9
    rts
    mov.l @r15+,r8
    .align 2
.Lhelper:
#if KUI_ARMADA_CE_MAPLE_A2
    .long KUI_ARMADA_CE_VMU_HOOK_P2
#else
    .long 0xacfcd000
#endif
    .global __kui_maple_fence_bridge_code_end
__kui_maple_fence_bridge_code_end:
    .fill (92-(.-_kui_armada_ce_maple_bridge))/2,2,0x0009
    .global __kui_maple_bridge_end
__kui_maple_bridge_end:
