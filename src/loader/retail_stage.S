/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_loader_layout.h"
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
    .section .entry,"ax"
    .align 2
    .global _start
_start:
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

    .text
    .align 2
    .global C(kui_retail_bootstrap_enter)
C(kui_retail_bootstrap_enter):
    mov.l .Lbootstack,r15
    mov.l .Lboot1,r0
    jmp @r0
    nop
    .align 2
.Lbootstack: .long KUI_RETAIL_BOOT_VBR
.Lboot1: .long KUI_RETAIL_BOOT1_ADDRESS+0x20000000

/* This template runs at the owner's executable entry. Save every integer
 * register used by C, preserving the owner's bootstrap result. The entire
 * freestanding experiment is built -m4-nofpu, so FP state is never touched.
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
.Lgame: .long KUI_RETAIL_EXEC_ADDRESS
    .section .resident_blob,"a"
    .align 2
    .global __retail_resident_blob_start,__retail_resident_blob_end
__retail_resident_blob_start:
    .incbin "build/retail/resident.bin"
    .align 2
__retail_resident_blob_end:
