/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/image_loader_layout.h"
#define KUI_JOIN_INNER(a,b) a##b
#define KUI_JOIN(a,b) KUI_JOIN_INNER(a,b)
#define KUI_C_SYMBOL(name) KUI_JOIN(__USER_LABEL_PREFIX__,name)

    .section .entry,"ax"
    .align 2
    .global _start
_start:
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
.Lhalt:
    bra .Lhalt
    nop
    .align 2
.Lstack:     .long KUI_IMAGE_RESIDENT_STACK
.Lbss_start: .long __bss_start
.Lbss_end:   .long __bss_end
.Lmain:      .long KUI_C_SYMBOL(kui_image_resident_main)

    .section .client_image,"a"
    .align 2
    .global __client_image_start
    .global __client_image_end
__client_image_start:
    .incbin "build/loader/image_client.bin"
    .align 2
__client_image_end:
