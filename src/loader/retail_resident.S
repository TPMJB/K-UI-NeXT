/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_loader_layout.h"
#define JOIN_INNER(a,b) a##b
#define JOIN(a,b) JOIN_INNER(a,b)
#define C_SYMBOL(name) JOIN(__USER_LABEL_PREFIX__,name)

/* Called by the high relay after it replaces the retired lower IP bootstrap.
 * Do not switch stacks here: decoding/init use the temporary high stack. */
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
    stc sr,r0
    mov.l r0,@-r15
    mov.l .Lirq_mask,r1
    or r1,r0
    ldc r0,sr
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
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
