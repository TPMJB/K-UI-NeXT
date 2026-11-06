/* SPDX-License-Identifier: GPL-3.0-only */
/* An image launcher's temporary installer may occupy the exact startup's
 * zeroed heap. Restore this wrapper's entire frame before jumping to the
 * reviewed stackless original clear; its RTS then reaches retained low code.
 * No instruction in this image or access to its stack follows the tail jump. */
#include "kui/native_isolation.h"
#if KUI_NATIVE_CURRENT_BOTTOM_HEAP && !KUI_NATIVE_PHYSICAL_DISC
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)

    .section .text.native_preclear_tail,"ax"
    .align 2
    .global __kui_native_preheap_clear_tail
    .set __kui_native_preheap_clear_tail,1
    .global C(kui_native_preclear_stage_install)
    .type C(kui_native_preclear_stage_install),@function
C(kui_native_preclear_stage_install):
    sts.l pr,@-r15
    mov.l .Linstall,r0
    jsr @r0
    nop
    /* The checker admits the saved incoming low continuation, exact pending
     * tuple and inherited masked context while high code/stack still exist. */
    mov.l @r15,r4
    mov.l .Lfinish,r0
    jsr @r0
    nop
    lds.l @r15+,pr
    mov r0,r2
    mov.l @r2,r3
    mov.l @(4,r2),r4
    mov.l @(8,r2),r6
    mov #0,r5
    mov.l r5,@r2
    mov.l r5,@(4,r2)
    mov.l r5,@(8,r2)
    /* Finish consuming all four P2 words before the original helper's first
     * instruction. It preserves R8-R15 and PR, including the low frame base. */
    jmp @r3
    mov.l r5,@(12,r2)
    .align 2
.Linstall: .long C(kui_vmu_observe_stage_install)
.Lfinish: .long C(kui_native_preclear_finish)
    .size C(kui_native_preclear_stage_install),.-C(kui_native_preclear_stage_install)
#endif
