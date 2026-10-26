/* SPDX-License-Identifier: GPL-3.0-only */
/* Relocated observer. The read-only bridge also returns to the original
 * consumer after validated synthetic replies have been published. */
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
#if KUI_VMU_NATIVE_CACHED_CODE
/* The reservation/export symbols retain P2. Every runtime C entry shares
 * one P1 stack alias, including failure-only entries: stale dirty P1 frames
 * must never be allowed to overwrite a later live P2 diagnostic frame. */
#define RUNTIME_STACK_TOP (__vmu_native_worker_stack_top-0x20000000)
#else
#define RUNTIME_STACK_TOP __vmu_native_worker_stack_top
#endif
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
#if KUI_NATIVE_MAPLE_BYPASS_ISOLATION
    nop
#else
    jsr @r0
#endif
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
.Lsubmit_stack: .long RUNTIME_STACK_TOP
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
#if KUI_VMU_NATIVE_READONLY_PROBE
    mov.l r4,@-r15
#endif
    mov.l .Lcomplete_visit,r0
#if KUI_NATIVE_MAPLE_BYPASS_ISOLATION
    nop
#else
    jsr @r0
#endif
    nop
#if KUI_VMU_NATIVE_READONLY_PROBE
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
    /* R3 becomes the original SDK consumer target below. Keep inherited
     * SR there while consuming R1/R0 and the final owner-stack words. */
    mov.l @r15+,r3
    mov.l @r15+,r1
    mov.l @r15+,r0
    ldc r3,sr
    nop
    nop
    nop
    nop
    mov.l .Lcomplete_consumer_cell,r3
    mov.l @r3,r3
    jmp @r3
    nop
#else
    /* A returned diagnostic callback cannot accidentally consume replies. */
    mov.l .Lcomplete_terminal,r0
    jmp @r0
    nop
#endif
    .align 2
.Lcomplete_stack: .long RUNTIME_STACK_TOP
.Lcomplete_visit: .long C(kui_vmu_native_passive_complete)
#if KUI_VMU_NATIVE_READONLY_PROBE
.Lcomplete_consumer_cell: .long C(kui_vmu_native_passive_consumer)
#else
.Lcomplete_terminal: .long C(kui_vmu_native_passive_terminal)
#endif
    .size C(kui_vmu_native_passive_complete_hook),.-C(kui_vmu_native_passive_complete_hook)

#if KUI_VMU_NATIVE_READONLY_PROBE
    /* The allocator's release entry writes R0 immediately. Inspect its
     * original R4 argument before replaying any allocator instructions. */
    .align 2
    .global C(kui_vmu_native_readonly_release_hook)
    .type C(kui_vmu_native_readonly_release_hook),@function
C(kui_vmu_native_readonly_release_hook):
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
    mov.l .Lrelease_stack,r15
    mov.l r4,@-r15
    mov.l .Lrelease_check,r0
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
    /* Keep IRQ exclusion uninterrupted across validation and the original
     * allocator's mask establishment. R7 is overwritten by its prologue;
     * temporarily preserve inherited SR there to recover its old IMASK. */
    mov r0,r7
    or #0xf0,r0
    ldc r0,sr
    nop
    nop
    nop
    nop
    /* Replay the first six ops with their original input SR, without
     * unmasking: MOV R7,R0 supplies the value original STC would see. */
    mov r7,r0
    mov.l .Lrelease_mask,r3
    shlr2 r0
    shlr2 r0
    and #15,r0
    mov r0,r7
    /* Witnessed original release+12..+19 establishes IMASK15. Include it
     * here and resume at+20, with exactly its original register state. */
    stc sr,r0
    and r3,r0
    or #0xf0,r0
    ldc r0,sr
    nop
    nop
    nop
    nop
    mov.l r2,@-r15
    mov.l .Lrelease_resume_cell,r2
    mov.l @r2,r2
    jmp @r2
    mov.l @r15+,r2
    .align 2
.Lrelease_stack: .long RUNTIME_STACK_TOP
.Lrelease_check: .long C(kui_vmu_native_readonly_release_check)
.Lrelease_mask: .long 0xffffff0f
.Lrelease_resume_cell: .long C(kui_vmu_native_readonly_release_resume)
    .size C(kui_vmu_native_readonly_release_hook),.-C(kui_vmu_native_readonly_release_hook)
#endif

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
.Lterminal_stack: .long RUNTIME_STACK_TOP
.Lterminal_report: .long C(kui_vmu_native_passive_report)
    .size C(kui_vmu_native_passive_terminal),.-C(kui_vmu_native_passive_terminal)

#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    /* Failure-only ABI: R4 kind, R5 value, R6/R7 context. No owner stack
     * write or return path exists. All diagnostics run on private storage. */
    .align 2
    .global C(kui_vmu_native_runtime_fault_hook)
    .type C(kui_vmu_native_runtime_fault_hook),@function
C(kui_vmu_native_runtime_fault_hook):
    stc sr,r0
    or #0xf0,r0
    ldc r0,sr
    mov.l .Lruntime_fault_stack,r15
    mov.l .Lruntime_fault_report,r0
    jmp @r0
    nop
    .align 2
.Lruntime_fault_stack: .long RUNTIME_STACK_TOP
.Lruntime_fault_report: .long C(kui_vmu_native_runtime_fault)
    .size C(kui_vmu_native_runtime_fault_hook),.-C(kui_vmu_native_runtime_fault_hook)

    /* The exact Evolution malloc-failure block has the rounded request in
     * R14 and the saved owner caller at SP+4. The reviewed stage veneer
     * reaches this entry without modifying those inputs or SP. */
    .align 2
    .global C(kui_vmu_native_allocation_failure_hook)
    .type C(kui_vmu_native_allocation_failure_hook),@function
C(kui_vmu_native_allocation_failure_hook):
    mov r14,r5
    mov.l @(4,r15),r6
    mov r15,r7
    mov #1,r4
    mov.l .Lallocation_runtime_fault,r0
    jmp @r0
    nop
    .align 2
.Lallocation_runtime_fault: .long C(kui_vmu_native_runtime_fault_hook)
    .size C(kui_vmu_native_allocation_failure_hook),.-C(kui_vmu_native_allocation_failure_hook)
#endif

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
#if KUI_VMU_NATIVE_READONLY_PROBE
    .global C(kui_vmu_native_readonly_release_resume)
    .type C(kui_vmu_native_readonly_release_resume),@object
C(kui_vmu_native_readonly_release_resume): .long 0
    .size C(kui_vmu_native_readonly_release_resume),4
#endif
