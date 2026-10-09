/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact-title high SCI vectors and forwarding. This is adapted from K-UI's
 * authored native retail vector handoff; no external loader code is used. */
#if !KUI_TOY_PILOT_SHARED_SCI
#error High SCI vectors require the separate shared Toy build
#endif
#define C_SYMBOL(name) _ ## name
    .section .text.toy_sci_vectors,"ax"
    .align 2
    .global C_SYMBOL(kui_toy_pilot_sci_forward)
C_SYMBOL(kui_toy_pilot_sci_forward):
    ldc r0,dbr
    mov.l 1f,r0
    jmp @r0
    nop
1:  .long 0
    .align 2
    .global C_SYMBOL(kui_toy_pilot_sci_interrupt)
C_SYMBOL(kui_toy_pilot_sci_interrupt):
    ldc r0,dbr
    mov.l 2f,r0
    mov.l @r0,r0
    shlr2 r0
    shlr2 r0
    shlr r0
    cmp/eq #0x27,r0 /* ERI, INTEVT 0x4e0: SCI receive error */
    bt 1f
    cmp/eq #0x28,r0 /* RXI, 0x500: SCI receive (masked by EIO during DMA) */
    bt 1f
    mov.l 3f,r0
    jmp @r0
    nop
1:  mov.l 4f,r0
    jmp @r0
    stc dbr,r0
    .align 2
2:  .long 0xff000028 /* INTEVT */
3:  .long C_SYMBOL(kui_toy_pilot_sci_release_600)
4:  .long C_SYMBOL(kui_toy_pilot_sci_irq_entry)

/* The stream's interrupt uses its OWN high stack. Neither the audio worker
 * nor GD bridge can be overwritten; interrupted R15 is in SGR. Bank-1 R0..R7, PR and MAC are
 * the game's and are saved; R8..R14 are preserved by the C handler. A
 * nonzero result passes the event on as any other (a releasing entry). */
    .section .text.toy_sci_irq,"ax"
    .align 2
    .global C_SYMBOL(kui_toy_pilot_sci_irq_entry)
C_SYMBOL(kui_toy_pilot_sci_irq_entry):
    mov.l .Lirq_stack,r15
    mov.l r0,@-r15
    mov.l r1,@-r15
    mov.l r2,@-r15
    mov.l r3,@-r15
    mov.l r4,@-r15
    mov.l r5,@-r15
    mov.l r6,@-r15
    mov.l r7,@-r15
    sts.l pr,@-r15
    sts.l mach,@-r15
    sts.l macl,@-r15
    mov.l .Lirq_handler,r0
    jsr @r0
    nop
    mov.l .Lirq_guard,r1
    mov.l @r1,r2
    mov.l .Lirq_guard_value,r3
    cmp/eq r2,r3
    bt .Lirq_guarded
    mov.l .Lirq_fault,r1
    mov #1,r2
    mov.l r2,@r1
.Lirq_guarded:
    tst r0,r0
    lds.l @r15+,macl
    lds.l @r15+,mach
    lds.l @r15+,pr
    mov.l @r15+,r7
    mov.l @r15+,r6
    mov.l @r15+,r5
    mov.l @r15+,r4
    mov.l @r15+,r3
    mov.l @r15+,r2
    mov.l @r15+,r1
    mov.l @r15+,r0
    stc sgr,r15
    bf .Lirq_forward
    rte
    nop
.Lirq_forward:
    ldc r0,dbr
    mov.l .Lirq_release,r0
    jmp @r0
    nop
    .align 2
.Lirq_stack: .long __toy_sci_irq_stack_top-16
.Lirq_handler: .long C_SYMBOL(kui_toy_pilot_sci_irq)
.Lirq_guard: .long __toy_sci_irq_stack_bottom
.Lirq_guard_value: .long 0xa55a5aa5
.Lirq_fault: .long C_SYMBOL(kui_toy_pilot_sci_irq_fault)
.Lirq_release: .long C_SYMBOL(kui_toy_pilot_sci_release_600)

/* Releasing entries: an event that is not the reader's first gives the game
 * its VBR and its SCI level back, then enters the game's vector, so the
 * game's handler never runs under the reader's VBR and the reader's
 * interrupt cannot reach the game's vectors. Entered from the vectors with
 * the game's R0 in DBR and R15 the interrupted stack, on which R1..R3 are
 * kept meanwhile; everything reaches the game's vector unchanged. With
 * launch Y (release.armed), an interrupt's handler also returns through
 * kui_toy_pilot_sci_rehook: its SPC and SGR are kept in a free pending slot and SPC
 * names the trampoline. Offsets into the high release frame:
 * vbr 0, sci 4, armed 8, depth 12, pending 16 + 8 * slot (SPC, SGR),
 * rehooks 40, released[3] 44. */
    .align 2
    .global C_SYMBOL(kui_toy_pilot_sci_release_100)
C_SYMBOL(kui_toy_pilot_sci_release_100):
    mov.l r1,@-r15
    mov.l r2,@-r15
    mov.w .Lrelease_100,r2
    bra .Lrelease
    mov #44,r1
    .global C_SYMBOL(kui_toy_pilot_sci_release_400)
C_SYMBOL(kui_toy_pilot_sci_release_400):
    mov.l r1,@-r15
    mov.l r2,@-r15
    mov.w .Lrelease_400,r2
    bra .Lrelease
    mov #48,r1
    .global C_SYMBOL(kui_toy_pilot_sci_release_600)
C_SYMBOL(kui_toy_pilot_sci_release_600):
    mov.l r1,@-r15
    mov.l r2,@-r15
    mov.w .Lrelease_600,r2
    mov #52,r1
.Lrelease:
    mov.l r3,@-r15
    mov.l .Lrelease_frame,r0
    mov.l @(r0,r1),r3
    add #1,r3
    mov.l r3,@(r0,r1)
    mov #52,r3
    cmp/eq r3,r1
    bf .Lrelease_vbr /* an exception: its handler may need SPC as it is */
    mov.l @(8,r0),r3
    tst r3,r3
    bt .Lrelease_vbr
    stc ssr,r3
    shll r3
    shll r3
    bf .Lrelease_vbr /* user mode: the trampoline could not run there */
    mov.l @(12,r0),r3
    mov #3,r1
    cmp/hs r1,r3
    bt .Lrelease_vbr /* every pending slot taken: just release */
    add #1,r3
    mov.l r3,@(12,r0)
    shll2 r3
    shll r3
    add r0,r3
    stc spc,r1
    mov.l r1,@(8,r3)
    stc sgr,r1
    mov.l r1,@(12,r3)
    mov.l .Lrelease_return,r1
    ldc r1,spc
.Lrelease_vbr:
    mov.l @r0,r3
    ldc r3,vbr
    add r3,r2
    mov.l @(4,r0),r1
    mov.l .Lrelease_iprb,r3
    mov.w @r3,r0
    xor r1,r0
    and #0xf0,r0
    mov.w @r3,r1
    xor r0,r1
    mov.w r1,@r3
    mov.w @r3,r1 /* the level is down before the game's handler runs */
    mov.l @r15+,r3
    mov r2,r0
    mov.l @r15+,r2
    mov.l @r15+,r1
    jmp @r0
    stc dbr,r0
.Lrelease_100: .word 0x100
.Lrelease_400: .word 0x400
.Lrelease_600: .word 0x600
    .align 2
.Lrelease_frame: .long C_SYMBOL(kui_toy_pilot_sci_release)
.Lrelease_iprb: .long 0xffd00008
.Lrelease_return: .long C_SYMBOL(kui_toy_pilot_sci_rehook)

/* The re-hook trampoline (launch Y). A released interrupt's handler returns
 * here through RTE, in the interrupted context: SR as the handler restored
 * it, every register the interrupted code's. Four of them go on its stack
 * while SR.BL and IMASK 15 are raised. Returns come back newest first (an
 * interrupt nested in a released handler returns before it), so the newest
 * pending slot gives the interrupted PC; its SGR must be this stack, or
 * something switched stacks under it and nothing here can resume safely, so
 * it stops. If the stream still runs and VBR is still the one released to,
 * the reader's VBR and SCI level are installed again. RTE then resumes the
 * interrupted code with its own SR. */
    .align 2
    .global C_SYMBOL(kui_toy_pilot_sci_rehook)
C_SYMBOL(kui_toy_pilot_sci_rehook):
    mov.l r0,@-r15
    mov.l r1,@-r15
    mov.l r2,@-r15
    mov.l r3,@-r15
    stc sr,r3
    mov.l .Lrehook_block,r0
    or r3,r0
    ldc r0,sr
    ldc r3,ssr
    mov.l .Lrehook_frame,r0
    mov.l @(12,r0),r1
    mov r1,r2
    shll2 r2
    shll r2
    add r0,r2
    mov.l @(8,r2),r3
    ldc r3,spc
    mov.l @(12,r2),r2
    mov r15,r3
    add #16,r3
    cmp/eq r3,r2
    bf . /* not this stack's return (or none pending): stop */
    add #-1,r1
    mov.l r1,@(12,r0)
    mov.l @(8,r0),r1
    tst r1,r1
    bt 5f /* the stream ended meanwhile */
    stc vbr,r1
    mov.l @r0,r2
    cmp/eq r2,r1
    bf 5f /* already installed, or the game moved its VBR */
    mov.l @(40,r0),r1
    add #1,r1
    mov.l r1,@(40,r0)
    mov.l .Lrehook_vbr,r1
    ldc r1,vbr
    mov.l .Lrehook_iprb,r1
    mov.w @r1,r0
    xor #0x10,r0
    and #0xf0,r0
    mov.w @r1,r2
    xor r0,r2
    mov.w r2,@r1
    mov.w @r1,r2
5:  mov.l @r15+,r3
    mov.l @r15+,r2
    mov.l @r15+,r1
    rte
    mov.l @r15+,r0
    .align 2
.Lrehook_block: .long 0x100000f0
.Lrehook_frame: .long C_SYMBOL(kui_toy_pilot_sci_release)
.Lrehook_vbr: .long C_SYMBOL(kui_toy_pilot_sci_vectors)-0x100
.Lrehook_iprb: .long 0xffd00008

    .section .bss.toy_sci_irq_stack,"aw",@nobits
    .balign 32
    .global __toy_sci_irq_stack_bottom
__toy_sci_irq_stack_bottom:
    .space 2048
    .global __toy_sci_irq_stack_top
__toy_sci_irq_stack_top:
