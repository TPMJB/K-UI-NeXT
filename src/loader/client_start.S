/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/loader_layout.h"
#define KUI_JOIN_INNER(a,b) a##b
#define KUI_JOIN(a,b) KUI_JOIN_INNER(a,b)
#define KUI_C_SYMBOL(name) KUI_JOIN(__USER_LABEL_PREFIX__,name)

    .section .entry,"ax"
    .align 2
    .global _start
_start:
    /* Save the resident call frame, then give the independent client its own
     * stack. r8 is callee-saved by the SH ABI; all client C observes that ABI. */
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov r15,r8
    mov.l .Lstack,r15
    mov.l .Lbss_start,r0
    mov.l .Lbss_end,r1
    mov #0,r2
.Lbss:
    cmp/hs r1,r0
    bt .Lready
    mov.l r2,@r0
    add #4,r0
    bra .Lbss
    nop
.Lready:
    mov.l .Lmain,r0
    jsr @r0
    nop
    mov r8,r15
    mov.l @r15+,r8
    lds.l @r15+,pr
    rts
    nop
    .align 2
.Lstack:     .long KUI_LOADER_CLIENT_STACK
.Lbss_start: .long __bss_start
.Lbss_end:   .long __bss_end
.Lmain:      .long KUI_C_SYMBOL(kui_probe_client_main)
