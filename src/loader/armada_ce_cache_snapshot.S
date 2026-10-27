/* SPDX-License-Identifier: GPL-3.0-only */
/* Complete coherent metadata snapshot, always called through its P2 alias.
 * R4 is the one C-owned P2 workspace object, exactly 60 bytes:
 *   +0 VA line, +4 P1 line, +8 software PTE, +12 eight-bit word mask;
 *   +16 words[8]; +48 detail[3] (address, actual, expected).
 * Return 0=OK, 1=CACHE, 2=CHANGED, 3=ARGUMENT. Output is private staging;
 * selected words may be partial on refusal and must be copied only on OK.
 *
 * Caller already owns the mapping/lifetime and supplies the P2 alias with
 * kernel mode and a private P2 SP. This leaf additionally checks IMASK15,
 * normal live CCR mode, input geometry and its exact shared job identity.
 * No P1 load, cache maintenance, callback or blocking operation occurs.
 * R8..R13 and SP are preserved; maximum stack is 40 bytes. PR, GBR, MACH,
 * MACL and SR mode/mask are unchanged; the T condition flag is unspecified.
 * Every array-reading exit has eight terminal
 * NOPs before any possible return to cached code.
 */
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
#include "kui/armada_ce_vmu_layout.h"
    .section .text.kui_armada_ce_cache_snapshot,"ax",@progbits
    .align 2
    .global C(kui_armada_ce_cache_snapshot)
    .type C(kui_armada_ce_cache_snapshot),@function
C(kui_armada_ce_cache_snapshot):
    mov.l .Ljob,r0
    cmp/eq r0,r4
    bt .Ljob_ok
    mov #3,r0
    rts
    nop
.Ljob_ok:
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    add #-16,r15
    mov r4,r8
    mov.l @(0,r8),r4
    mov.l @(4,r8),r5
    mov.l @(8,r8),r6
    mov.l @(12,r8),r13
    mov r4,r0
    or r5,r0
    tst #31,r0
    bf .Largument
    cmp/pz r4
    bf .Largument
    mov.l .Lp1_low,r0
    cmp/hs r0,r5
    bf .Largument
    mov.l .Lp1_high,r0
    cmp/hi r0,r5
    bt .Largument
    mov.l .Lprivate_low,r0
    cmp/hs r0,r5
    bf .Lnot_private
    mov.l .Lprivate_high,r0
    cmp/hs r0,r5
    bf .Largument
.Lnot_private:
    mov r4,r0
    xor r5,r0
    mov.l .Loffset_mask,r1
    tst r1,r0
    bf .Largument
    mov r6,r0
    tst #1,r0
    bt .Largument
    mov.l .Lpage_mask,r0
    mov r6,r1
    and r0,r1
    mov.l .Lphysical_page_mask,r0
    mov r5,r2
    and r0,r2
    cmp/eq r1,r2
    bf .Largument
    mov r13,r0
    tst r0,r0
    bt .Largument
    shlr8 r0
    tst r0,r0
    bf .Largument
    stc sr,r0
    mov.l .Limask,r1
    and r1,r0
    cmp/eq r1,r0
    bf .Largument
    mov.l .Lccr,r1
    mov.l @r1,r0
    and #0xa1,r0
    cmp/eq #1,r0
    bf .Lcache_pre
    bra .Lcapture
    nop
.Largument:
    mov.l @(0,r8),r0
    mov.l r0,@(48,r8)
    mov.l @(4,r8),r0
    mov.l r0,@(52,r8)
    mov.l @(8,r8),r0
    mov.l r0,@(56,r8)
    mov #3,r0
    bra .Lreturn
    nop
.Lcache_pre:
    mov.l .Lccr,r1
    mov.l r1,@(48,r8)
    mov.l @r1,r0
    mov.l r0,@(52,r8)
    mov #1,r0
    mov.l r0,@(56,r8)
    bra .Lreturn
    nop
.Lcapture:
    mov r5,r9
    mov.l .Lindex_mask,r0
    and r0,r9
    mov.l .Ltag_array,r0
    or r0,r9
    mov r5,r10
    mov.l .Lphysical_tag_mask,r0
    and r0,r10
    mov r4,r11
    mov.l .Lcolor_mask,r0
    and r0,r11
    or r9,r11
    mov r5,r12
    mov.l .Lp2_delta,r0
    or r0,r12
    mov r6,r5
    mov #8,r0
    and r0,r5
    mov.l .Ltag_mask,r3
    mov #-4,r2
    mov r15,r4
    mov r9,r6
    mov #4,r7
.Ltags_before:
    mov.l @r6,r0
    and r3,r0
    mov.l r0,@r4
    add #4,r4
    tst #1,r0
    bt .Lnext_tag
    mov r0,r1
    and r2,r1
    cmp/eq r10,r1
    bf .Lnext_tag
    tst #2,r0
    bt .Lmatching_tag
    tst r5,r5
    bt .Ldirty_alias
    cmp/eq r11,r6
    bf .Ldirty_alias
.Lmatching_tag:
    tst r5,r5
    bt .Lnext_tag
    cmp/eq r11,r6
    bf .Lnext_tag
    mov r6,r12
    mov.l .Ldata_delta,r1
    add r1,r12
.Lnext_tag:
    mov.l .Lcolor_step,r1
    add r1,r6
    dt r7
    bf .Ltags_before
    mov r8,r4
    add #16,r4
    mov r12,r5
    mov r13,r6
    mov #8,r7
.Lcopy_words:
    shlr r6
    bf .Lcopy_skip
    mov.l @r5,r0
    mov.l r0,@r4
.Lcopy_skip:
    add #4,r4
    add #4,r5
    dt r7
    bf .Lcopy_words
    mov r15,r4
    mov r9,r5
    mov #4,r7
.Ltags_after:
    mov.l @r5,r0
    and r3,r0
    mov.l @r4+,r1
    cmp/eq r1,r0
    bf .Lchanged
    mov.l .Lcolor_step,r2
    add r2,r5
    dt r7
    bf .Ltags_after
    mov r8,r4
    add #16,r4
    mov r12,r5
    mov r13,r6
    mov #8,r7
.Lcompare_words:
    shlr r6
    bf .Lcompare_skip
    mov.l @r5,r0
    mov.l @r4,r1
    cmp/eq r1,r0
    bf .Lchanged
.Lcompare_skip:
    add #4,r4
    add #4,r5
    dt r7
    bf .Lcompare_words
    mov #0,r0
    bra .Lreturn
    nop
.Ldirty_alias:
    mov.l r6,@(48,r8)
    mov.l r0,@(52,r8)
    mov.l @(8,r8),r0
    mov.l r0,@(56,r8)
    mov #1,r0
    bra .Lreturn
    nop
.Lchanged:
    mov.l r5,@(48,r8)
    mov.l r0,@(52,r8)
    mov.l r1,@(56,r8)
    mov #2,r0
.Lreturn:
    .rept 8
    nop
    .endr
    add #16,r15
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    rts
    nop
    .align 2
.Ljob: .long C(kui_armada_ce_cache_snapshot_job)
.Lp1_low: .long 0x8c010000
.Lp1_high: .long 0x8cffffe0
.Lprivate_low: .long KUI_ARMADA_CE_VMU_RESERVE_BEGIN
.Lprivate_high: .long 0x8cfd0000
.Loffset_mask: .long 0x00000fff
.Lpage_mask: .long 0xfffff000
.Lphysical_page_mask: .long 0x1ffff000
.Limask: .long 0x000000f0
.Lccr: .long 0xff00001c
.Lindex_mask: .long 0x00000fe0
.Ltag_array: .long 0xf4000000
.Lphysical_tag_mask: .long 0x1ffffc00
.Lcolor_mask: .long 0x00003000
.Lp2_delta: .long 0x20000000
.Ltag_mask: .long 0x1ffffc03
.Ldata_delta: .long 0x01000000
.Lcolor_step: .long 0x00001000
    .size C(kui_armada_ce_cache_snapshot),.-C(kui_armada_ce_cache_snapshot)
