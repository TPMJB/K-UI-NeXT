/* SPDX-License-Identifier: GPL-3.0-only */
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
/* Source-bound MDE application reset entry, before its cleanup helpers.
 * Capture the active integer bank, preserve FPU state, then mask BL/IMASK
 * without changing RB. A busy guard parks on the original owner stack while
 * its IRQ callbacks finish; no original cleanup/CRT caller can resume. */
    .section .entry,"ax"
    .align 2
    .global C(kui_smash_menu_return_entry)
C(kui_smash_menu_return_entry):
    mov.l r0,@-r15
    mov.l r1,@-r15
    mov.l r2,@-r15
    mov.l r3,@-r15
    mov.l r4,@-r15
    mov.l r5,@-r15
    mov.l r6,@-r15
    mov.l r7,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
    sts.l pr,@-r15
    stc.l sr,@-r15
    stc.l vbr,@-r15
    stc.l gbr,@-r15
    sts.l mach,@-r15
    sts.l macl,@-r15
    mov r15,r8
    stc sr,r0
    mov.l .Lmask,r1
    or r1,r0
    ldc r0,sr
.Lvisit:
    mov.l .Lstack,r15
    mov r8,r4
    mov.l .Lmain,r0
    jsr @r0
    nop
    /* Only an interrupt-eligible busy guard returns. The saved owner frame
     * stays ABOVE the parked SP, leaving IRQ pushes below it. Restore its
     * real global/vector bases and SR before permitting SDK completion. */
    mov r8,r15
    mov.l @(8,r8),r0
    ldc r0,gbr
    mov.l @(12,r8),r0
    ldc r0,vbr
    mov.l @(16,r8),r0
    ldc r0,sr
    .global __smash_menu_return_wait_owner
__smash_menu_return_wait_owner:
    mov.l .Lspins,r0
.Lwait:
    nop
    dt r0
    bf .Lwait
    mov.l @(16,r8),r0
    mov.l .Lmask,r1
    or r1,r0
    ldc r0,sr
    bra .Lvisit
    nop
    .align 2
.Lmask: .long 0x100000f0
.Lstack: .long __smash_menu_return_stack_top
.Lmain: .long C(kui_smash_menu_return_main)
.Lspins: .long 1024
