/* SPDX-License-Identifier: GPL-3.0-only */
/* Entered by the checked privileged reset or Swirl private-return wrapper.
 * Keep its virtual stack and CE VBR/MMU alive until pending work completes.
 * No return from the successful terminal transfer can resume old CE code. */
    .section .entry,"ax"
    .align 2
    .global _kui_smash_ce_return_entry
_kui_smash_ce_return_entry:
    /* Firmware-call volatile R0/R1 are scratch. Keep unrelated forwarded
     * commands stackless, and restore the exact incoming SR/T after testing
     * the command. The terminal frame below retains every other register. */
    stc sr,r1
    mov #1,r0
    cmp/eq r4,r0
    bt .Lcapture
#if KUI_SMASH_SWIRL_VMU
    /* Swirl's source-owned MAIGO callback takes0 directly at8300. Its
     * fallback thunk sets3 and tail-jumps E0, retaining the same EXE PR.
     * Filter that exact PR before any stack touch; R0/R1 remain the only
     * firmware-call scratch registers. Match the C guard's alias mask. */
    mov #0,r0
    cmp/eq r4,r0
    bt .Lswirl_private
    mov #3,r0
    cmp/eq r4,r0
    bf .Lforward
.Lswirl_private:
    sts pr,r0
    xor #0xa6,r0
    tst #0xff,r0
    bf .Lforward
    shlr8 r0
    xor #0x19,r0
    tst #0xff,r0
    bf .Lforward
    shlr8 r0
    xor #0x04,r0
    tst #0xff,r0
    bf .Lforward
    shlr8 r0
    tst #0x81,r0
    bt .Lcapture
#endif
.Lforward:
    ldc r1,sr
    /* Unrelated firmware calls retain the exact previous low hook. */
    mov.l .Lprevious_e0,r0
    mov.l @r0,r0
    jmp @r0
    nop
.Lcapture:
    ldc r1,sr
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
    mov r15,r8
.Lvisit:
    mov.l @(16,r8),r0
    mov.l .Lmask,r1
    or r1,r0
    ldc r0,sr
    mov.l .Lstack,r15
    add #-16,r15
    mov r8,r4
    mov.l .Lmain,r0
    jsr @r0
    nop
    /* C returns only BUSY. Restore the real CE owner stack/global/vector
     * bases before making scheduling possible. The frame stays above SP. */
    mov r8,r15
    mov.l @(8,r8),r0
    ldc r0,gbr
    mov.l @(12,r8),r0
    ldc r0,vbr
    mov.l @(16,r8),r0
    ldc r0,sr
    .global __smash_ce_return_wait_owner
__smash_ce_return_wait_owner:
    mov.l .Lspins,r0
.Lwait:
    nop
    dt r0
    bf .Lwait
    bra .Lvisit
    nop
    .align 2
.Lmask: .long 0x000000f0
.Lstack: .long __smash_ce_return_stack_top
.Lmain: .long _kui_smash_ce_return_main
.Lspins: .long 1024
.Lprevious_e0: .long _kui_smash_ce_return_previous_e0
