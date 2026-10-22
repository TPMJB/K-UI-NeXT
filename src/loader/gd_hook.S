/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/image_loader_layout.h"
#define KUI_JOIN_INNER(a,b) a##b
#define KUI_JOIN(a,b) KUI_JOIN_INNER(a,b)
#define KUI_C_SYMBOL(name) KUI_JOIN(__USER_LABEL_PREFIX__,name)

/* Firmware vector 0x8c0000bc is called with r4/r5 arguments, r6 subsystem
 * (zero for GD), and r7 selector. Match the actual SH C register convention
 * at the C dispatch boundary. The service stack is disjoint from both the
 * client stack and the resident main stack. Interrupts stay masked in this
 * correctness probe; this wrapper is deliberately not a retail IRQ hook. */
    .section .text.gd_hook,"ax"
    .align 2
    .global KUI_C_SYMBOL(kui_image_gd_hook)
    .global __image_gd_hook_stack
    .set __image_gd_hook_stack,KUI_IMAGE_HOOK_STACK
KUI_C_SYMBOL(kui_image_gd_hook):
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
    mov r15,r8
    mov.l .Lhook_stack,r15
    mov.l .Ldispatch,r0
    jsr @r0
    nop
    mov r8,r15
    mov.l @r15+,r14
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    lds.l @r15+,pr
    rts
    nop
    .align 2
.Lhook_stack: .long __image_gd_hook_stack
.Ldispatch: .long KUI_C_SYMBOL(kui_image_gd_dispatch)
