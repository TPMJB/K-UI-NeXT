/* SPDX-License-Identifier: GPL-3.0-only */
/* Both original GIN children have the same captured integer continuation.
 * Compile the proven resumer under a separate native symbol; CE's object,
 * helper image, resource bounds and build policy remain unchanged. */
#define kui_smash_ce_gin_resume kui_smash_native_gin_resume
#include "smash_ce_gin_resume.S"

    .text
    .align 2
    .global C(kui_smash_native_gin_arm_scope)
    .type C(kui_smash_native_gin_arm_scope),@function
C(kui_smash_native_gin_arm_scope):
/* Match the accepted native relay's cache boundary exactly. Card attachment
 * and resident initialization ran under the ordinary cached high stage;
 * native discovery/hook publication requires clean RAM and CCR=0x100. The
 * original GIN frame and active register bank remain untouched. */
    mov.l r8,@-r15
    mov.l r9,@-r15
    sts.l pr,@-r15
    mov r4,r8
    mov r5,r9
    mov.l .Lnative_arm_p2,r0
    jmp @r0
    nop
.Lnative_arm_uncached:
    mov.l .Lnative_arm_ram,r0
    mov.l .Lnative_arm_end,r1
.Lnative_arm_purge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lnative_arm_purge
    mov.l .Lnative_arm_ccr,r0
    mov.l .Lnative_arm_cache,r1
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
    mov.l .Lnative_arm_main,r0
    jsr @r0
    nop
    lds.l @r15+,pr
    mov.l @r15+,r9
    mov.l @r15+,r8
    rts
    nop
    .align 2
.Lnative_arm_p2: .long .Lnative_arm_uncached+0x20000000
.Lnative_arm_ram: .long 0x8c000000
.Lnative_arm_end: .long 0x8d000000
.Lnative_arm_ccr: .long 0xff00001c
.Lnative_arm_cache: .long 0x908
.Lnative_arm_main: .long C(kui_retail_stage_relay)
    .size C(kui_smash_native_gin_arm_scope),.-C(kui_smash_native_gin_arm_scope)
