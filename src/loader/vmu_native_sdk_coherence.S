/* SPDX-License-Identifier: GPL-3.0-only */
/* Cold SDK handoff, entered through P2. No owner-stack or cached-operand
 * access precedes the idle/unlocked checks. Purge the original P1 pointer
 * and flag lines only before any TAS can acquire its P2 flag. They may be
 * distinct lines (SA2). No lock value is forced.
 * The success path then replays exactly the original initializer prefix's
 * state and resumes original code; every refusal is terminal. */
#define JI(a,b) a##b
#define J(a,b) JI(a,b)
#define C(n) J(__USER_LABEL_PREFIX__,n)
    .text
    .macro SDK_GATE name,kind
    .align 2
    .global C(\name)
    .type C(\name),@function
C(\name):
    mov.l .Lstate\@,r0
    mov.l r2,@(16,r0)
    mov.l r3,@(20,r0)
    stc sr,r2
    mov.l r2,@(24,r0)
    mov r2,r3
    mov.w .Lmask\@,r2
    /* Compare only IMASK, retaining every original SR bit in scratch. */
    and r2,r3
    cmp/eq r2,r3
    bf .Lbad_sr\@
    mov.l @(28,r0),r3
    mov.l .Lready\@,r2
    cmp/eq r2,r3
    bf .Lbad_ready\@
    mov.l @(36,r0),r3
    mov #\kind,r2
    cmp/eq r2,r3
    bf .Lbad_kind\@
    mov.l .Lmdst\@,r2
    mov.l @r2,r3
    tst r3,r3
    bf .Lbad_dma\@
    mov.l @(32,r0),r2
    mov.b @r2,r3
    extu.b r3,r3
    tst r3,r3
    bf .Lbad_p2_flag\@
    /* Only now may a P1 operand be sampled. Both copies must be zero;
     * purging after TAS would instead destroy a legitimately held lock. */
    mov.l @r0,r2
    add #4,r2
    mov.b @r2,r3
    extu.b r3,r3
    tst r3,r3
    bf .Lbad_p1_flag\@
    mov.l @r0,r2
    mov.l @r2,r3
    mov.l @(32,r0),r0
    cmp/eq r0,r3
    bf .Lbad_p1_pointer\@
    ocbp @r2
    add #4,r2
    ocbp @r2
    add #-4,r2
    mov.l .Lstate\@,r0
    mov.l .Lp2\@,r3
    or r3,r2
    mov.l @r2,r3
    mov.l @(32,r0),r0
    cmp/eq r0,r3
    bf .Lbad_ram_pointer\@
    mov.l .Lstate\@,r0
    mov.l @(32,r0),r2
    mov.b @r2,r3
    extu.b r3,r3
    tst r3,r3
    bf .Lbad_ram_flag\@
    mov.l @(44,r0),r3
    add #1,r3
    mov.l r3,@(44,r0)
    mov.l @(24,r0),r2
    ldc r2,sr
    mov.l @(16,r0),r2
    mov.l @(20,r0),r3
    /* Replay the admitted original twelve-byte prefix. Uniform R0 is
     * defined by MOV.W before use; explicit48's complete initializer first
     * defines it at+24. The call-target register R3 is likewise redefined
     * before use (in this uniform prefix, or at explicit48 initializer+16). */
    mov.l r14,@-r15
    .if \kind == 0
    mov.l @(4,r0),r14
    mov.l @(8,r0),r3
    mov r3,r2
    mov.l r13,@-r15
    /* SH-4 forbids PC-relative loads in delay slots. Load R0 first;
     * JMP captures R3 before the legal slot restores its original value. */
    mov.l @(12,r0),r3
    mov.w .Lfc4\@,r0
    jmp @r3
    mov r2,r3
    .else
    mov.l r13,@-r15
    mov.l r12,@-r15
    mov.l r11,@-r15
    mov.l r10,@-r15
    mov.l r9,@-r15
    mov.l @(12,r0),r0
    jmp @r0
    nop
    .endif
.Lbad_sr\@:
    mov.l @(24,r0),r6
    mov #0,r5
    mov.w .Lmask\@,r7
    bra .Lfault\@
    mov #1,r4
.Lbad_ready\@:
    mov r0,r5
    add #28,r5
    mov r3,r6
    mov.l .Lready\@,r7
    bra .Lfault\@
    mov #2,r4
.Lbad_kind\@:
    mov r0,r5
    add #36,r5
    mov r3,r6
    mov #\kind,r7
    bra .Lfault\@
    mov #3,r4
.Lbad_dma\@:
    mov #4,r4
    bra .Lzero_fault\@
    nop
.Lbad_p2_flag\@:
    mov #5,r4
    bra .Lzero_fault\@
    nop
.Lbad_p1_flag\@:
    mov #6,r4
    bra .Lzero_fault\@
    nop
.Lbad_p1_pointer\@:
    mov #7,r4
    bra .Lpointer_fault\@
    nop
.Lbad_ram_pointer\@:
    mov #8,r4
.Lpointer_fault\@:
    mov r2,r5
    mov r3,r6
    bra .Lfault\@
    mov r0,r7
.Lbad_ram_flag\@:
    mov #9,r4
.Lzero_fault\@:
    mov r2,r5
    mov r3,r6
    mov #0,r7
.Lfault\@:
    mov.l .Lreason\@,r0
    or r0,r4
    mov.l .Lterminal\@,r0
    jmp @r0
    nop
    .align 1
.Lmask\@: .word 0x00f0
    .if \kind == 0
.Lfc4\@: .word 0x0fc4
    .endif
    .align 2
.Lstate\@: .long C(kui_vmu_native_sdk_coherence_state)
.Lready\@: .long 0x4e434f31
.Lmdst\@: .long 0xa05f6c18
.Lp2\@: .long 0xa0000000
.Lreason\@: .long 0x00020000
.Lterminal\@: .long C(kui_vmu_native_runtime_fault_hook)
    .size C(\name),.-C(\name)
    .endm
    SDK_GATE kui_vmu_native_sdk_coherence_uniform_gate,0
    SDK_GATE kui_vmu_native_sdk_coherence_variable_gate,1
