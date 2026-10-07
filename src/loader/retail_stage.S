/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_loader_layout.h"
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
    .section .entry,"ax"
    .align 2
    .global _start
_start:
/* arch_exec leaves both caches off. Enable them from P2 before the serial
 * byte loop and CRC work, then execute C through its cached P1 address. */
    mov.l .Lcache_setup,r0
    jmp @r0
    nop
.Lcache_setup_p2:
    mov.l .Lstart_ccr,r0
    mov.l .Lstart_cache,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov.l .Lcached_start,r0
    jmp @r0
    nop
.Lcached:
    mov.l .Lbss,r0
    mov.l .Lend,r1
    mov #0,r2
.Lzero:
    cmp/hs r1,r0
    bt .Lready
    mov.l r2,@r0
    add #4,r0
    bra .Lzero
    nop
.Lready:
    mov.l .Lmain,r0
    jsr @r0
    nop
.Lhalt:
    bra .Lhalt
    nop
    .align 2
.Lbss: .long __retail_stage_bss_begin
.Lend: .long __retail_stage_bss_end
.Lmain: .long C(kui_retail_stage_main)
.Lcache_setup: .long .Lcache_setup_p2+0x20000000
.Lcached_start: .long .Lcached
.Lstart_ccr: .long 0xff00001c
.Lstart_cache: .long 0x00000909

    .text
    .align 2
    .global C(kui_retail_bootstrap_enter)
C(kui_retail_bootstrap_enter):
/* Owner bootstrap 2 is the normal ISO-loader entry. Prime the retail CPU
 * state explicitly; none of the departed KOS register state is an ABI.
 * Register profile informed by DreamShell ISO Loader startup.s (GPL-3.0),
 * Copyright (C) 2009-2016, 2025-2026 SWAT, based on Lawrence Sebald's
 * Sylverant PSO Patcher. This implements that CPU interface in our stage.
 * This one-time FPSCR initialization is the only permitted FP-state write;
 * the trampoline, relay and resident still preserve the game's FPU state. */
    mov.l .Lboot_p2,r0
    jmp @r0
    nop
.Lboot_uncached:
    mov.l .Lbootram,r0
    mov.l .Lbootramend,r1
.Lboot_purge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lboot_purge
    mov.l .Lbootccr,r0
    mov.l .Lbootcache,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
#ifdef KUI_RETAIL_CE
    /* Windows CE boot test: bootstrap 2 runs under the stage's exception
     * table (below) with SR.BL clear, so a fault there is reported instead
     * of resetting the console. The relay restores the boot VBR. */
    mov.l .Lce_vbr,r0
    ldc r0,vbr
#endif
    mov.l .Lbootsr,r0
    ldc r0,ssr
    ldc r0,sr
    mov.l .Lbootgbr,r0
    ldc r0,gbr
    mov.l .Lbootstack,r15
#ifndef KUI_RETAIL_CE
    ldc r15,vbr
#endif
    mov.l .Lbootfpscr,r0
    .global __retail_boot_fpscr_init
__retail_boot_fpscr_init:
    lds r0,fpscr
    mov.l .Lboot2,r14
    ldc r14,spc
    mov.l .Lboot_return,r0
    lds r0,pr
    mov #0,r0
    mov #0,r1
    mov #0,r2
    mov #0,r3
    mov #0,r4
    mov #0,r5
    mov #0,r6
    mov #0,r7
    mov #0,r8
    mov #0,r9
    mov #0,r10
    mov #0,r11
    mov #0,r12
    mov #0,r13
    clrmac
    clrt
    jmp @r14
    mov #0,r14
.Lboot_returned:
    mov.l .Lhighstack,r15
    mov.l .Lreturn_handler,r0
    jmp @r0
    nop
    .align 2
.Lboot_p2: .long .Lboot_uncached+0x20000000
.Lbootram: .long 0x8c000000
.Lbootramend: .long KUI_RETAIL_RAM_END
.Lbootccr: .long 0xff00001c
.Lbootcache: .long 0x00000909
#ifdef KUI_RETAIL_CE
.Lbootsr: .long 0x600000f0 /* MD, RB, IMASK 15; BL clear (see above) */
.Lce_vbr: .long __retail_ce_vbr
#else
.Lbootsr: .long 0x700000f0
#endif
.Lbootgbr: .long 0x8c000000
.Lbootfpscr: .long 0x00040001
.Lbootstack: .long KUI_RETAIL_BOOT_VBR
.Lboot2: .long KUI_RETAIL_BOOT2_ADDRESS+0x20000000
.Lboot_return: .long .Lboot_returned
.Lreturn_handler: .long C(kui_retail_boot_returned)

/* Publish newly copied code before calling the resident. The temporary stage
 * has no cache-as-RAM usage; all RAM dirty lines are written back first. */
    .align 2
    .global C(kui_retail_stage_sync)
C(kui_retail_stage_sync):
    mov.l .Lsync_p2,r0
    jmp @r0
    nop
.Lsync_uncached:
    mov.l .Lsync_begin,r0
    mov.l .Lsync_end,r1
.Lsync_loop:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lsync_loop
    mov.l .Lsync_ccr,r0
    mov.l .Lsync_cache,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    rts
    nop
    .align 2
.Lsync_p2: .long .Lsync_uncached+0x20000000
.Lsync_begin: .long 0x8c000000
.Lsync_end: .long KUI_RETAIL_RAM_END
.Lsync_ccr: .long 0xff00001c
.Lsync_cache: .long 0x00000909

/* This template runs at the owner's executable entry. Save every integer
 * register used by C, preserving the owner's bootstrap result. The entire
 * freestanding experiment reserves FPU registers and uses integer division;
 * a linked machine-code audit rejects instructions that touch FP state.
 * The preserved frame is 21 words: MACL,MACH,GBR,VBR,SR,PR,r14..r0. */
    .section .trampoline,"ax"
    .align 2
    .global __retail_trampoline_start,__retail_trampoline_end
__retail_trampoline_start:
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
    stc sr,r0
    or #0xf0,r0
    ldc r0,sr
    mov.l .Lrelay,r0
    jmp @r0
    nop
    .align 2
.Lrelay: .long C(kui_retail_game_resume)+0x20000000
/* Windows CE keeps its body's "ECEC" signature and ROM header pointer at
 * offset 0x40 while bootstrap 2 runs: that stage places only the first 64
 * bytes, so all of the code and its literal must fit in them (checked by
 * retail_stage_ce.ld). */
    .global __retail_trampoline_code_end
__retail_trampoline_code_end:
    .space 128-(.-__retail_trampoline_start)
__retail_trampoline_end:

    .text
    .align 2
    .global C(kui_retail_game_resume)
C(kui_retail_game_resume):
    mov r15,r8
    mov.l .Lhighstack,r15
    mov.l .Lccr,r0
    mov.l @r0,r9
/* Execute through P2. Purge dirty RAM lines before disabling cache, including
 * the frame and bootstrap modifications. No stale cached alias may overwrite
 * the newly installed resident or restored executable later. */
    mov.l .Lram,r0
    mov.l .Lramend,r1
.Lpurge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lpurge
    mov.l .Lccr,r0
    mov.l .Loff,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov r8,r4
    mov r9,r5
    mov.l .Lrelaymain,r0
    jsr @r0
    nop
/* C returns only after exact executable restoration and resident installation.
 * Invalidate before restoring the owner's cache enable/mode bits through P2. */
    mov.l .Loff,r0
    or r9,r0
    mov.l .Lccr,r1
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov r8,r15
    lds.l @r15+,macl
    lds.l @r15+,mach
    ldc.l @r15+,gbr
    ldc.l @r15+,vbr
/* Delay restoring SR until other state is restored. Keep it in the original
 * frame, load via r0 after all other registers; no intervening flag changes. */
    add #4,r15
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
    mov r15,r0
    add #-64,r0
    mov.l @r0,r0
    ldc r0,sr
    mov.l .Lgame,r0
    jmp @r0
    mov.l @r15+,r0
    .align 2
.Lhighstack: .long KUI_RETAIL_STAGE_STACK
.Lccr: .long 0xff00001c
.Lram: .long 0x8c000000
.Lramend: .long KUI_RETAIL_RAM_END
.Loff: .long 0x00000808
.Lrelaymain: .long C(kui_retail_stage_relay)
/* Owner bootstrap2 calls the uncached executable alias. Resume that same
 * alias while the original entry performs its own cache setup. */
.Lgame: .long KUI_RETAIL_EXEC_ADDRESS+0x20000000

#if defined(KUI_RETAIL_SONIC_STACK_TEST) && KUI_RETAIL_SONIC_STACK_TEST && !defined(KUI_RETAIL_CE)
/* All seven original-entry stubs have saved R0 before selecting P2. Save
 * the rest of the real integer CPU frame BEFORE using high bookkeeping.
 * No FPU instruction is used anywhere in this freestanding stage. */
    .macro SONIC_STACK_SAVE_REST
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
    .endm
    .macro SONIC_STACK_ENTRY name,point
    .align 2
    .global C(\name)
C(\name):
    SONIC_STACK_SAVE_REST
    mov #\point,r10
    bra .Lsonic_stack_saved
    nop
    .endm
    SONIC_STACK_ENTRY kui_retail_sonic_g2_fifo,0
    SONIC_STACK_ENTRY kui_retail_sonic_g2_busy,1
    SONIC_STACK_ENTRY kui_retail_sonic_scope_entry,2
    SONIC_STACK_ENTRY kui_retail_sonic_asset_guard,3
    SONIC_STACK_ENTRY kui_retail_sonic_texture_entry,4
    SONIC_STACK_ENTRY kui_retail_sonic_compressed_guard,5
    SONIC_STACK_ENTRY kui_retail_sonic_decode_guard,6
.Lsonic_stack_saved:
    stc sr,r0
    or #0xf0,r0
    ldc r0,sr
    mov r15,r8
    mov.l .Lsonic_stack_book,r15
    mov.l .Lsonic_stack_ccr,r0
    mov.l @r0,r9
    mov.l .Lsonic_stack_ram,r0
    mov.l .Lsonic_stack_ramend,r1
.Lsonic_stack_purge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lsonic_stack_purge
    mov.l .Lsonic_stack_ccr,r0
    mov.l .Lsonic_stack_off,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov r8,r4
    mov r10,r5
    mov r9,r6
    mov #4,r0
    cmp/hs r0,r10
    bf .Lsonic_stack_first
    mov.l .Lsonic_texture_state,r7
    bra .Lsonic_stack_call
    nop
.Lsonic_stack_first:
    mov.l .Lsonic_stack_state,r7
.Lsonic_stack_call:
    mov.l .Lsonic_stack_before,r0
    jsr @r0
    nop
    mov r0,r8
/* A normal G2 checkpoint returns its untouched native frame. The scope
 * returns the copied P1 private frame, whose PR alone names our return.
 * Restore caches before popping that frame through its coherent alias. */
    mov.l .Lsonic_stack_off,r0
    or r9,r0
    mov.l .Lsonic_stack_ccr,r1
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    bra .Lsonic_stack_delegate
    nop
    .align 2
.Lsonic_stack_book: .long C(kui_retail_sonic_book_stack)+4096+0x20000000
.Lsonic_stack_state: .long C(kui_retail_sonic_stack_state)+0x20000000
.Lsonic_texture_state: .long C(kui_retail_sonic_texture_state)+0x20000000
.Lsonic_stack_ccr: .long 0xff00001c
.Lsonic_stack_ram: .long 0x8c000000
.Lsonic_stack_ramend: .long KUI_RETAIL_RAM_END
.Lsonic_stack_off: .long 0x00000808
.Lsonic_stack_before: .long C(kui_retail_sonic_stack_checkpoint)+0x20000000
/* Pop every word of the chosen actual frame. SR is restored after all
 * decisions and flag-changing instructions, and R0 in the final delay slot. */
    .macro SONIC_STACK_RESTORE
    mov r8,r15
    lds.l @r15+,macl
    lds.l @r15+,mach
    ldc.l @r15+,gbr
    ldc.l @r15+,vbr
    add #4,r15
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
    mov r15,r0
    add #-64,r0
    mov.l @r0,r0
    ldc r0,sr
    .endm
.Lsonic_stack_delegate:
    SONIC_STACK_RESTORE
    mov.l .Lsonic_stack_resume,r0
    mov.l @r0,r0
    jmp @r0
    mov.l @r15+,r0
    .align 2
.Lsonic_stack_resume: .long C(kui_retail_sonic_stack_resume)+0x20000000
    .align 2
    .global C(kui_retail_sonic_scope_return)
C(kui_retail_sonic_scope_return):
/* Save the REAL callee results on the private owner stack. This preserves
 * changes to R0, SR and all other CPU words, instead of replaying entry state.
 * Only IMASK is briefly masked; CCR and all FPU state remain untouched. */
    mov.l r0,@-r15
    SONIC_STACK_SAVE_REST
    stc sr,r0
    or #0xf0,r0
    ldc r0,sr
    mov r15,r8
    mov.l .Lsonic_return_book,r15
    mov r8,r4
    mov.l .Lsonic_return_ccr,r0
    mov.l @r0,r5
    mov.l .Lsonic_return_state,r6
    mov.l .Lsonic_return_after,r0
    jsr @r0
    nop
    mov r0,r8
    SONIC_STACK_RESTORE
    rts
    mov.l @r15+,r0
    .align 2
.Lsonic_return_book: .long C(kui_retail_sonic_book_stack)+4096+0x20000000
.Lsonic_return_ccr: .long 0xff00001c
.Lsonic_return_state: .long C(kui_retail_sonic_stack_state)+0x20000000
.Lsonic_return_after: .long C(kui_retail_sonic_stack_after)+0x20000000
    .align 2
    .global C(kui_retail_sonic_texture_return)
C(kui_retail_sonic_texture_return):
/* Distinct backing preserves the earlier flash helper's escaped workspace.
 * Capture actual returned CPU state before any bookkeeping or cache work. */
    mov.l r0,@-r15
    SONIC_STACK_SAVE_REST
    stc sr,r0
    or #0xf0,r0
    ldc r0,sr
    mov r15,r8
    mov.l .Ltexture_return_book,r15
    mov.l .Ltexture_return_ccr,r0
    mov.l @r0,r9
    mov r8,r4
    mov r9,r5
    mov.l .Ltexture_return_state,r6
    mov.l .Ltexture_return_after,r0
    jsr @r0
    nop
    mov r0,r8
/* C has checked immutable RAM through both aliases before this purge. On
 * early return, a write guard may never have run: publish its restored code
 * and invalidate both caches before any later owner call can fetch it. */
    mov.l .Ltexture_return_ram,r0
    mov.l .Ltexture_return_ramend,r1
.Ltexture_return_purge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Ltexture_return_purge
    mov.l .Ltexture_return_off,r0
    or r9,r0
    mov.l .Ltexture_return_ccr,r1
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    SONIC_STACK_RESTORE
    rts
    mov.l @r15+,r0
    .align 2
.Ltexture_return_book: .long C(kui_retail_sonic_book_stack)+4096+0x20000000
.Ltexture_return_ccr: .long 0xff00001c
.Ltexture_return_state: .long C(kui_retail_sonic_texture_state)+0x20000000
.Ltexture_return_after: .long C(kui_retail_sonic_stack_after)+0x20000000
.Ltexture_return_ram: .long 0x8c000000
.Ltexture_return_ramend: .long KUI_RETAIL_RAM_END
.Ltexture_return_off: .long 0x00000808
#endif

#if defined(KUI_RETAIL_STARTUP_TRACE) && KUI_RETAIL_STARTUP_TRACE && !defined(KUI_RETAIL_CE)
/* The RAM-only 12-byte checkpoint stub has already saved original R0.
 * No owner instruction is copied into this image. Each wrapper saves the
 * remaining relay frame before assigning a checkpoint number. */
    .macro STARTUP_TRACE_ENTRY name,point
    .align 2
    .global C(\name)
C(\name):
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
    mov #\point,r10
    bra .Lstartup_trace_saved
    nop
    .endm
    STARTUP_TRACE_ENTRY kui_retail_startup_trace_scan,0
    STARTUP_TRACE_ENTRY kui_retail_startup_trace_g2,1
    STARTUP_TRACE_ENTRY kui_retail_startup_trace_pvr,2
    STARTUP_TRACE_ENTRY kui_retail_startup_trace_gd,3
#if KUI_RETAIL_STARTUP_TRACE >= 3
    STARTUP_TRACE_ENTRY kui_retail_startup_trace_mount,4
#endif
.Lstartup_trace_saved:
    stc sr,r0
    or #0xf0,r0
    ldc r0,sr
    mov r15,r8
    mov.l .Lstartup_trace_stack,r15
    mov.l .Lstartup_trace_ccr,r0
    mov.l @r0,r9
/* Publish dirty owner frame/heap state before disabling caches. None of the
 * first three successful checkpoints writes scanout registers or VRAM. */
    mov.l .Lstartup_trace_ram,r0
    mov.l .Lstartup_trace_ramend,r1
.Lstartup_trace_purge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lstartup_trace_purge
    mov.l .Lstartup_trace_ccr,r0
    mov.l .Lstartup_trace_off,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov r8,r4
    mov r10,r5
    mov r9,r6
    mov.l .Lstartup_trace_main,r0
    jsr @r0
    nop
/* Restore the owner cache mode with both caches invalidated. The saved
 * trace state and restored instructions were written with caches disabled. */
    mov.l .Lstartup_trace_off,r0
    or r9,r0
    mov.l .Lstartup_trace_ccr,r1
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov r8,r15
    lds.l @r15+,macl
    lds.l @r15+,mach
    ldc.l @r15+,gbr
    ldc.l @r15+,vbr
    add #4,r15
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
    mov r15,r0
    add #-64,r0
    mov.l @r0,r0
    ldc r0,sr
    mov.l .Lstartup_trace_resume,r0
    mov.l @r0,r0
    jmp @r0
    mov.l @r15+,r0
    .align 2
.Lstartup_trace_stack: .long C(kui_retail_startup_trace_stack)+4096+0x20000000
.Lstartup_trace_ccr: .long 0xff00001c
.Lstartup_trace_ram: .long 0x8c000000
.Lstartup_trace_ramend: .long KUI_RETAIL_RAM_END
.Lstartup_trace_off: .long 0x00000808
.Lstartup_trace_main: .long C(kui_retail_startup_trace_checkpoint)+0x20000000
.Lstartup_trace_resume: .long C(kui_retail_startup_trace_resume)+0x20000000
#if KUI_RETAIL_STARTUP_TRACE >= 2
/* Transparent two-phase BC observer. Each phase saves the full integer CPU
 * frame on the native caller stack before briefly masking IMASK. CCR, BL,
 * register banks and all FPU state stay unchanged. Bookkeeping uses a P2
 * high stack/state; the saved native handler runs on the ORIGINAL stack.
 * An interrupt reentry while active bypasses bookkeeping and reaches the
 * real handler with its original PR, preserving its own lock/busy policy. */
    .macro STARTUP_GD_SAVE
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
    stc sr,r0
    or #0xf0,r0
    ldc r0,sr
    .endm
/* Restore all words except R0, popped by the final jump/return delay slot.
 * The original SR is the word 64 bytes below that final saved R0 slot. */
    .macro STARTUP_GD_RESTORE
    mov r8,r15
    lds.l @r15+,macl
    lds.l @r15+,mach
    ldc.l @r15+,gbr
    ldc.l @r15+,vbr
    add #4,r15
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
    mov r15,r0
    add #-64,r0
    mov.l @r0,r0
    ldc r0,sr
    .endm
    .align 2
    .global C(kui_retail_startup_gd_proxy)
C(kui_retail_startup_gd_proxy):
    STARTUP_GD_SAVE
    mov r15,r8
    mov.l .Lstartup_gd_state,r9
    mov.l @(4,r9),r0
    tst r0,r0
    bf .Lstartup_gd_delegate
    mov #1,r0
    mov.l r0,@(4,r9)
    mov.l .Lstartup_gd_stack,r15
    mov r8,r4
    mov r9,r5
    mov.l .Lstartup_gd_ccr,r0
    mov.l @r0,r6
    mov.l .Lstartup_gd_before,r0
    jsr @r0
    nop
.Lstartup_gd_delegate:
    STARTUP_GD_RESTORE
    mov.l .Lstartup_gd_state,r0
    mov.l @r0,r0
    jmp @r0
    mov.l @r15+,r0
    .align 2
    .global C(kui_retail_startup_gd_return)
C(kui_retail_startup_gd_return):
    STARTUP_GD_SAVE
    mov r15,r8
    mov.l .Lstartup_gd_state,r5
    mov.l .Lstartup_gd_stack,r15
    mov r8,r4
    mov.l .Lstartup_gd_ccr,r0
    mov.l @r0,r6
    mov.l .Lstartup_gd_after,r0
    jsr @r0
    nop
    STARTUP_GD_RESTORE
    rts
    mov.l @r15+,r0
    .align 2
.Lstartup_gd_state: .long C(kui_retail_startup_gd_state)+0x20000000
.Lstartup_gd_stack: .long C(kui_retail_startup_trace_stack)+4096+0x20000000
.Lstartup_gd_ccr: .long 0xff00001c
.Lstartup_gd_before: .long C(kui_retail_startup_gd_before)+0x20000000
.Lstartup_gd_after: .long C(kui_retail_startup_gd_after)+0x20000000
#endif
#endif
    .section .resident_blob,"a"
    .align 2
    .global __retail_resident_scif_blob_start,__retail_resident_scif_blob_end
__retail_resident_scif_blob_start:
    .incbin "build/retail/resident-scif.bin"
    .align 2
__retail_resident_scif_blob_end:
    .section .resident_blob,"a"
    .align 2
    .global __retail_resident_sci_blob_start,__retail_resident_sci_blob_end
__retail_resident_sci_blob_start:
#ifdef KUI_RETAIL_CE
    /* The Windows CE boot test's CE-safe SCI resident. */
    .incbin "build/retail-ce/resident-sci.bin"
#else
    .incbin "build/retail/resident-sci.bin"
#endif
    .align 2
__retail_resident_sci_blob_end:
    .section .resident_blob,"a"
    .align 2
    .global __retail_resident_ide_blob_start,__retail_resident_ide_blob_end
__retail_resident_ide_blob_start:
    .incbin "build/retail/resident-ide.bin"
    .align 2
__retail_resident_ide_blob_end:
    .section .resident_blob,"a"
    .align 2
    .global __retail_resident_scia_blob_start,__retail_resident_scia_blob_end
__retail_resident_scia_blob_start:
#ifdef KUI_RETAIL_CE
    /* The Windows CE boot test's background reader. */
    .incbin "build/retail-ce/resident-scia.bin"
#else
    .incbin "build/retail/resident-scia.bin"
#endif
    .align 2
__retail_resident_scia_blob_end:

#ifdef KUI_RETAIL_CE
/* Windows CE boot test: the exception table bootstrap 2 runs under, matching
 * the native entry's SR.BL=1 except that a fault is reported instead of
 * resetting the console. Bootstrap 2 runs in register bank 1, the same bank
 * an exception selects, so no stub that returns may change a general
 * register: an interrupt (possible only if bootstrap 2 lowers IMASK; BL=1
 * would have held it) parks R0 in DBR (used only by H-UDI/user-break
 * debugging), raises the interrupted IMASK back to 15 and returns, leaving
 * the interrupt pending as BL=1 would. Any exception (vector 1 general, 4 TLB
 * miss) records its vector, SPC, SSR, R15 (SGR) and PR and reports from the
 * stage's stack: kui_retail_stage_exception. */
    .section .text.retail_ce_vectors,"ax"
    .balign 32
    .global __retail_ce_vbr
__retail_ce_vbr:
    .org 0x100
    bra .Lce_catch
    mov #1,r4
    .org 0x400
    bra .Lce_catch
    mov #4,r4
    .org 0x600
    ldc r0,dbr
    stc ssr,r0
    or #0xf0,r0
    ldc r0,ssr
    stc dbr,r0
    rte
    nop
.Lce_catch:
    mov.l .Lce_fault,r0
    mov.l r4,@r0
    stc spc,r1
    mov.l r1,@(4,r0)
    stc ssr,r1
    mov.l r1,@(8,r0)
    stc sgr,r1
    mov.l r1,@(12,r0)
    sts pr,r1
    mov.l r1,@(16,r0)
    mov.l .Lce_stack,r15
    mov.l .Lce_report,r0
    jmp @r0
    nop
    .align 2
.Lce_fault: .long C(kui_retail_ce_fault)
.Lce_stack: .long KUI_RETAIL_STAGE_STACK
.Lce_report: .long C(kui_retail_stage_exception)
#endif
