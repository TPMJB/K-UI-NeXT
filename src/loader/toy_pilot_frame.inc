/* SPDX-License-Identifier: GPL-3.0-only */
/* PRIVATE_P2 alone changes these bridges. Keep inherited native SP values
 * unchanged: authored P1 saves get a bounded RAM copy before a native cache
 * invalidator or an unmasked interrupt can discard their operand tags.
 * Callers save PR before publication; r1..r3 are caller-clobbered scratch.
 * P2 private callers are canonicalized only for the OCBP cache operation. */
#ifndef KUI_TOY_PILOT_PRIVATE_P2
#define KUI_TOY_PILOT_PRIVATE_P2 0
#endif
#if KUI_TOY_PILOT_PRIVATE_P2
#if KUI_TOY_PILOT_SHARED_SCI || defined(KUI_RETAIL_CE) || defined(KUI_RETAIL_ASYNC)
#error Private Toy frame admission requires the synchronous native SHARED0 profile
#endif
    .macro TOY_FRAME_BEGIN
    stc sr,r1
    mov #15,r2
    shll2 r2
    shll2 r2
    or r1,r2
    ldc r2,sr
    .endm

    .macro TOY_FRAME_PUBLISH bytes
    mov r15,r1
    mov r15,r2
    add #\bytes,r2
#ifdef TOY_FRAME_SHARED_PUBLISH
    mov.l .Ltoy_frame_publish_target,r3
    jsr @r3
#else
    bsr .Ltoy_frame_publish
#endif
    nop
    .endm

    /* r1 still contains the exact SR captured by TOY_FRAME_BEGIN. The
     * temporary word is popped before the native call, preserving its SP. */
    .macro TOY_FRAME_HOLD bytes
    mov.l r1,@-r15
    TOY_FRAME_PUBLISH (\bytes+4)
    .endm

    .macro TOY_FRAME_END bytes
    TOY_FRAME_HOLD \bytes
    ldc.l @r15+,sr
    .endm

    .macro TOY_FRAME_HELPER name
    .align 2
    .global \name
    .type \name,@function
\name:
.Ltoy_frame_publish:
    mov.l .Ltoy_frame_alias_mask,r3
    and r3,r1
    and r3,r2
    mov #-32,r3
    and r3,r1
1:  ocbp @r1
    add #32,r1
    cmp/hs r2,r1
    bf 1b
    rts
    nop
    .align 2
.Ltoy_frame_alias_mask: .long 0xdfffffff
    .size \name,.-\name
    .endm
#endif
