/* SPDX-License-Identifier: GPL-3.0-only */
/* Relocated one-pair observer. Only the submission gateway returns. */
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
    .text
    .align 2
    .global C(kui_vmu_native_passive_submit_hook)
    .type C(kui_vmu_native_passive_submit_hook),@function
C(kui_vmu_native_passive_submit_hook):
    /* R0 is dead at the exact admitted SDK patch site. */
    mov.l r1,@-r15
    stc sr,r1
    mov.l r1,@-r15
    mov r1,r0
    or #0xf0,r0
    ldc r0,sr
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
    sts.l mach,@-r15
    sts.l macl,@-r15
    stc.l gbr,@-r15
    mov r15,r4
    mov.l .Lsubmit_stack,r15
    mov.l r4,@-r15
    mov.l .Lsubmit_visit,r0
    jsr @r0
    nop
    mov.l @r15,r15
    ldc.l @r15+,gbr
    lds.l @r15+,macl
    lds.l @r15+,mach
    lds.l @r15+,pr
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
    mov.l @r15+,r0
    mov.l @r15+,r1
    /* Every frame word has been consumed before inherited IRQ admission. */
    ldc r0,sr
    nop
    nop
    nop
    nop
    /* Replay all six overwritten instructions, exactly one MDST store. */
    mov.l .Lregister_offset,r0
    mov.l @r14,r3
    mov.l @(r0,r3),r2
    mov.l r12,@(24,r2)
    mov.l .Lpending_offset,r0
    mov.l @r14,r3
    mov.l r2,@-r15
    mov.l .Lresume_cell,r2
    mov.l @r2,r2
    jmp @r2
    mov.l @r15+,r2
    .align 2
.Lsubmit_stack: .long __vmu_native_worker_stack_top
.Lsubmit_visit: .long C(kui_vmu_native_passive_submit)
.Lregister_offset: .long 0x10f4
.Lpending_offset: .long 0x0fd8
.Lresume_cell: .long C(kui_vmu_native_passive_resume)
    .size C(kui_vmu_native_passive_submit_hook),.-C(kui_vmu_native_passive_submit_hook)

    .align 2
    .global C(kui_vmu_native_passive_complete_hook)
    .type C(kui_vmu_native_passive_complete_hook),@function
C(kui_vmu_native_passive_complete_hook):
    /* Extra R0 at frame+76 does not change the shared frame member offsets. */
    mov.l r0,@-r15
    mov.l r1,@-r15
    stc sr,r1
    mov.l r1,@-r15
    mov r1,r0
    or #0xf0,r0
    ldc r0,sr
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
    sts.l mach,@-r15
    sts.l macl,@-r15
    stc.l gbr,@-r15
    mov r15,r4
    mov.l .Lcomplete_stack,r15
    mov.l .Lcomplete_visit,r0
    jsr @r0
    nop
    /* A returned diagnostic callback cannot accidentally consume replies. */
    mov.l .Lcomplete_terminal,r0
    jmp @r0
    nop
    .align 2
.Lcomplete_stack: .long __vmu_native_worker_stack_top
.Lcomplete_visit: .long C(kui_vmu_native_passive_complete)
.Lcomplete_terminal: .long C(kui_vmu_native_passive_terminal)
    .size C(kui_vmu_native_passive_complete_hook),.-C(kui_vmu_native_passive_complete_hook)

    .align 2
    .global C(kui_vmu_native_passive_terminal)
    .type C(kui_vmu_native_passive_terminal),@function
C(kui_vmu_native_passive_terminal):
    stc sr,r0
    or #0xf0,r0
    ldc r0,sr
    mov.l .Lterminal_stack,r15
    mov.l .Lterminal_report,r0
    jmp @r0
    nop
    .align 2
.Lterminal_stack: .long __vmu_native_worker_stack_top
.Lterminal_report: .long C(kui_vmu_native_passive_report)
    .size C(kui_vmu_native_passive_terminal),.-C(kui_vmu_native_passive_terminal)

    .data
    .align 2
    .global C(kui_vmu_native_passive_resume)
    .type C(kui_vmu_native_passive_resume),@object
C(kui_vmu_native_passive_resume): .long 0
    .size C(kui_vmu_native_passive_resume),4
    .global C(kui_vmu_native_passive_consumer)
    .type C(kui_vmu_native_passive_consumer),@object
C(kui_vmu_native_passive_consumer): .long 0
    .size C(kui_vmu_native_passive_consumer),4
