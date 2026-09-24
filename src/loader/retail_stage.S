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
    mov.l .Lbootsr,r0
    ldc r0,ssr
    ldc r0,sr
    mov.l .Lbootgbr,r0
    ldc r0,gbr
    mov.l .Lbootstack,r15
    ldc r15,vbr
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
.Lbootsr: .long 0x700000f0
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
    .section .resident_blob,"a"
    .align 2
    .global __retail_resident_blob_start,__retail_resident_blob_end
__retail_resident_blob_start:
    .incbin "build/retail/resident.bin"
    .align 2
__retail_resident_blob_end:
