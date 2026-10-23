/* SPDX-License-Identifier: GPL-3.0-only */
/* Entered only from the witnessed general exception vector. Its first eight
 * bytes save entry R0 in R15; hardware SGR still holds interrupted R15. No
 * instruction borrows the interrupted stack, even if the exception concerns
 * that stack. SR.BL is already one, so no ordinary IRQ can nest this scratch.
 * Normal exceptions reach the original vector with every register restored.
 * Only a selected, still-default SDK exception becomes a terminal report. */
#define JI(a,b) a##b
#define J(a,b) JI(a,b)
#define C(n) J(__USER_LABEL_PREFIX__,n)
    .text
    .align 2
    .global C(kui_vmu_native_fault_capture_gateway)
    .type C(kui_vmu_native_fault_capture_gateway),@function
C(kui_vmu_native_fault_capture_gateway):
    mov.l .Lstate,r0
    mov.l r15,@r0
    mov.l r1,@(4,r0)
    stc sr,r1
    mov.l r1,@(8,r0)
    mov.l .Lexpevt,r1
    mov.l @r1,r1
    mov.w .Lread_address,r0
    cmp/eq r0,r1
    bt .Lselected
    mov.w .Lwrite_address,r0
    cmp/eq r0,r1
    bt .Lselected
    mov.w .Lillegal,r0
    cmp/eq r0,r1
    bt .Lselected
    mov.w .Lslot_illegal,r0
    cmp/eq r0,r1
    bf .Lchain
.Lselected:
    /* SDK table: VBR+0x200+((EXPEVT-0x200)>>3). A game which registers a
     * real recovery handler retains that handler, even for a selected code. */
    mov.w .Ltable_bias,r0
    sub r0,r1
    shar r1
    shar r1
    shar r1
    add r0,r1
    stc vbr,r0
    add r0,r1
    mov.l @r1,r1
    mov.l .Lstate,r0
    mov.l @(20,r0),r0
    cmp/eq r0,r1
    bf .Lchain
    /* Terminal failure ABI already owns the canonical runtime stack. Read
     * actual hardware exception state, not an interrupted IRQ checkpoint. */
    mov.l .Lexpevt,r0
    mov.l @r0,r4
    mov.l .Lreason,r0
    or r0,r4
    stc spc,r5
    stc ssr,r6
    mov.l .Ltea,r0
    mov.l @r0,r7
    mov.l .Lreport,r0
    jmp @r0
    nop
.Lchain:
    mov.l .Lstate,r0
    mov.l @(8,r0),r1
    ldc r1,sr
    mov.l @(16,r0),r15
    mov.l @(4,r0),r1
    mov.l @r0,r0
    jmp @r15
    stc sgr,r15
    .align 1
.Lread_address: .word 0x00e0
.Lwrite_address: .word 0x0100
.Lillegal: .word 0x0180
.Lslot_illegal: .word 0x01a0
.Ltable_bias: .word 0x0200
    .align 2
.Lstate: .long C(kui_vmu_native_fault_capture_state)
.Lexpevt: .long 0xff000024
.Ltea: .long 0xff00000c
.Lreason: .long 0x00010000
.Lreport: .long C(kui_vmu_native_runtime_fault_hook)
    .size C(kui_vmu_native_fault_capture_gateway),.-C(kui_vmu_native_fault_capture_gateway)

    /* One-time installer publication. OCBP first writes each changed P1
     * operand-cache line back; only then invalidate instructions from P2.
     * This is the same cache handoff as the temporary stage publisher. */
    .align 2
    .global C(kui_vmu_native_fault_capture_publish)
    .type C(kui_vmu_native_fault_capture_publish),@function
C(kui_vmu_native_fault_capture_publish):
    mov.l .Lpublish_p2,r0
    jmp @r0
    nop
    .align 2
.Lpublish_p2:
#if KUI_VMU_NATIVE_CACHED_CODE
    .long .Lpublish_body+0x20000000
#else
    .long .Lpublish_body
#endif
.Lpublish_body:
    mov #-32,r0
    and r0,r4
.Lpublish_loop:
    cmp/hs r5,r4
    bt .Lpublish_ici
    ocbp @r4
    add #32,r4
    bra .Lpublish_loop
    nop
.Lpublish_ici:
    mov.l .Lpublish_ccr,r0
    mov.l @r0,r1
    mov.l .Lpublish_mask,r2
    or r2,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    rts
    nop
    .align 2
.Lpublish_ccr: .long 0xff00001c
.Lpublish_mask: .long 0x00000800
    .size C(kui_vmu_native_fault_capture_publish),.-C(kui_vmu_native_fault_capture_publish)
