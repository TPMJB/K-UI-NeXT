/* SPDX-License-Identifier: GPL-3.0-only */
#define KUI_JOIN_INNER(a,b) a##b
#define KUI_JOIN(a,b) KUI_JOIN_INNER(a,b)
#define KUI_C_SYMBOL(name) KUI_JOIN(__USER_LABEL_PREFIX__,name)

    .text
    .align 2
    .global KUI_C_SYMBOL(kui_image_client_gd_call)
    .global __image_gd_vector_address
    .set __image_gd_vector_address,0x8c0000bc
KUI_C_SYMBOL(kui_image_client_gd_call):
    /* C: r4 = function, r5 = argument 0, r6 = argument 1.
     * GD BIOS: r4/r5 = arguments, r6 = 0, r7 = function. */
    sts.l pr,@-r15
    mov r4,r7
    mov r5,r4
    mov r6,r5
    mov #0,r6
    mov.l .Lgd_vector,r0
    mov.l @r0,r0
    jsr @r0
    nop
    lds.l @r15+,pr
    rts
    nop
    .align 2
    .global __image_gd_vector_literal
.Lgd_vector:
__image_gd_vector_literal: .long __image_gd_vector_address
