/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact-owner observation bootstrap. No low image pointer survives
 * into temporary stage RAM. The low control is accessed through P2 only. */
#include "kui/retail_loader_layout.h"
#include "kui/vmu_maple_observe_boot.h"
#include "kui/vmu_maple_observe_profile.h"
#include "kui/vmu_native_startup_stack.h"
#if !KUI_VMU_MAPLE_OBSERVE || !KUI_RETAIL_LOW_RESIDENT || defined(KUI_RETAIL_CE) || (defined(KUI_RETAIL_ASYNC) && !KUI_VMU_NATIVE_BACKGROUND)
#error VMU observation bootstrap requires its separate native build
#endif
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)

    .section .text.vmu_observe_boot,"ax"
    .align 2
    .global C(kui_vmu_observe_heap_hook)
    .type C(kui_vmu_observe_heap_hook),@function
C(kui_vmu_observe_heap_hook):
    stc sr,r1
    mov.l .Lmask,r2
    or r1,r2
    ldc r2,sr
    mov.l r1,@-r15
    mov.l .Lcontrol,r2
    mov.l @(KUI_VMU_OBS_BOOT_STATUS,r2),r0
    cmp/eq #1,r0
    bf .Lrepeat
    mov #2,r0
    mov.l r0,@(KUI_VMU_OBS_BOOT_STATUS,r2)
    mov.l r4,@(KUI_VMU_OBS_BOOT_HEAP_BEGIN,r2)
    mov.l r5,@(KUI_VMU_OBS_BOOT_HEAP_BYTES,r2)
    mov r15,r1
    add #4,r1
    mov.l r1,@(KUI_VMU_OBS_BOOT_ENTRY_SP,r2)
    sts pr,r1
    mov.l r1,@(KUI_VMU_OBS_BOOT_ORIGINAL_PR,r2)
    mov.l r9,@(KUI_VMU_OBS_BOOT_SAVED_R9,r2)
    mov.l r10,@(KUI_VMU_OBS_BOOT_SAVED_R10,r2)
#if KUI_VMU_NATIVE_HEAP_PROBE
    mov.l .Loriginal_init,r3
    mov.l @(4,r3),r0
    cmp/eq r4,r0
    bf .Lrepeat
    mov.l @(8,r3),r0
    cmp/eq r5,r0
    bf .Lrepeat
    mov.l @(12,r3),r0
    mov.l @(KUI_VMU_OBS_BOOT_ORIGINAL_PR,r2),r1
    mov.l .Lphysical_mask,r3
    and r3,r1
    cmp/eq r1,r0
    bf .Lrepeat
    mov.l @(KUI_VMU_OBS_BOOT_ENTRY_SP,r2),r1
    and r3,r1
    mov.l .Lentry_stack_low,r0
    cmp/hs r0,r1
    bf .Lrepeat
    mov.l .Lentry_stack_high,r0
    cmp/hi r0,r1
    bt .Lrepeat
#endif
    /* Restore native SR/SP before the actual title heap initializer. */
    ldc.l @r15+,sr
    mov.l .Loriginal_init,r3
#if KUI_VMU_NATIVE_HEAP_PROBE
    mov.l @r3,r3
#endif
    jsr @r3
    nop
    /* Original initializer preserves R9/R10. Capture its exact post-call
     * state before changing T, and mask before creating a live save frame. */
    stc sr,r9
    mov.l .Lmask,r10
    or r9,r10
    ldc r10,sr
#if KUI_VMU_NATIVE_HEAP_PROBE
    /* Keep linker relaxation from pushing before interrupt masking. */
    nop
#endif
    mov.l r9,@-r15
    sts.l mach,@-r15
    sts.l macl,@-r15
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
    mov.l @(KUI_VMU_OBS_BOOT_ORIGINAL_PR,r1),r0
    mov.l r0,@-r15
    /* Publish the complete inherited frame before arbitrary native cache
     * invalidation could discard dirty owner-stack tags. BL remains held. */
    mov r15,r1
    mov r15,r2
    add #52,r2
    bsr .Lpublish_frame
    nop
    mov.l .Lcontrol,r1
    mov.l @(KUI_VMU_OBS_BOOT_SAVED_R9,r1),r9
    mov.l @(KUI_VMU_OBS_BOOT_SAVED_R10,r1),r10
    mov r15,r8
    mov.l .Lcontrol,r2
    mov.l @r2,r0
    tst r0,r0
    bt .Lmissing
    mov.l @(KUI_VMU_OBS_BOOT_STAGE_STACK,r2),r15
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
    lds.l @r15+,macl
    lds.l @r15+,mach
    /* Restore SR last, after every saved word and integer register has
     * been consumed while BL is still held. R9/R10 are already restored. */
    ldc.l @r15+,sr
    rts
    nop
.Lrepeat:
    mov #11,r1
    bra .Lfault
    nop
.Lmissing:
    mov #1,r1
.Lfault:
    mov #5,r0
    mov.l r0,@(KUI_VMU_OBS_BOOT_STATUS,r2)
    mov.l r1,@(KUI_VMU_OBS_BOOT_ERROR,r2)
    mov.l .Lreturn,r0
    jmp @r0
    nop
    .align 2
.Lcontrol: .long C(kui_vmu_observe_boot_control)+0x20000000
.Lmask: .long 0x100000f0
#if KUI_VMU_NATIVE_HEAP_PROBE
    .global __kui_native_owner_stack_ip_hook
    .set __kui_native_owner_stack_ip_hook,1
.Loriginal_init: .long C(kui_vmu_native_heap_original)+0x20000000
.Lphysical_mask: .long 0x1fffffff
.Lentry_stack_low: .long KUI_VMU_NATIVE_OWNER_STACK_LOW
.Lentry_stack_high: .long KUI_VMU_NATIVE_OWNER_STACK_HIGH
#else
.Loriginal_init: .long KUI_VMU_OBS_HEAP_INIT
#endif
.Lreturn: .long C(kui_vmu_observe_return_hook)
    .size C(kui_vmu_observe_heap_hook),.-C(kui_vmu_observe_heap_hook)

/* R1/R2 bound a small known saved frame, aliases P1 or P2 only. */
.Lpublish_frame:
    mov.l .Lalias,r3
    and r3,r1
    and r3,r2
    mov #-32,r3
    and r3,r1
1:  ocbp @r1
    add #32,r1
    cmp/hs r2,r1
    bf 1b
    rts
    nop
    .align 2
.Lalias: .long 0xdfffffff

    .align 2
    .global C(kui_vmu_observe_return_hook)
    .type C(kui_vmu_observe_return_hook),@function
C(kui_vmu_observe_return_hook):
    stc sr,r0
    mov.l .Lreturn_mask,r1
    or r1,r0
    ldc r0,sr
    mov.l .Lreturn_stack,r15
    mov.l .Lfailure,r0
    jmp @r0
    nop
    .align 2
.Lreturn_mask: .long 0x100000f0
.Lreturn_stack: .long KUI_RETAIL_HOOK_STACK+0x20000000
.Lfailure: .long C(kui_vmu_observe_low_failure)
    .size C(kui_vmu_observe_return_hook),.-C(kui_vmu_observe_return_hook)

#if KUI_VMU_MAPLE_PROFILE == KUI_VMU_MAPLE_PROFILE_SILVER
    /* The exact owner's two native BIOS-exit wrappers dereference this
     * persistent read-only cell instead of the firmware vector cell. */
    .section .rodata.vmu_observe_exit_target,"a"
    .align 2
    .global C(kui_vmu_observe_exit_target)
    .type C(kui_vmu_observe_exit_target),@object
C(kui_vmu_observe_exit_target):
    .long C(kui_vmu_observe_return_hook)
    .size C(kui_vmu_observe_exit_target),4
#endif

    .section .bss.vmu_observe_control,"aw",@nobits
    /* This object is accessed through P2 while surrounding resident state
     * uses P1. Give both ends full data-cache lines: a dirty neighbouring
     * P1 pacing line must never write stale control bytes over P2 updates. */
    .balign 32
    .global C(kui_vmu_observe_boot_control)
    .type C(kui_vmu_observe_boot_control),@object
C(kui_vmu_observe_boot_control):
    .space KUI_VMU_OBS_BOOT_BYTES
    .size C(kui_vmu_observe_boot_control),.-C(kui_vmu_observe_boot_control)
    .balign 32
#if KUI_VMU_NATIVE_HEAP_PROBE
    .global C(kui_vmu_native_heap_original)
    .type C(kui_vmu_native_heap_original),@object
C(kui_vmu_native_heap_original):
    .space 32
    .size C(kui_vmu_native_heap_original),32
    .balign 32
#endif
