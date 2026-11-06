/* SPDX-License-Identifier: GPL-3.0-only */
/* Physical-disc virtual-VMU stage. The startup and native bootstrap handoff
 * reuse K-UI's console-validated physical-disc stage. The executable relay
 * preserves the same owner-state ABI as K-UI's native VMU image stage. */
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
    .section .entry,"ax"
    .align 2
    .global _start
_start:
    mov.l .Lcache_setup,r0
    jmp @r0
    nop
.Lcache_setup_p2:
    mov.l .Lccr,r0
    mov.l .Lcache,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov.l .Lcached,r0
    jmp @r0
    nop
.Lcached_entry:
    mov.l .Lbss,r0
    mov.l .Lbss_end,r1
    mov #0,r2
.Lclear:
    cmp/hs r1,r0
    bt .Lready
    mov.l r2,@r0
    add #4,r0
    bra .Lclear
    nop
.Lready:
    mov.l .Lmain,r0
    jsr @r0
    nop
.Lstopped:
    bra .Lstopped
    nop
    .align 2
.Lcache_setup: .long .Lcache_setup_p2+0x20000000
.Lcached: .long .Lcached_entry
.Lccr: .long 0xff00001c
.Lcache: .long 0x00000909
.Lbss: .long __disc_vmu_stage_bss_begin
.Lbss_end: .long __disc_vmu_stage_bss_end
.Lmain: .long C(kui_disc_vmu_stage_main)

    .text
    .align 2
    .global C(kui_disc_vmu_stage_enter)
C(kui_disc_vmu_stage_enter):
    mov.l .Luncached,r0
    jmp @r0
    nop
.Lenter_p2:
    mov.l .Lram_begin,r0
    mov.l .Lram_end,r1
.Lpublish:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lpublish
    mov.l .Lboot_ccr,r0
    mov.l .Lboot_cache,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov.l .Lsr,r0
    ldc r0,ssr
    ldc r0,sr
    mov.l .Lgbr,r0
    ldc r0,gbr
    mov.l .Lowner_stack,r15
    ldc r15,vbr
    mov.l .Lfpscr,r0
    /* Shared K-UI instruction-audit witness: this one-time initialization is
     * the only FPU-control instruction permitted in a bootstrap stage. */
    .global __retail_boot_fpscr_init
__retail_boot_fpscr_init:
    lds r0,fpscr
    mov.l .Lreturn,r0
    lds r0,pr
    mov.l .Lbootstrap,r14
    ldc r14,spc
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
.Lreturned:
    mov.l .Lhigh_stack,r15
    mov.l .Lreturn_handler,r0
    jmp @r0
    nop
    .align 2
.Luncached: .long .Lenter_p2+0x20000000
.Lram_begin: .long 0x8c000000
.Lram_end: .long 0x8d000000
.Lboot_ccr: .long 0xff00001c
.Lboot_cache: .long 0x00000909
.Lsr: .long 0x700000f0
.Lgbr: .long 0x8c000000
.Lowner_stack: .long 0x8c00f400
.Lfpscr: .long 0x00040001
/* Bootstrap 2 is IP+0x6000. IP+0x300 is the lower bootstrap and may run
 * firmware disc boot again instead of entering our already staged game. */
.Lbootstrap: .long 0xac00e000
.Lreturn: .long .Lreturned
.Lhigh_stack: .long 0x8cff0000
.Lreturn_handler: .long C(kui_disc_vmu_stage_returned)

/* This template runs at the owner's executable entry. Save every integer
 * register used by C, preserving the owner's bootstrap result. The entire
 * freestanding experiment reserves FPU registers and uses integer division;
 * a linked machine-code audit rejects instructions that touch FP state.
 * The preserved frame is 21 words: MACL,MACH,GBR,VBR,SR,PR,r14..r0. */
    .section .trampoline,"ax"
    .align 2
    .global __disc_vmu_trampoline_start,__disc_vmu_trampoline_end
__disc_vmu_trampoline_start:
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
    mov.l .Ldisc_resume_relay,r0
    jmp @r0
    nop
    .align 2
.Ldisc_resume_relay: .long C(kui_disc_vmu_game_resume)+0x20000000
/* The native physical-disc stage copies this exact128-byte template to the
 * executable entry and keeps its original bytes until the relay restores it.
 * No GD service or disc-read implementation is installed by this template. */
    .global __disc_vmu_trampoline_code_end
__disc_vmu_trampoline_code_end:
    .space 128-(.-__disc_vmu_trampoline_start)
__disc_vmu_trampoline_end:

    .text
    .align 2
    .global C(kui_disc_vmu_game_resume)
C(kui_disc_vmu_game_resume):
    mov r15,r8
    mov.l .Ldisc_resume_highstack,r15
    mov.l .Ldisc_resume_ccr,r0
    mov.l @r0,r9
/* Execute through P2. Purge dirty RAM lines before disabling cache, including
 * the frame and bootstrap modifications. No stale cached alias may overwrite
 * the newly installed resident or restored executable later. */
    mov.l .Ldisc_resume_ram,r0
    mov.l .Ldisc_resume_ramend,r1
.Ldisc_resume_purge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Ldisc_resume_purge
    mov.l .Ldisc_resume_ccr,r0
    /* RAM is published above. The high-stage relay is called through P1:
     * cache its instructions while keeping every mutable RAM access uncached.
     * The return below restores the owner's exact enable/mode bits. */
    mov.l .Ldisc_resume_scan_ccr,r1
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
    mov.l .Ldisc_resume_relaymain,r0
    jsr @r0
    nop
/* C returns only after exact executable restoration and resident installation.
 * Invalidate before restoring the owner's cache enable/mode bits through P2. */
    mov.l .Ldisc_resume_off,r0
    or r9,r0
    mov.l .Ldisc_resume_ccr,r1
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
    mov.l .Ldisc_resume_game,r0
    jmp @r0
    mov.l @r15+,r0
    .align 2
.Ldisc_resume_highstack: .long 0x8cff0000
.Ldisc_resume_ccr: .long 0xff00001c
.Ldisc_resume_ram: .long 0x8c000000
.Ldisc_resume_ramend: .long 0x8d000000
.Ldisc_resume_off: .long 0x00000808
.Ldisc_resume_scan_ccr: .long 0x00000908
.Ldisc_resume_relaymain: .long C(kui_disc_vmu_stage_relay)
/* Owner bootstrap2 calls the uncached executable alias. Resume that same
 * alias while the original entry performs its own cache setup. */
.Ldisc_resume_game: .long 0xac010000

/* Same immutable-proof scopes as image launch. Live SDK/card operations stay
 * outside these scopes, and unknown entry cache modes retain the old path. */
#include "native_proof_cache_scope.inc"
