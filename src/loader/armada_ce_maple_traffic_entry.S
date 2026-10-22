/* SPDX-License-Identifier: GPL-3.0-only */
/* Private-stack entry for bounded read-only traffic observation. The mapped
 * bridge has already saved every volatile register and entered kernel mode. */
#include "kui/armada_ce_vmu_layout.h"
#include "kui/armada_ce_maple_continuation.h"
    .section .hook.entry,"ax"
    .align 2
    .global _kui_armada_ce_maple_hook
_kui_armada_ce_maple_hook:
    stc sr,r3
    mov r3,r0
    or #0xf0,r0
    ldc r0,sr
    mov.l .Lstack,r0
    mov.l r15,@-r0
    mov.l r8,@-r0
    sts.l pr,@-r0
    mov.l r3,@-r0
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    /* Only this PRE invocation owns the ordinary source worker. Keep its
     * argument, previous mode and incoming SR across all C/IRQ boundaries. */
    mov.l r4,@-r0
    mov.l r6,@-r0
#endif
    mov r0,r15
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    mov.l @(12,r15),r1
    mov.l .Lbridge_return,r2
    mov.l r1,@r2
    mov r3,r7
#endif
    mov.l .Lrun,r0
    jsr @r0
    nop
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
.Ldispatch:
    tst r0,r0
    bt .Lreturn
    cmp/eq #KUI_ARMADA_CE_MAPLE_SERVICE,r0
    bf .Lterminal
    /* C has confirmed the original MDEN0 fence still belongs to this worker.
     * Independently refuse an entry that arrived blocked or IRQ-masked. */
    mov.l @(8,r15),r0
    mov.l .Lforbidden_sr,r1
    tst r1,r0
    bf .Lterminal
    /* r8 is callee-saved by the fixed worker routine and the CE IRQ ABI. The
     * separate 4KiB service stack never shares the bounded proof C frames. */
    mov r15,r8
    mov.l .Lworker_stack,r15
    ldc r0,sr
    mov.l @(4,r8),r4
    mov.l .Lwork,r0
    jsr @r0
    nop
    /* Preserve the result before the mask sequence reuses r0. No C or guest
     * access occurs between the service return and remasking. */
    mov r0,r7
    stc sr,r3
    mov r3,r0
    or #0xf0,r0
    ldc r0,sr
    mov r8,r15
    tst r7,r7
    bf .Lresume
    /* A real CE wait needs the ordinary CE stack: the external API's frame
     * size is not part of either private freestanding stack budget. Every
     * stack change is masked; the wait sees the exact admitted incoming SR. */
    mov.l @(20,r8),r15
    mov.l @(8,r8),r0
    ldc r0,sr
    mov.l @(4,r8),r4
    mov.l .Lwait,r0
    jsr @r0
    nop
    mov r0,r7
    stc sr,r3
    mov r3,r0
    or #0xf0,r0
    ldc r0,sr
    mov r8,r15
    tst r7,r7
    bt .Lresume
    mov #-2,r7
.Lresume:
    mov.l @(4,r15),r4
    mov #KUI_ARMADA_CE_MAPLE_RESUME_PHASE,r5
    mov.l @r15,r6
    mov.l .Lrun,r0
    jsr @r0
    nop
    bra .Ldispatch
    nop
.Lterminal:
    /* No fallthrough to the unchanged original consumer on an unresolved
     * operation. C records a first fault and attempts fence restoration. */
    bra .Lterminal
    nop
.Lreturn:
    mov.l @(8,r15),r0
    mov.l @(12,r15),r1
    lds r1,pr
    mov.l @(16,r15),r8
    mov.l @(20,r15),r1
    mov r1,r15
    ldc r0,sr
    rts
    nop
#else
    mov.l @r15+,r0
    lds.l @r15+,pr
    mov.l @r15+,r8
    mov.l @r15,r1
    mov r1,r15
    ldc r0,sr
    rts
    nop
#endif
    .align 2
.Lstack: .long __kui_maple_stack_top
.Lrun: .long _kui_armada_ce_maple_traffic_run
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
.Lworker_stack: .long __kui_vmu_worker_stack_top
.Lwork: .long _kui_armada_ce_vmu_runtime_work
.Lwait: .long _kui_armada_ce_maple_runtime_wait
.Lforbidden_sr: .long KUI_ARMADA_CE_MAPLE_CONTINUATION_SR_FORBIDDEN
.Lbridge_return: .long _kui_armada_ce_maple_bridge_return
#endif
    .global __kui_maple_traffic_shim_end
__kui_maple_traffic_shim_end:

#if KUI_ARMADA_CE_MAPLE_AUTO_STOP
    /* Terminal diagnostic transfer only. C has proved this installed vector
     * equals our resident menu hook. Its command-1 entry masks BL/IMASK and
     * replaces this helper stack before entering the existing report loop. */
    .align 2
    .global _kui_armada_ce_maple_auto_stop
    .type _kui_armada_ce_maple_auto_stop,@function
_kui_armada_ce_maple_auto_stop:
    mov.l .Lauto_vector,r0
    mov.l @r0,r0
    mov #1,r4
    jmp @r0
    nop
    .align 2
.Lauto_vector: .long 0xac0000e0
    .size _kui_armada_ce_maple_auto_stop,.-_kui_armada_ce_maple_auto_stop
#endif

    /* Immutable diagnostic selector: all modes retain the same helper code.
     * Native comparison admits only this four-byte initializer difference. */
#ifndef KUI_ARMADA_CE_MAPLE_DIAGNOSTIC_MODE
#define KUI_ARMADA_CE_MAPLE_DIAGNOSTIC_MODE 0
#endif
    .section .rodata.a2_diagnostic_mode,"a"
    .align 2
    .global _kui_armada_ce_maple_diagnostic_mode
    .type _kui_armada_ce_maple_diagnostic_mode,@object
_kui_armada_ce_maple_diagnostic_mode:
    .long KUI_ARMADA_CE_MAPLE_DIAGNOSTIC_MODE
    .size _kui_armada_ce_maple_diagnostic_mode,4

#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    /* Microsoft CE/SH requires16 writable caller-owned argument-home bytes.
     * The exact WaitForSingleObject prologue stores all four r4..r7 there.
     * This adapter is called only on the ordinary CE stack, never either
     * private stack. Preserve GBR independently of the native callee ABI. */
    .section .text.kui_armada_ce_maple_native_wait,"ax"
    .align 2
    .global _kui_armada_ce_maple_native_wait
    .type _kui_armada_ce_maple_native_wait,@function
_kui_armada_ce_maple_native_wait:
    sts.l pr,@-r15
    stc.l gbr,@-r15
    mov r4,r0
    mov r5,r4
    mov r6,r5
    add #-16,r15
    jsr @r0
    nop
    add #16,r15
    ldc.l @r15+,gbr
    lds.l @r15+,pr
    rts
    nop
    .size _kui_armada_ce_maple_native_wait,.-_kui_armada_ce_maple_native_wait
#endif
