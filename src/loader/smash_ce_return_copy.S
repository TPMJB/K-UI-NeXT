/* SPDX-License-Identifier: GPL-3.0-only */
/* Called only after complete prepared-transport CRC validation, from the
 * source-owned pre-NK ABORT after original BOOT1 has retired. The body is
 * position independent until it jumps into its verified low copy. Only then
 * may the original helper padding containing this source be erased. */
    .section .ce_return_copy,"ax"
    .align 2
    .global __smash_ce_return_copy
__smash_ce_return_copy:
    mov r4,r7
    mov.l .Lhook_from,r0
    cmp/hs r0,r7
    bf .Lfailed
    mov.l .Lhook_to,r0
    cmp/hs r0,r7
    bt .Lfailed
    mov.l .Lmenu_p1,r0
    ocbp @r0
    mov.l .Lmenu_p2,r0
    mov.l @r0,r1
    cmp/eq r7,r1
    bf .Lfailed
    /* Retired BOOT1 can still have dirty P1 aliases. Publish/invalidate its
     * entire incoming code/BSS/stack lease before the first P2 replacement. */
    mov.l .Ltarget_p1,r5
    mov.l .Lstack_end_p1,r6
.Lpurge:
    ocbp @r5
    add #32,r5
    cmp/hs r6,r5
    bf .Lpurge
    mov.l .Lsource,r0
    mov.l .Ltarget,r1
    mov.l .Lend,r2
.Lcopy:
    mov.l @r0+,r3
    mov.l r3,@r1
    mov.l @r1+,r4
    cmp/eq r3,r4
    bf .Lfailed
    cmp/hs r2,r1
    bf .Lcopy
    mov.l .Llow_continue,r0
    jmp @r0
    nop
.Llow:
    mov.l .Lbss_from,r0
    mov #0,r2
    /* The linker pins this complete BSS to exactly four words. Unroll its
     * exact zeroing rather than spending transport bytes on a general loop. */
    mov.l r2,@r0
    mov.l r2,@(4,r0)
    mov.l r2,@(8,r0)
    mov.l r2,@(12,r0)
.Lpadding:
    mov.l .Lpadding_from,r0
    mov.l .Lpadding_to,r1
.Lclear_padding:
    mov.l r2,@r0
    add #4,r0
    cmp/hs r1,r0
    bf .Lclear_padding
    /* Publish only after the complete code/runtime/BSS is ready. The
     * caller passes its exact linked former E0 hook, checked above. */
    mov.l .Lprevious_e0,r0
    mov.l r7,@r0
    mov.l .Lmenu_p2,r0
    mov.l .Ltarget,r1
    mov.l r1,@r0
    mov.l @r0,r2
    cmp/eq r1,r2
    bf .Lfailed
    mov #0,r0
    rts
    nop
.Lfailed:
    mov #-1,r0
    rts
    nop
    .align 2
.Lsource: .long 0xacfce0e0
.Ltarget: .long __smash_ce_return_begin
.Lend: .long __smash_ce_return_binary_end
.Llow_continue: .long .Llow
.Lbss_from: .long __smash_ce_return_bss_begin
.Lpadding_from: .long 0xacfce0c0
.Lpadding_to: .long 0xacfd0000
.Ltarget_p1: .long 0x8c008300
.Lstack_end_p1: .long 0x8c00c000
.Lhook_from: .long 0x8c004000
.Lhook_to: .long 0x8c007ea0
.Lmenu_p1: .long 0x8c0000e0
.Lmenu_p2: .long 0xac0000e0
.Lprevious_e0: .long _kui_smash_ce_return_previous_e0
