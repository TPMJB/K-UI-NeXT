/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse K-UI's independently audited read-only SH7750 cache-array leaf.
 * Only the computed P2 alias is called by the bounded reader. The historic
 * symbol lets the existing complete-byte cache-reader audit verify it. */
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
    .section .text.kui_armada_ce_excerpt_array_read,"ax",@progbits
    .align 2
    .global C(kui_armada_ce_excerpt_array_read)
    .type C(kui_armada_ce_excerpt_array_read),@function
C(kui_armada_ce_excerpt_array_read):
    stc sr,r1
    mov r1,r0
    or #0xf0,r0
    ldc r0,sr
    mov.l @r4,r0
    .rept 8
    nop
    .endr
    ldc r1,sr
    rts
    nop
    .size C(kui_armada_ce_excerpt_array_read),.-C(kui_armada_ce_excerpt_array_read)
