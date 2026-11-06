/* SPDX-License-Identifier: GPL-3.0-only */
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)

/* Original GIN reaches this fixed P2 entry by its own final RTE. Capture
 * the active register bank before changing any GPR or SR. In particular,
 * GIN's earlier RB0 assignments do not establish the actual RB1 R0..R7.
 * The frame is identical to the proven cold relay's21-word integer frame:
 * MACL,MACH,GBR,VBR,SR,PR,R14..R0. No FPU state is touched. */
    .section .entry,"ax"
    .align 2
    .global _start
    .global C(kui_smash_menu_loader_entry)
    .type C(kui_smash_menu_loader_entry),@function
_start:
C(kui_smash_menu_loader_entry):
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
    mov.l .Lccr,r0
    mov.l @r0,r9
    mov.l .Lstack,r15
    mov r8,r4
    mov r9,r5
    mov.l .Lmain,r0
    jsr @r0
    nop

/* The C entry returns only for an unselected original GIN transition.
 * That path leaves CCR, SR's bank/mode bits, FPU and both source buffers
 * untouched. Restore every actual saved value and use the original RTE
 * contract to resume the original uncached executable entry. */
    mov r8,r15
    lds.l @r15+,macl
    lds.l @r15+,mach
    ldc.l @r15+,gbr
    ldc.l @r15+,vbr
    ldc.l @r15+,ssr
    lds.l @r15+,pr
    mov.l .Lgame,r0
    ldc r0,spc
    mov.l @r15+,r14
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    mov.l @r15+,r7
    mov.l @r15+,r6
    mov.l @r15+,r5
    mov.l @r15+,r4
    mov.l @r15+,r3
    mov.l @r15+,r2
    mov.l @r15+,r1
    mov.l @r15+,r0
    rte
    nop
    .align 2
.Lccr: .long 0xff00001c
.Lstack: .long __smash_menu_loader_stack_top
.Lmain: .long C(kui_smash_menu_loader_main)
.Lgame: .long 0xac010000
    .size C(kui_smash_menu_loader_entry),.-C(kui_smash_menu_loader_entry)
