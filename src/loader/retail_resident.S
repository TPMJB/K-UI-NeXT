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
#ifdef KUI_RETAIL_CE
/* KUI_RETAIL_CE_KERNEL: where Windows CE's kernel keeps its interrupt
 * bookkeeping (pending mask, ring head, reschedule flag) and its interrupt
 * handler table, written here by the stage, which finds them in the loaded
 * kernel; zero if they were not found. */
    .global C_SYMBOL(kui_retail_ce_kernel)
C_SYMBOL(kui_retail_ce_kernel): .long 0,0,0,0
#endif

/* Save original SR on the caller's stack, then mask BEFORE examining the
 * entry lock or touching the single resident stack. This also rejects direct
 * recursion without replacing an active service frame. No FPU instruction or
 * register-bank change occurs. PR and all ABI callee-saved GPRs are preserved.
 * Synchronous SD chunks run masked; the caller's exact SR is restored.
 * KUI_RETAIL_CE (the Windows CE boot test): CE calls with its MMU on and its
 * stack at a virtual address, where a TLB miss with BL set resets the
 * console. That build touches the caller's stack only with the caller's own
 * SR: it masks after the pushes and restores SR before the pops. It masks
 * interrupts only, leaving SR.BL clear throughout (see .Lirq_mask). */
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
#ifndef KUI_RETAIL_CE
    mov.l .Lirq_mask,r2
    or r2,r1
    ldc r1,sr
#endif
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
#ifdef KUI_RETAIL_CE
    mov.l .Lirq_mask,r2
    or r2,r1
    ldc r1,sr
#endif
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
#ifdef KUI_RETAIL_CE
    /* The caller's return address and stack, for the CE trace. */
    mov.l @(28,r8),r1
    mov.l .Lcaller,r0
    mov.l r1,@r0
    mov r8,r1
    add #36,r1
    mov.l r1,@(4,r0)
#endif
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
#ifdef KUI_RETAIL_CE
    mov.l .Ldeferred,r1
    mov.l @r1,r2
    tst r2,r2
    bf .Ldeferred_call
#endif
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
#ifdef KUI_RETAIL_CE
    mov.l @(32,r15),r1
    ldc r1,sr
#endif
    mov.l @r15+,r14
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    lds.l @r15+,pr
    mov.l @r15+,r1
#ifndef KUI_RETAIL_CE
    ldc r1,sr
#endif
    rts
    nop
#ifdef KUI_RETAIL_CE
/* Windows CE's PIO stream: the BIOS calls the CD driver's callback when a
 * PIO transfer ends. The service marks it due; it is made here, after the
 * lock is released, in the caller's own context (its SR, stack and R8..R14
 * restored, as if the firmware had called it within this call), and the
 * call's result is then returned as usual. Microsoft's SH convention gives
 * a callee 16 bytes at the top of the stack to keep R4..R7; GBR is kept. */
.Ldeferred_call:
    mov #0,r3
    mov.l r3,@r1
    mov.l @(4,r1),r4
    mov r8,r15
    mov.l @(32,r15),r1
    ldc r1,sr
    mov.l @r15+,r14
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    lds.l @r15+,pr
    mov.l @r15+,r1
    sts.l pr,@-r15
    mov.l r0,@-r15
    stc.l gbr,@-r15
    add #-16,r15
    jsr @r2
    nop
    add #16,r15
    ldc.l @r15+,gbr
    mov.l @r15+,r0
    lds.l @r15+,pr
    rts
    nop
#endif
    .align 2
#ifdef KUI_RETAIL_CE
/* Windows CE: interrupts masked, SR.BL left clear, so a TLB miss on CE's
 * virtual memory (the service reads and writes it directly) goes to CE's own
 * handler instead of resetting the console. */
.Lirq_mask: .long 0x000000f0
#else
.Lirq_mask: .long 0x100000f0
#endif
.Lactive: .long C_SYMBOL(kui_retail_hook_active)
.Lfault: .long C_SYMBOL(kui_retail_hook_fault)
.Lprivate_stack: .long KUI_RETAIL_HOOK_STACK-16
.Lguard: .long KUI_RETAIL_HOOK_STACK_BOTTOM
.Lguard_value: .long 0x4b554947
.Ldispatch: .long C_SYMBOL(kui_retail_resident_dispatch)
.Lsource: .long C_SYMBOL(kui_retail_hook_source)
.Lcaller_sr: .long C_SYMBOL(kui_retail_hook_sr)
#ifdef KUI_RETAIL_CE
.Lcaller: .long C_SYMBOL(kui_retail_hook_caller)
.Ldeferred: .long C_SYMBOL(kui_retail_ce_deferred)
#endif

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
#ifdef KUI_RETAIL_CE
    /* Windows CE's CD driver calls function 2, the BIOS check that the disc
     * in the drive is still the one that booted. The BIOS would ask the
     * physical drive, holding K-UI's own disc or none: the reader answers
     * for the image instead, through the common entry as source 4. */
    cmp/eq #2,r0
    bf .Lmenu_forward
    bra .Lgd_common
    mov #4,r0
.Lmenu_forward:
#endif
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

#if defined(KUI_RETAIL_ASYNC) && defined(KUI_RETAIL_CE)
/* Windows CE's background reader. CE's interrupt dispatch calls this for
 * the SCI's events through the entries of its handler table the reader
 * installs (retail_async.c): SR.BL set, register bank 1, R15 the
 * interrupted stack (perhaps a virtual address: never touched here) and R7
 * the dispatcher's own PR. CE's handlers use R0..R3 alone; R4..R7, PR, MACH
 * and MACL are kept here and R8..R14 by the C handler, which runs on the
 * private stack: no GD call is using it, as they mask interrupts. R0
 * returns the SYSINTR for CE's dispatch to report, or 0. Should CE take an
 * interrupt inside a GD call anyway (an exception path that unmasks), the
 * SCI's level is only dropped, without a stack; the call raises it again. */
    .section .text.retail_irq,"ax"
    .align 2
    .global C_SYMBOL(kui_retail_ce_isr)
C_SYMBOL(kui_retail_ce_isr):
    mov.l .Lce_isr_active,r0
    mov.l @r0,r0
    tst r0,r0
    bf .Lce_isr_held
    mov.l .Lce_isr_sp,r0
    mov.l r15,@r0
    mov.l .Lce_isr_stack,r15
    mov.l r4,@-r15
    mov.l r5,@-r15
    mov.l r6,@-r15
    mov.l r7,@-r15
    sts.l pr,@-r15
    sts.l mach,@-r15
    sts.l macl,@-r15
    mov.l .Lce_isr_handler,r0
    jsr @r0
    nop
    mov.l .Lce_isr_guard,r1
    mov.l @r1,r2
    mov.l .Lce_isr_guard_value,r3
    cmp/eq r2,r3
    bt 1f
    mov.l .Lce_isr_fault,r1
    mov #1,r2
    mov.l r2,@r1
1:  lds.l @r15+,macl
    lds.l @r15+,mach
    lds.l @r15+,pr
    mov.l @r15+,r7
    mov.l @r15+,r6
    mov.l @r15+,r5
    mov.l @r15+,r4
    mov.l .Lce_isr_sp,r1
    rts
    mov.l @r1,r15
.Lce_isr_held:
    mov.l .Lce_isr_iprb,r1
    mov.w @r1,r0
    mov.w .Lce_isr_keep,r2
    and r2,r0
    mov.w r0,@r1
    mov.w @r1,r0 /* the level is down before CE resumes */
    rts
    mov #0,r0
.Lce_isr_keep: .word 0xff0f /* IPRB without the SCI's field (bits 7..4) */
    .align 2
.Lce_isr_active: .long C_SYMBOL(kui_retail_hook_active)
.Lce_isr_sp: .long C_SYMBOL(kui_retail_ce_isr_sp)
.Lce_isr_stack: .long KUI_RETAIL_HOOK_STACK-16
.Lce_isr_handler: .long C_SYMBOL(kui_retail_ce_irq)
.Lce_isr_guard: .long KUI_RETAIL_HOOK_STACK_BOTTOM
.Lce_isr_guard_value: .long 0x4b554947
.Lce_isr_fault: .long C_SYMBOL(kui_retail_hook_fault)
.Lce_isr_iprb: .long 0xffd00008
/* The interrupted R15 while the handler runs. */
    .section .bss.retail_ce_isr,"aw",@nobits
    .align 2
C_SYMBOL(kui_retail_ce_isr_sp): .space 4
#elif defined(KUI_RETAIL_ASYNC)
/* Vector templates for the background reader, copied by
 * kui_retail_async_init into its region (VBR = region - 0x100). They run in
 * exception state (BL=1, RB=1) and leave every register, SSR, SPC and SGR as
 * the hardware set them: R0 is parked in DBR, which serves only H-UDI and
 * user-break debugging (games use neither). The stream's events go to the
 * interrupt entry with R0 back; every other event goes to a releasing entry
 * below with R0 still in DBR. Word 2 of the forwarding template names its
 * releasing entry (set at init); the interrupt template's targets are fixed. */
    .section .text.retail_vectors,"ax"
    .align 2
    .global C_SYMBOL(kui_retail_vector_forward)
C_SYMBOL(kui_retail_vector_forward):
    ldc r0,dbr
    mov.l 1f,r0
    jmp @r0
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
    jmp @r0
    nop
1:  mov.l 4f,r0
    jmp @r0
    stc dbr,r0
    .align 2
2:  .long 0xff000028 /* INTEVT */
3:  .long C_SYMBOL(kui_retail_release_600)
4:  .long C_SYMBOL(kui_retail_irq_entry)

/* The stream's interrupt. GD calls run with BL set, so the private stack is
 * free here; the interrupted R15 is in SGR. Bank-1 R0..R7, PR and MAC are
 * the game's and are saved; R8..R14 are preserved by the C handler. A
 * nonzero result passes the event on as any other (a releasing entry). */
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
    mov.l .Lirq_release,r0
    jmp @r0
    nop
    .align 2
.Lirq_stack: .long KUI_RETAIL_HOOK_STACK-16
.Lirq_handler: .long C_SYMBOL(kui_retail_async_irq)
.Lirq_guard: .long KUI_RETAIL_HOOK_STACK_BOTTOM
.Lirq_guard_value: .long 0x4b554947
.Lirq_fault: .long C_SYMBOL(kui_retail_hook_fault)
.Lirq_release: .long C_SYMBOL(kui_retail_release_600)

/* Releasing entries: an event that is not the reader's first gives the game
 * its VBR and its SCI level back, then enters the game's vector, so the
 * game's handler never runs under the reader's VBR and the reader's
 * interrupt cannot reach the game's vectors. Entered from the vectors with
 * the game's R0 in DBR and R15 the interrupted stack, on which R1..R3 are
 * kept meanwhile; everything reaches the game's vector unchanged. With
 * launch Y (release.armed), an interrupt's handler also returns through
 * kui_retail_rehook: its SPC and SGR are kept in a free pending slot and SPC
 * names the trampoline. Offsets into the release frame (region + 0x240):
 * vbr 0, sci 4, armed 8, depth 12, pending 16 + 8 * slot (SPC, SGR),
 * rehooks 40, released[3] 44. */
    .align 2
    .global C_SYMBOL(kui_retail_release_100)
C_SYMBOL(kui_retail_release_100):
    mov.l r1,@-r15
    mov.l r2,@-r15
    mov.w .Lrelease_100,r2
    bra .Lrelease
    mov #44,r1
    .global C_SYMBOL(kui_retail_release_400)
C_SYMBOL(kui_retail_release_400):
    mov.l r1,@-r15
    mov.l r2,@-r15
    mov.w .Lrelease_400,r2
    bra .Lrelease
    mov #48,r1
    .global C_SYMBOL(kui_retail_release_600)
C_SYMBOL(kui_retail_release_600):
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
.Lrelease_frame: .long C_SYMBOL(kui_retail_async_region)+0x240
.Lrelease_iprb: .long 0xffd00008
.Lrelease_return: .long C_SYMBOL(kui_retail_rehook)

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
    .global C_SYMBOL(kui_retail_rehook)
C_SYMBOL(kui_retail_rehook):
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
.Lrehook_frame: .long C_SYMBOL(kui_retail_async_region)+0x240
.Lrehook_vbr: .long C_SYMBOL(kui_retail_async_region)-0x100
.Lrehook_iprb: .long 0xffd00008
#endif
