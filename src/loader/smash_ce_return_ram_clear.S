/* SPDX-License-Identifier: GPL-3.0-only */
    .text
    .align 2
    .global __kui_smash_ce_return_ram_clear
    .set __kui_smash_ce_return_ram_clear,1
    .global _kui_smash_ce_return_ram_clear
    .type _kui_smash_ce_return_ram_clear,@function
_kui_smash_ce_return_ram_clear:
    /* Terminal CE retirement only: producers, caches and translation are
     * already off, while this code, its caller and stack remain below0xac010000
     * in P2. Match original MAIGO's complete main-RAM clear before loading
     * the fresh outer capsule. No old high helper or CE frame may resume. */
    mov.l .Lbegin,r0
    mov.l .Lend,r1
    mov #0,r2
.Lzero:
    mov.l r2,@r0
    mov.l r2,@(4,r0)
    mov.l r2,@(8,r0)
    mov.l r2,@(12,r0)
    mov.l r2,@(16,r0)
    mov.l r2,@(20,r0)
    mov.l r2,@(24,r0)
    mov.l r2,@(28,r0)
    add #32,r0
    cmp/hs r1,r0
    bf .Lzero
    rts
    nop
    .align 2
.Lbegin: .long 0xac010000
.Lend: .long 0xad000000
    .size _kui_smash_ce_return_ram_clear,.-_kui_smash_ce_return_ram_clear
