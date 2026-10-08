/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_loader_layout.h"
#include "kui/toy_pilot_boot.h"
#if !KUI_RETAIL_TOY_PILOT
#error This bootstrap is only for the separate Toy finite pilot
#endif
#if !KUI_RETAIL_LOW_RESIDENT || defined(KUI_RETAIL_CE) || defined(KUI_RETAIL_ASYNC)
#error The Toy finite pilot requires the synchronous low native resident
#endif
#define JOIN_INNER(a,b) a##b
#define JOIN(a,b) JOIN_INNER(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)

/* Preserve the real heap-init call on the owner stack and with its own SR.
 * After it returns, capture its actual caller-clobbered integer/SR/MAC state
 * before invoking authored installer code. No FPU or register-bank change. */
    .section .text.toy_pilot_boot,"ax"
    .align 2
    .global C(kui_toy_pilot_heap_hook)
C(kui_toy_pilot_heap_hook):
    stc.l sr,@-r15
    mov.l .Lcontrol,r2
    mov.l @(KUI_TOY_BOOT_STATUS,r2),r0
    cmp/eq #1,r0
    bf .Lheap_reinit_refused
    mov #2,r0
    mov.l r0,@(KUI_TOY_BOOT_STATUS,r2)
    mov.l r4,@(KUI_TOY_BOOT_HEAP_BEGIN,r2)
    mov.l r5,@(KUI_TOY_BOOT_HEAP_BYTES,r2)
    mov r15,r1
    add #4,r1
    mov.l r1,@(KUI_TOY_BOOT_ENTRY_SP,r2)
    sts pr,r1
    mov.l r1,@(KUI_TOY_BOOT_ORIGINAL_PR,r2)
    /* The guard changed T; restore the exact inherited SR before the real
     * allocator init. The persistent slot refuses a later re-init first. */
    ldc.l @r15+,sr
    mov.l .Loriginal_init,r3
    jsr @r3
    nop
    sts.l mach,@-r15
    sts.l macl,@-r15
    stc.l sr,@-r15
    mov.l r0,@-r15
    mov.l r1,@-r15
    mov.l r2,@-r15
    mov.l r3,@-r15
    mov.l r4,@-r15
    mov.l r5,@-r15
    mov.l r6,@-r15
    mov.l r7,@-r15
    mov.l r8,@-r15
    mov.l .Lcontrol,r1
    mov.l @(KUI_TOY_BOOT_ORIGINAL_PR,r1),r0
    mov.l r0,@-r15
    mov r15,r8
    stc sr,r0
    mov.l .Linstaller_mask,r1
    or r1,r0
    ldc r0,sr
    mov.l .Lcontrol,r2
    mov.l @r2,r0
    tst r0,r0
    bt .Lmissing_installer
    mov.l @(KUI_TOY_BOOT_STAGE_STACK,r2),r15
    jsr @r0
    nop
    mov r8,r15
    lds.l @r15+,pr
    mov.l @r15+,r8
    mov.l @r15+,r7
    mov.l @r15+,r6
    mov.l @r15+,r5
    mov.l @r15+,r4
    mov.l @r15+,r3
    mov.l @r15+,r2
    mov.l @r15+,r1
    mov.l @r15+,r0
    ldc.l @r15+,sr
    lds.l @r15+,macl
    lds.l @r15+,mach
    rts
    nop
.Lmissing_installer:
    mov #5,r1
    mov.l r1,@(KUI_TOY_BOOT_STATUS,r2)
    mov #1,r1
    mov.l r1,@(KUI_TOY_BOOT_ERROR,r2)
    mov.l .Lreturn_hook,r0
    jmp @r0
    nop
.Lheap_reinit_refused:
    mov #5,r1
    mov.l r1,@(KUI_TOY_BOOT_STATUS,r2)
    mov #11,r1
    mov.l r1,@(KUI_TOY_BOOT_ERROR,r2)
    mov.l .Lreturn_hook,r0
    jmp @r0
    nop
    .align 2
.Lcontrol: .long C(kui_toy_pilot_boot_control)
.Loriginal_init: .long KUI_TOY_BOOT_HEAP_INIT
.Lreturn_hook: .long C(kui_toy_pilot_return_hook)
.Linstaller_mask: .long 0x100000f0

/* The game's inherited PR must never return into the temporary high stage.
 * This terminal path uses only low resident code/stack and resets after its
 * numeric report. It never resumes the game or frees an executing lease. */
    .align 2
    .global C(kui_toy_pilot_return_hook)
C(kui_toy_pilot_return_hook):
    stc sr,r0
    mov.l .Lterminal_mask,r1
    or r1,r0
    ldc r0,sr
    sts pr,r5
    mov r15,r6
    mov #1,r4
    mov.l .Lterminal_stack,r15
    mov.l .Lterminal_report,r0
    jmp @r0
    nop
    .align 2
.Lterminal_mask: .long 0x100000f0
.Lterminal_stack: .long KUI_RETAIL_HOOK_STACK-16
.Lterminal_report: .long C(kui_retail_menu_return)

    .section .bss.toy_pilot_boot,"aw",@nobits
    /* Numerical SH control only; never a DMA source/destination. Word
     * alignment suffices, and cache publication rounds the span itself. */
    .align 2
    .global C(kui_toy_pilot_boot_control)
C(kui_toy_pilot_boot_control):
    .space KUI_TOY_BOOT_CONTROL_BYTES
