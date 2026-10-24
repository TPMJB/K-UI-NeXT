/* SPDX-License-Identifier: GPL-3.0-only */
#define JI(a,b) a##b
#define J(a,b) JI(a,b)
#define C(n) J(__USER_LABEL_PREFIX__,n)
#if KUI_VMU_NATIVE_CACHED_CODE
#define RUNTIME_STACK_TOP (__vmu_native_worker_stack_top-0x20000000)
#else
#define RUNTIME_STACK_TOP __vmu_native_worker_stack_top
#endif
    .text
    .align 2
    .global C(kui_vmu_native_trace_irq)
    .type C(kui_vmu_native_trace_irq),@function
C(kui_vmu_native_trace_irq):
    stc spc,r0
    mov.l r0,@r4
    stc ssr,r0
    mov.l r0,@(4,r4)
    stc vbr,r0
    rts
    mov.l r0,@(8,r4)
    .size C(kui_vmu_native_trace_irq),.-C(kui_vmu_native_trace_irq)
    /* Private C callback; complete owner integer frame including R0. Reorder
     * the final three words before restoring so SR is consumed last, after
     * every GPR and the original SP. C uses no FR/FPUL registers. */
    .macro TRACE_SAVE code
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
    mov r15,r5
    mov #\code,r4
    mov.l .Lstack\@,r15
    mov.l r5,@-r15
    mov.l .Lvisit\@,r0
    jsr @r0
    nop
    mov.l @r15,r15
    mov r15,r0
    add #68,r0
    mov.l @r0,r1
    mov.l @(4,r0),r2
    mov.l @(8,r0),r3
    mov.l r2,@r0
    mov.l r3,@(4,r0)
    mov.l r1,@(8,r0)
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
    mov.l @r15+,r1
    mov.l @r15+,r0
    ldc.l @r15+,sr
    nop
    nop
    nop
    nop
    bra .Lready\@
    nop
    .align 2
.Lstack\@: .long RUNTIME_STACK_TOP
.Lvisit\@: .long C(kui_vmu_native_trace_game)
.Lready\@:
    .endm
    .align 2
    .global C(kui_vmu_native_trace_frame_entry)
    .type C(kui_vmu_native_trace_frame_entry),@function
C(kui_vmu_native_trace_frame_entry):
    TRACE_SAVE 0x41
    mov.l r14,@-r15
    mov.l r13,@-r15
    mov.l r12,@-r15
    mov.l r11,@-r15
    mov #0,r11
    mov.l r10,@-r15
    mov.l .Lframe_resume,r0
    jmp @r0
    nop
    .align 2
.Lframe_resume: .long 0x8c166404
    .size C(kui_vmu_native_trace_frame_entry),.-C(kui_vmu_native_trace_frame_entry)
    .align 2
    .global C(kui_vmu_native_trace_wait_loop)
    .type C(kui_vmu_native_trace_wait_loop),@function
C(kui_vmu_native_trace_wait_loop):
    TRACE_SAVE 0x42
    jsr @r13
    nop
    mov r0,r5
    jsr @r10
    mov.l @r12,r4
    jsr @r9
    mov r0,r4
    mov r0,r14
    mov.l .Lwait_pr,r0
    lds r0,pr
    mov r14,r0
    mov.l .Lwait_resume,r3
    jmp @r3
    nop
    .align 2
.Lwait_pr: .long 0x8c166430
.Lwait_resume: .long 0x8c166432
    .size C(kui_vmu_native_trace_wait_loop),.-C(kui_vmu_native_trace_wait_loop)
    .align 2
    .global C(kui_vmu_native_trace_wait_before)
    .type C(kui_vmu_native_trace_wait_before),@function
C(kui_vmu_native_trace_wait_before):
    TRACE_SAVE 0x43
    mov.l .Lwait_target,r3
    jmp @r3
    nop
    .align 2
.Lwait_target: .long 0x8c1cd000
    .size C(kui_vmu_native_trace_wait_before),.-C(kui_vmu_native_trace_wait_before)
    .align 2
    .global C(kui_vmu_native_trace_wait_after)
    .type C(kui_vmu_native_trace_wait_after),@function
C(kui_vmu_native_trace_wait_after):
    TRACE_SAVE 0x44
    mov.l .Lafter_r4,r4
    mov.l .Lafter_r2,r2
    mov.l .Lafter_r0,r0
    mov.l .Lafter_r3,r3
    mov.l r11,@r0
    mov.l r11,@r3
    mov.l .Lafter_resume,r1
    jmp @r1
    nop
    .align 2
.Lafter_r4: .long 0x8c304854
.Lafter_r2: .long 0x8c304834
.Lafter_r0: .long 0x8c30482c
.Lafter_r3: .long 0x8c304830
.Lafter_resume: .long 0x8c166460
    .size C(kui_vmu_native_trace_wait_after),.-C(kui_vmu_native_trace_wait_after)
    .section .rodata.native_trace_exports,"a"
    .align 2
    .global C(kui_vmu_native_trace_exports)
    .type C(kui_vmu_native_trace_exports),@object
C(kui_vmu_native_trace_exports):
    .long 0x4b545231,2,40
    .long C(kui_vmu_native_trace_frame_entry),C(kui_vmu_native_trace_wait_loop)
    .long C(kui_vmu_native_trace_wait_before),C(kui_vmu_native_trace_wait_after)
#if KUI_VMU_NATIVE_CACHED_CODE
    .long C(kui_vmu_native_sdk_coherence_uniform_gate)+0x20000000
    .long C(kui_vmu_native_sdk_coherence_variable_gate)+0x20000000
#else
    .long C(kui_vmu_native_sdk_coherence_uniform_gate),C(kui_vmu_native_sdk_coherence_variable_gate)
#endif
    .long C(kui_vmu_native_sdk_coherence_state)
    .size C(kui_vmu_native_trace_exports),.-C(kui_vmu_native_trace_exports)
