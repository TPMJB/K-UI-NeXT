/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_loader_layout.h"
#define JOIN_INNER(a,b) a##b
#define JOIN(a,b) JOIN_INNER(a,b)
#define C_SYMBOL(name) JOIN(__USER_LABEL_PREFIX__,name)

/* Called by the high stage before entering the owner's upper IP bootstrap.
 * Do not switch stacks here: state adoption/init use the temporary high stack.
 * R4..R7 hold all four C arguments and remain intact across BSS clearing. */
    .section .entry,"ax"
    .align 2
    .global _start
_start:
    mov.l .Lbss_begin,r0
    mov.l .Lbss_end,r1
    mov #0,r2
.Lzero_bss:
    cmp/hs r1,r0
    bt .Linitialized
    mov.l r2,@r0
    add #4,r0
    bra .Lzero_bss
    nop
.Linitialized:
    mov.l .Linit,r0
    jmp @r0
    nop
    .align 2
.Lbss_begin: .long __retail_resident_bss_begin
.Lbss_end: .long __retail_resident_bss_end
.Linit: .long C_SYMBOL(kui_retail_resident_init)

/* Save original SR on the caller's stack, then mask BEFORE examining the
 * entry lock or touching the single resident stack. This also rejects direct
 * recursion without replacing an active service frame. No FPU instruction or
 * register-bank change occurs. PR and all ABI callee-saved GPRs are preserved.
 * Synchronous SD chunks run masked; the caller's exact SR is restored. */
    .section .text.retail_hook,"ax"
    .align 2
    .global C_SYMBOL(kui_retail_resident_hook)
C_SYMBOL(kui_retail_resident_hook):
    mov #0,r0
    bra .Lgd_common
    nop
    .global C_SYMBOL(kui_retail_gd_c0_hook)
C_SYMBOL(kui_retail_gd_c0_hook):
    mov #1,r0
    bra .Lgd_common
    nop
    .global C_SYMBOL(kui_retail_gd_1000_hook)
C_SYMBOL(kui_retail_gd_1000_hook):
    mov #2,r0
    bra .Lgd_common
    nop
    .global C_SYMBOL(kui_retail_gd_10f0_hook)
C_SYMBOL(kui_retail_gd_10f0_hook):
    mov #3,r0
.Lgd_common:
    stc sr,r1
    mov.l r1,@-r15
    mov.l .Lirq_mask,r2
    or r2,r1
    ldc r1,sr
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
    mov r0,r9
    mov r15,r8
    mov.l .Lactive,r0
    mov.l @r0,r1
    tst r1,r1
    bf .Lbusy
    mov.l .Lguard,r1
    mov.l @r1,r2
    mov.l .Lguard_value,r3
    cmp/eq r2,r3
    bf .Lguard_fault
    mov #1,r1
    mov.l r1,@r0
    mov.l .Lsource,r0
    mov.l r9,@r0
    /* Publish the caller's original SR (saved above PR and R8..R14) for
     * diagnostics: IMASK/BL show whether the game called from a handler. */
    mov.l @(32,r8),r1
    mov.l .Lcaller_sr,r0
    mov.l r1,@r0
    mov.l .Lprivate_stack,r15
    mov.l .Ldispatch,r0
    jsr @r0
    nop
    mov r0,r9
    mov.l .Lguard,r1
    mov.l @r1,r2
    mov.l .Lguard_value,r3
    cmp/eq r2,r3
    bt .Lunlock
    mov.l .Lfault,r1
    mov #1,r2
    mov.l r2,@r1
    mov #-1,r9
.Lunlock:
    mov.l .Lactive,r1
    mov #0,r2
    mov.l r2,@r1
    mov r9,r0
    bra .Lreturn
    nop
.Lbusy:
    mov r7,r0
    cmp/eq #0,r0
    bt .Lreturn
    cmp/eq #1,r0
    bf .Lerror
    mov #4,r0
    bra .Lreturn
    nop
.Lguard_fault:
    mov.l .Lfault,r1
    mov #1,r2
    mov.l r2,@r1
.Lerror:
    mov #-1,r0
.Lreturn:
    mov r8,r15
    mov.l @r15+,r14
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    lds.l @r15+,pr
    mov.l @r15+,r1
    ldc r1,sr
    rts
    nop
    .align 2
.Lirq_mask: .long 0x100000f0
.Lactive: .long C_SYMBOL(kui_retail_hook_active)
.Lfault: .long C_SYMBOL(kui_retail_hook_fault)
.Lprivate_stack: .long KUI_RETAIL_HOOK_STACK-16
.Lguard: .long KUI_RETAIL_HOOK_STACK_BOTTOM
.Lguard_value: .long 0x4b554947
.Ldispatch: .long C_SYMBOL(kui_retail_resident_dispatch)
.Lsource: .long C_SYMBOL(kui_retail_hook_source)
.Lcaller_sr: .long C_SYMBOL(kui_retail_hook_sr)

/* Menu command 1 (return to the BIOS menu; A+B+X+Y+Start in most games)
 * shows the reader's counters, then restarts the console so the boot disc
 * starts K-UI again. Other menu functions still forward to firmware. Use our
 * private stack even if the caller was in a dying game. */
    .align 2
    .global C_SYMBOL(kui_retail_menu_hook)
C_SYMBOL(kui_retail_menu_hook):
    mov r4,r0
    cmp/eq #1,r0
    bt .Lmenu_stop
    mov.l .Lmenu_original,r0
    mov.l @r0,r0
    jmp @r0
    nop
.Lmenu_stop:
    stc sr,r0
    mov.l .Lmenu_mask,r1
    or r1,r0
    ldc r0,sr
    sts pr,r5
    mov r15,r6
    mov.l .Lmenu_stack,r15
    mov.l .Lmenu_report,r0
    jmp @r0
    nop
    .align 2
.Lmenu_original: .long C_SYMBOL(kui_retail_original_menu)
.Lmenu_mask: .long 0x100000f0
.Lmenu_stack: .long KUI_RETAIL_HOOK_STACK-16
.Lmenu_report: .long C_SYMBOL(kui_retail_menu_return)

#ifdef KUI_RETAIL_ASYNC
/* Vector templates for the background reader, copied by
 * kui_retail_async_init into its region (VBR = region - 0x100). They run in
 * exception state (BL=1, RB=1) and leave every register, SSR, SPC and SGR as
 * the hardware set them for the game's handler: R0 is parked in DBR and put
 * back in the jump's delay slot. DBR serves only H-UDI/user-break debugging,
 * which games do not use. Word 3 of the forwarding template and word 10 of
 * the interrupt template name the slot (engine.forward) holding where events
 * go: the game's vector, or a releasing entry below. */
    .section .text.retail_vectors,"ax"
    .align 2
    .global C_SYMBOL(kui_retail_vector_forward)
C_SYMBOL(kui_retail_vector_forward):
    ldc r0,dbr
    mov.l 1f,r0
    mov.l @r0,r0
    jmp @r0
    stc dbr,r0
    nop
1:  .long 0
    .align 2
    .global C_SYMBOL(kui_retail_vector_interrupt)
C_SYMBOL(kui_retail_vector_interrupt):
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
    mov.l @r0,r0
    jmp @r0
    stc dbr,r0
1:  mov.l 4f,r0
    jmp @r0
    stc dbr,r0
    .align 2
2:  .long 0xff000028 /* INTEVT */
3:  .long 0
4:  .long C_SYMBOL(kui_retail_irq_entry)

/* The stream's interrupt. GD calls run with BL set, so the private stack is
 * free here; the interrupted R15 is in SGR. Bank-1 R0..R7, PR and MAC are
 * the game's and are saved; R8..R14 are preserved by the C handler. A
 * nonzero result passes the event to the game's vector unchanged. */
    .section .text.retail_irq,"ax"
    .align 2
    .global C_SYMBOL(kui_retail_irq_entry)
C_SYMBOL(kui_retail_irq_entry):
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
    mov.l .Lirq_slot,r0
    mov.l @r0,r0
    mov.l @r0,r0
    jmp @r0
    stc dbr,r0
    .align 2
.Lirq_stack: .long KUI_RETAIL_HOOK_STACK-16
.Lirq_handler: .long C_SYMBOL(kui_retail_async_irq)
.Lirq_guard: .long KUI_RETAIL_HOOK_STACK_BOTTOM
.Lirq_guard_value: .long 0x4b554947
.Lirq_fault: .long C_SYMBOL(kui_retail_hook_fault)
/* Word 10 of the copied interrupt vector: the 0x600 slot. */
.Lirq_slot: .long C_SYMBOL(kui_retail_async_region)+0x500+40

/* Releasing entries (launch X): an event that is not the reader's gives the
 * game its VBR and its SCI level back before entering the game's vector, so
 * the game's handler never runs under the reader's VBR and the reader's
 * interrupt cannot reach the game; the next GD call installs them again.
 * Entered like the game's vector (R0 restored from DBR); R0 is parked in
 * DBR again and R1..R3 in the engine's release frame (region + 0x240:
 * game VBR, SCI field, R1..R3, target), so every register reaches the game
 * unchanged. BL=1 throughout, so the frame is never shared. */
    .align 2
    .global C_SYMBOL(kui_retail_release_100)
C_SYMBOL(kui_retail_release_100):
    ldc r0,dbr
    mov.l .Lrelease_frame,r0
    mov.l r1,@(8,r0)
    mov.w .Lrelease_100,r1
    bra .Lrelease
    nop
    .global C_SYMBOL(kui_retail_release_400)
C_SYMBOL(kui_retail_release_400):
    ldc r0,dbr
    mov.l .Lrelease_frame,r0
    mov.l r1,@(8,r0)
    mov.w .Lrelease_400,r1
    bra .Lrelease
    nop
    .global C_SYMBOL(kui_retail_release_600)
C_SYMBOL(kui_retail_release_600):
    ldc r0,dbr
    mov.l .Lrelease_frame,r0
    mov.l r1,@(8,r0)
    mov.w .Lrelease_600,r1
.Lrelease:
    mov.l r2,@(12,r0)
    mov.l r3,@(16,r0)
    mov.l @r0,r2
    add r2,r1
    mov.l r1,@(20,r0)
    ldc r2,vbr
    mov.l .Lrelease_iprb,r1
    mov.w @r1,r2
    mov.l .Lrelease_mask,r3
    and r3,r2
    mov.l @(4,r0),r3
    or r3,r2
    mov.w r2,@r1
    mov.w @r1,r2 /* the level is down before the game's handler runs */
    mov.l @(16,r0),r3
    mov.l @(12,r0),r2
    mov.l @(8,r0),r1
    mov.l @(20,r0),r0
    jmp @r0
    stc dbr,r0
.Lrelease_100: .word 0x100
.Lrelease_400: .word 0x400
.Lrelease_600: .word 0x600
    .align 2
.Lrelease_frame: .long C_SYMBOL(kui_retail_async_region)+0x240
.Lrelease_iprb: .long 0xffd00008
.Lrelease_mask: .long 0xffffff0f
#endif
