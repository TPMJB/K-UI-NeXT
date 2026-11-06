/* SPDX-License-Identifier: GPL-3.0-only */
/* Terminal UBC entry. Hardware owns SGR/SPC/SSR and selects register bank1.
 * Never borrow interrupted R15: it may be unmapped or point into the range
 * being erased. The manifest becomes a terminal snapshot; no reader or
 * owner instruction may resume after this entry. Snapshot ABI is declared
 * in kui/vc2_ce_vmu_watch.h: bank1[0..7], R8..R14[8..14], SGR[15],
 * bank0[16..23], SPC/SSR/PR/GBR/MACH/MACL/EXPEVT/BRCR/CCR/MMUCR/DBR[24..34],
 * and the stage-saved original DBR[35]. */
#define JI(a,b) a##b
#define J(a,b) JI(a,b)
#define C(n) J(__USER_LABEL_PREFIX__,n)
    .section .text.vc2_vmu_watch_entry,"ax"
    .align 2
    .global C(kui_vc2_ce_vmu_watch_entry)
    .type C(kui_vc2_ce_vmu_watch_entry),@function
C(kui_vc2_ce_vmu_watch_entry):
    /* A branch is not a valid first instruction at a DBR exception entry. */
    mov r0,r15
    mov.l .Lsnapshot,r0
    mov.l r15,@r0
    mov.l r1,@(4,r0)
    /* Disable both channels before any further snapshot or display work.
     * BBRA/BBRB changes retain BRCR's latched condition-match flags. */
    mov #0,r15
    mov.l .Lbbra,r1
    mov.w r15,@r1
    add #12,r1
    mov.w r15,@r1
    .rept 11
    nop
    .endr
    mov.l r2,@(8,r0)
    mov.l r3,@(12,r0)
    mov.l r4,@(16,r0)
    mov.l r5,@(20,r0)
    mov.l r6,@(24,r0)
    mov.l r7,@(28,r0)
    mov.l r8,@(32,r0)
    mov.l r9,@(36,r0)
    mov.l r10,@(40,r0)
    mov.l r11,@(44,r0)
    mov.l r12,@(48,r0)
    mov.l r13,@(52,r0)
    mov.l r14,@(56,r0)
    stc sgr,r1
    mov.l r1,@(60,r0)
    add #64,r0
    /* The stage saved original DBR in the now-retired manifest CRC word.
     * Preserve it before bank0 R0 overwrites that same snapshot offset. */
    mov.l @r0,r1
    mov r0,r2
    add #76,r2
    mov.l r1,@r2
    stc r0_bank,r1
    mov.l r1,@r0
    stc r1_bank,r1
    mov.l r1,@(4,r0)
    stc r2_bank,r1
    mov.l r1,@(8,r0)
    stc r3_bank,r1
    mov.l r1,@(12,r0)
    stc r4_bank,r1
    mov.l r1,@(16,r0)
    stc r5_bank,r1
    mov.l r1,@(20,r0)
    stc r6_bank,r1
    mov.l r1,@(24,r0)
    stc r7_bank,r1
    mov.l r1,@(28,r0)
    add #32,r0
    stc spc,r1
    mov.l r1,@r0
    stc ssr,r1
    mov.l r1,@(4,r0)
    sts pr,r1
    mov.l r1,@(8,r0)
    stc gbr,r1
    mov.l r1,@(12,r0)
    sts mach,r1
    mov.l r1,@(16,r0)
    sts macl,r1
    mov.l r1,@(20,r0)
    mov.l .Lmmucr,r2
    mov.l @(20,r2),r1
    mov.l r1,@(24,r0)
    mov.l .Lbbra,r1
    add #24,r1
    mov.w @r1,r1
    extu.w r1,r1
    mov.l r1,@(28,r0)
    mov.l @(12,r2),r1
    mov.l r1,@(32,r0)
    mov.l @r2,r1
    mov.l r1,@(36,r0)
    stc dbr,r1
    mov.l r1,@(40,r0)
    /* Canonical P1 stores share any already-dirty native manifest lines.
     * Publish every line before the terminal renderer reads its P2 alias;
     * an older native cache line must not later resurrect the original map. */
    mov.l .Lsnapshot,r0
    mov #-32,r1
    and r1,r0
    mov.l .Lsnapshot_end,r1
.Lpublish:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lpublish
    mov.l .Lstack,r15
    mov.l .Lstop,r0
    jmp @r0
    nop
    .align 2
.Lsnapshot: .long C(manifest)
.Lsnapshot_end: .long C(manifest)+828
.Lbbra: .long 0xff200008
.Lmmucr: .long 0xff000010
.Lstack: .long __retail_hook_stack+0x20000000
.Lstop: .long C(kui_vc2_ce_vmu_watch_stop)+0x20000000
    .size C(kui_vc2_ce_vmu_watch_entry),.-C(kui_vc2_ce_vmu_watch_entry)
