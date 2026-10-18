/* SPDX-License-Identifier: GPL-3.0-only */
/* Optional exact-owner Toy Commander diagnostic. Separate readonly modes
 * supply a synthetic blank card or verified SD-image replies.
 * Installed at 0x8c0bf264 only after complete executable/window admission.
 * The patch's r0 is dead: the first replaced instruction overwrote it. */
#include "kui/vmu_maple_observe.h"
#define C_INNER(name) _ ## name
#define C(name) C_INNER(name)

    .section .vmu_observe_exports,"a"
    .align 2
    .long KUI_VMU_MAPLE_OBSERVE_MAGIC,KUI_VMU_MAPLE_OBSERVE_VERSION
    .long KUI_VMU_MAPLE_OBSERVE_EXPORT_BYTES
    .long C(kui_vmu_maple_observer_init),C(kui_vmu_maple_observer_hook)
    .long __vmu_observe_bss_begin,__vmu_observe_bss_end
    .long __vmu_observe_stack_bottom,__vmu_observe_stack_top
    .long __vmu_observe_worker_end,KUI_VMU_MAPLE_OBSERVE_LEASE_BYTES
    .long C(kui_vmu_maple_observer_snapshot),C(kui_vmu_maple_observer_terminal)
    .long C(kui_vmu_maple_observer_complete_hook)
    .long KUI_VMU_READONLY_PROOF
#if KUI_VMU_READONLY_PROOF == 2
    .long C(kui_vmu_maple_observer_sd_transaction)
#else
    .long 0
#endif

    .text
    .align 2
    .global C(kui_vmu_maple_observer_hook)
    .type C(kui_vmu_maple_observer_hook),@function
C(kui_vmu_maple_observer_hook):
    /* The two initial saves are on the existing live SDK stack. No shared
     * worker state is touched before IMASK15 excludes interrupt reentry.
     * The observer has no SDK callback/cache invalidation/FPU operation. */
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
    mov.l .Lprivate_stack,r15
    mov.l r4,@-r15
    mov r4,r0
    add #68,r0
    mov.l @r0,r4 /* Original SR from the saved game frame. */
    mov.l .Lvisit,r0
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
    mov.l @r15+,r1
    ldc r1,sr
    mov.l @r15+,r1

    /* Exact relocation of the six overwritten instructions. The original
     * PC-relative word loads had values 0x10f4 and 0x0fd8. The original DMA
     * start store executes ONCE, with the original inherited SR restored. */
    mov.l .Lmanager_register_offset,r0
    mov.l @r14,r3
    mov.l @(r0,r3),r2
    mov.l r12,@(24,r2)
    mov.l .Lmanager_pending_offset,r0
    mov.l @r14,r3
    /* Preserve the scratch jump register in the delay slot, including SP. */
    mov.l r2,@-r15
    mov.l .Lresume,r2
    jmp @r2
    mov.l @r15+,r2
    .align 2
.Lprivate_stack: .long __vmu_observe_stack_top
.Lvisit: .long C(kui_vmu_maple_observer_visit)
.Lmanager_register_offset: .long 0x10f4
.Lmanager_pending_offset: .long 0x0fd8
.Lresume: .long KUI_VMU_MAPLE_OBSERVE_RESUME
    .size C(kui_vmu_maple_observer_hook),.-C(kui_vmu_maple_observer_hook)

    /* The admitted consumer call at 0x8c0b535e loads its target from the
     * single literal at 0x8c0b540c. Only that literal is replaced. The SDK
     * has already found MDST idle and taken its lock, with IMASK15 held.
     * This observer reads completed headers; it changes no response bytes.
     * R3 must become the original consumer target before the tail call. */
    .align 2
    .global C(kui_vmu_maple_observer_complete_hook)
    .type C(kui_vmu_maple_observer_complete_hook),@function
C(kui_vmu_maple_observer_complete_hook):
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
    mov.l r4,@-r15
    mov.l @(12,r4),r5 /* Exact SDK return PR at consumer entry. */
    mov r4,r0
    add #68,r0
    mov.l @r0,r4
    mov.l .Lcomplete_visit,r0
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
    mov.l @r15+,r1
    ldc r1,sr
    mov.l @r15+,r1
    mov.l @r15+,r0
    mov.l .Lcomplete_consumer,r3
    jmp @r3
    nop
    .align 2
.Lcomplete_stack: .long __vmu_observe_stack_top
.Lcomplete_visit: .long C(kui_vmu_maple_observer_complete)
.Lcomplete_consumer: .long 0x8c0bf37a
    .size C(kui_vmu_maple_observer_complete_hook),.-C(kui_vmu_maple_observer_complete_hook)

    /* A terminal entry never returns to SDK teardown or a temporary stage. */
    .align 2
    .global C(kui_vmu_maple_observer_terminal)
    .type C(kui_vmu_maple_observer_terminal),@function
C(kui_vmu_maple_observer_terminal):
    stc sr,r0
    or #0xf0,r0
    ldc r0,sr
    mov.l .Lterminal_stack,r15
    mov.l .Lreport,r0
    jmp @r0
    nop
    .align 2
.Lterminal_stack: .long __vmu_observe_stack_top
.Lreport: .long C(kui_vmu_maple_observer_report)
    .size C(kui_vmu_maple_observer_terminal),.-C(kui_vmu_maple_observer_terminal)
