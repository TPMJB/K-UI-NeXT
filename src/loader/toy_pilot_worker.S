/* SPDX-License-Identifier: GPL-3.0-only */
#define C_SYMBOL(name) _ ## name
#include "toy_pilot_frame.inc"
    .section .toy_exports,"a"
    .align 2
    .long 0x54595031,8,76
    .long C_SYMBOL(kui_toy_pilot_initialize)
    .long C_SYMBOL(kui_toy_pilot_request)
    .long C_SYMBOL(kui_toy_pilot_service_hook)
    .long C_SYMBOL(kui_toy_pilot_am_init_hook)
    .long C_SYMBOL(kui_toy_pilot_shutdown_hook)
    .long C_SYMBOL(kui_toy_pilot_snapshot)
    .long __toy_pilot_bss_begin,__toy_pilot_bss_end
    .long __toy_pilot_stack_bottom,__toy_pilot_stack_top
    .long __toy_pilot_worker_end,0x30000
    .long C_SYMBOL(kui_toy_pilot_allstop_hook)
    .long C_SYMBOL(kui_toy_pilot_driver_load_hook)
#if KUI_TOY_PILOT_SHARED_SCI
    .long C_SYMBOL(kui_toy_pilot_gd_bridge)
#else
    .long C_SYMBOL(kui_toy_pilot_gd_dispatch)
#endif
    .long C_SYMBOL(kui_toy_pilot_pause_hook)

    .text
    .align 2
    .global C_SYMBOL(kui_toy_pilot_initialize)
C_SYMBOL(kui_toy_pilot_initialize):
    /* Runs on the installer's current stack before selecting our stack. */
    mov.l .Lstack_bottom,r1
    mov.l .Lstack_top,r2
    mov.l .Lpattern,r3
1:  mov.l r3,@r1
    add #4,r1
    cmp/hs r2,r1
    bf 1b
    mov.l .Linitialize,r0
    bra .Lbridge
    nop

    .global C_SYMBOL(kui_toy_pilot_service_hook)
C_SYMBOL(kui_toy_pilot_service_hook):
    /* This replaces a verified main-context updater call. One worker visit
     * before and one after the SDK updater shorten the finite-bank setup
     * cadence and let an already latched worker fault report before an SDK
     * queue wait. Both visits use the guarded private-stack bridge. */
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_BEGIN
#endif
    sts.l pr,@-r15
    mov.l r4,@-r15
    mov.l r5,@-r15
    mov.l r6,@-r15
    mov.l r7,@-r15
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_END 20
#endif
    mov.l .Lservice,r0
    bsr .Lbridge
    nop
    mov.l @r15+,r7
    mov.l @r15+,r6
    mov.l @r15+,r5
    mov.l @r15+,r4
    /* Numeric breadcrumbs bracket the untouched original call. Keep its
     * inherited stack and argument registers exactly as they were. */
#if !KUI_TOY_PILOT_PRIVATE_P2
    mov.l r1,@-r15
    mov.l r2,@-r15
#endif
    mov.l .Lupdater_entries,r1
    mov.l @r1,r2
    add #1,r2
    mov.l r2,@r1
#if !KUI_TOY_PILOT_PRIVATE_P2
    mov.l @r15+,r2
    mov.l @r15+,r1
#endif
    mov.l .Loriginal_updater,r0
    jsr @r0
    nop
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_BEGIN
    mov.l r0,@-r15
    TOY_FRAME_END 4
#else
    mov.l r0,@-r15
    mov.l r1,@-r15
    mov.l r2,@-r15
#endif
    mov.l .Lupdater_returns,r1
    mov.l @r1,r2
    add #1,r2
    mov.l r2,@r1
#if !KUI_TOY_PILOT_PRIVATE_P2
    mov.l @r15+,r2
    mov.l @r15+,r1
#endif
    mov.l .Lservice,r0
    bsr .Lbridge
    nop
    mov.l @r15+,r0
    lds.l @r15+,pr
    rts
    nop

    .global C_SYMBOL(kui_toy_pilot_am_init_hook)
C_SYMBOL(kui_toy_pilot_am_init_hook):
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_BEGIN
#endif
    sts.l pr,@-r15
    mov.l r4,@-r15
    mov.l r5,@-r15
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_HOLD 12
#endif
    mov.l .Lrevoke,r0
    jsr @r0
    nop
#if KUI_TOY_PILOT_PRIVATE_P2
    /* This scalar revocation must run even when a worker is suspended.
     * SHARED0 has no SDK/bus callback here; its temporary C frame dies
     * under IMASK15 before the inherited interrupt opportunity returns. */
    ldc.l @r15+,sr
#endif
    mov.l @(4,r15),r4
    mov.l @r15,r5
    mov.l .Lpre_init,r0
    bsr .Lbridge
    nop
    mov.l @r15+,r5
    mov.l @r15+,r4
    mov.l .Loriginal_am_init,r0
    jsr @r0
    nop
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_BEGIN
#endif
    mov.l r0,@-r15
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_END 4
#endif
    mov r0,r4
    mov.l .Lpost_init,r0
    bsr .Lbridge
    nop
    mov.l @r15+,r0
    lds.l @r15+,pr
    rts
    nop

    .global C_SYMBOL(kui_toy_pilot_driver_load_hook)
    .type C_SYMBOL(kui_toy_pilot_driver_load_hook),@function
C_SYMBOL(kui_toy_pilot_driver_load_hook):
    /* The original loader fills the amInit destination on the inherited
     * SDK caller stack. Hash only after its successful return, before the
     * original amInit continues with SDK initialization/driver install. */
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_BEGIN
#endif
    sts.l pr,@-r15
    mov.l r4,@-r15
    mov.l r5,@-r15
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_END 12
#endif
    mov.l .Loriginal_driver_load,r0
    jsr @r0
    nop
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_BEGIN
#endif
    mov.l r0,@-r15
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_END 4
#endif
    mov r0,r4
    mov.l @(4,r15),r5
    mov.l .Lpost_load,r0
    bsr .Lbridge
    nop
    mov.l @r15+,r0
    mov.l @r15+,r5
    mov.l @r15+,r4
    lds.l @r15+,pr
    rts
    nop
    .size C_SYMBOL(kui_toy_pilot_driver_load_hook),.-C_SYMBOL(kui_toy_pilot_driver_load_hook)

    .global C_SYMBOL(kui_toy_pilot_shutdown_hook)
C_SYMBOL(kui_toy_pilot_shutdown_hook):
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_BEGIN
#endif
    sts.l pr,@-r15
    mov.l r4,@-r15
    mov.l r5,@-r15
    mov.l r6,@-r15
    mov.l r7,@-r15
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_HOLD 20
#endif
    mov.l .Lshutdown,r0
    jsr @r0
    nop
#if KUI_TOY_PILOT_PRIVATE_P2
    ldc.l @r15+,sr
#endif
    mov.l @r15+,r7
    mov.l @r15+,r6
    mov.l @r15+,r5
    mov.l @r15+,r4
    lds.l @r15+,pr
    mov.l .Loriginal_shutdown,r0
    jmp @r0
    nop

    .global C_SYMBOL(kui_toy_pilot_allstop_hook)
C_SYMBOL(kui_toy_pilot_allstop_hook):
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_BEGIN
#endif
    sts.l pr,@-r15
    mov.l r4,@-r15
    mov.l r5,@-r15
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_HOLD 12
#endif
    mov.l .Lallstop,r0
    jsr @r0
    nop
#if KUI_TOY_PILOT_PRIVATE_P2
    ldc.l @r15+,sr
#endif
    mov.l @r15+,r5
    mov.l @r15+,r4
    lds.l @r15+,pr
    mov.l .Loriginal_allstop,r0
    jmp @r0
    nop

/* Integer ABI stack bridge. Saving SR, PR and r8..r14 costs 36 caller bytes.
 * The anchor is 16 destination bytes. Exact entry SR is restored on return;
 * FPU/FPSCR are untouched by the authored integer code. */
    .global C_SYMBOL(kui_toy_pilot_worker_bridge)
    .type C_SYMBOL(kui_toy_pilot_worker_bridge),@function
C_SYMBOL(kui_toy_pilot_worker_bridge):
.Lbridge:
    /* Claim before selecting the private stack. An interrupt/recursive call
     * cannot overwrite the suspended bridge's top-of-stack anchor. */
    stc sr,r1
    mov.l .Lmask,r2
    or r1,r2
    ldc r2,sr
    mov.l .Lbridge_active,r2
    mov.l @r2,r3
    tst r3,r3
    bt 2f
    mov.l .Lbridge_skips,r2
    mov.l @r2,r3
    add #1,r3
    mov.l r3,@r2
    ldc r1,sr
    mov #0,r0
    rts
    nop
2:  mov #1,r3
    mov.l r3,@r2
#if !KUI_TOY_PILOT_PRIVATE_P2
    ldc r1,sr
#endif
    mov.l r1,@-r15
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_PUBLISH 36
#endif
    mov r15,r8
    mov.l .Lstack_top,r15
    add #-16,r15
    mov.l r8,@r15
#if KUI_TOY_PILOT_PRIVATE_P2
    mov.l @(32,r8),r1
    ldc r1,sr
#endif
    jsr @r0
    nop
    mov.l @r15,r8
    mov r8,r15
    mov.l @r15+,r14
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    lds.l @r15+,pr
    stc sr,r1
    mov.l .Lmask,r2
    or r1,r2
    ldc r2,sr
    mov.l .Lbridge_active,r2
    mov #0,r3
    mov.l r3,@r2
    mov.l @r15+,r1
    ldc r1,sr
    rts
    nop
    .size C_SYMBOL(kui_toy_pilot_worker_bridge),.-C_SYMBOL(kui_toy_pilot_worker_bridge)
    .align 2
.Lstack_bottom: .long __toy_pilot_stack_bottom
.Lstack_top: .long __toy_pilot_stack_top
.Lpattern: .long 0xa55a5aa5
.Lmask: .long 0x000000f0
.Lbridge_active: .long C_SYMBOL(kui_toy_pilot_bridge_active)
.Lbridge_skips: .long C_SYMBOL(kui_toy_pilot_bridge_skips)
.Lupdater_entries: .long C_SYMBOL(kui_toy_pilot_updater_entries)
.Lupdater_returns: .long C_SYMBOL(kui_toy_pilot_updater_returns)
.Linitialize: .long C_SYMBOL(kui_toy_pilot_worker_initialize)
.Lservice: .long C_SYMBOL(kui_toy_pilot_worker_step)
.Lpre_init: .long C_SYMBOL(kui_toy_pilot_worker_pre_init)
.Lpost_load: .long C_SYMBOL(kui_toy_pilot_worker_post_load)
.Lrevoke: .long C_SYMBOL(kui_toy_pilot_worker_revoke)
.Lpost_init: .long C_SYMBOL(kui_toy_pilot_worker_post_init)
.Lshutdown: .long C_SYMBOL(kui_toy_pilot_worker_shutdown)
.Lallstop: .long C_SYMBOL(kui_toy_pilot_worker_allstop)
.Loriginal_updater: .long 0x8c04a270
.Loriginal_am_init: .long 0x8c06aa86
.Loriginal_driver_load: .long 0x8c06ac54
.Loriginal_shutdown: .long 0x8c06aa0e
.Loriginal_allstop: .long 0x8c068a96

#if KUI_TOY_PILOT_PRIVATE_P2
    TOY_FRAME_HELPER C_SYMBOL(kui_toy_pilot_frame_publish_worker)
#endif

#if KUI_TOY_PILOT_SHARED_SCI
/* Serialized low GD entry owns the lock and BL mask. Copy its fifth C
 * argument onto the independent high GD stack before calling the adapter.
 * The audio stack may be suspended when a game interrupt calls GD. */
    .text
    .align 2
    .global C_SYMBOL(kui_toy_pilot_gd_bridge)
    .type C_SYMBOL(kui_toy_pilot_gd_bridge),@function
C_SYMBOL(kui_toy_pilot_gd_bridge):
    mov.l @r15,r0
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov r15,r8
    mov.l .Lgd_guard,r1
    mov.l @r1,r2
    mov.l .Lgd_pattern,r3
    cmp/eq r2,r3
    bf .Lgd_bad
    mov.l .Lgd_top,r15
    mov.l r0,@-r15
    mov.l .Lgd_dispatch,r0
    jsr @r0
    nop
    mov.l .Lgd_guard,r1
    mov.l @r1,r2
    mov.l .Lgd_pattern,r3
    cmp/eq r2,r3
    bt .Lgd_return
.Lgd_bad:
    mov #-1,r0
.Lgd_return:
    mov r8,r15
    mov.l @r15+,r9
    mov.l @r15+,r8
    lds.l @r15+,pr
    rts
    nop
    .align 2
.Lgd_guard: .long __toy_pilot_gd_stack_bottom
.Lgd_top: .long __toy_pilot_gd_stack_top
.Lgd_pattern: .long 0xa55a4aa5
.Lgd_dispatch: .long C_SYMBOL(kui_toy_pilot_gd_dispatch)
    .size C_SYMBOL(kui_toy_pilot_gd_bridge),.-C_SYMBOL(kui_toy_pilot_gd_bridge)
#endif
