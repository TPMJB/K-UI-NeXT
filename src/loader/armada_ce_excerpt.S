/* SPDX-License-Identifier: GPL-3.0-only */
#define JOIN_INNER(a,b) a##b
#define JOIN(a,b) JOIN_INNER(a,b)
#define C_SYMBOL(name) JOIN(__USER_LABEL_PREFIX__,name)
#if !defined(KUI_ARMADA_CE_EXCERPT) || KUI_ARMADA_CE_EXCERPT != 1
#error "The excerpt bridge requires its isolated feature flag"
#endif
#if !defined(KUI_RETAIL_CE)
#error "The excerpt bridge is Windows CE only"
#endif
    .section .text
    .align 2
    .global C_SYMBOL(kui_armada_ce_excerpt_ce_call)
    .type C_SYMBOL(kui_armada_ce_excerpt_ce_call),@function
C_SYMBOL(kui_armada_ce_excerpt_ce_call):
    mov.l r8,@-r15
    mov.l r9,@-r15
    sts.l pr,@-r15
    stc.l gbr,@-r15
    mov r4,r8
    mov r5,r9
    add #-32,r15
    mov.l @(16,r9),r0
    mov.l r0,@(16,r15)
    mov.l @(20,r9),r0
    mov.l r0,@(20,r15)
    mov.l @(24,r9),r0
    mov.l r0,@(24,r15)
    mov.l @(28,r9),r0
    mov.l r0,@(28,r15)
    mov.l @r9,r4
    mov.l @(4,r9),r5
    mov.l @(8,r9),r6
    mov.l @(12,r9),r7
    jsr @r8
    nop
    add #32,r15
    ldc.l @r15+,gbr
    lds.l @r15+,pr
    mov.l @r15+,r9
    rts
    mov.l @r15+,r8
    .size C_SYMBOL(kui_armada_ce_excerpt_ce_call),.-C_SYMBOL(kui_armada_ce_excerpt_ce_call)

    .align 2
    .global C_SYMBOL(kui_armada_ce_excerpt_deferred)
    .type C_SYMBOL(kui_armada_ce_excerpt_deferred),@function
C_SYMBOL(kui_armada_ce_excerpt_deferred):
    /* No FPU transfer in the driver process. Native PSL callback enters the
     * verified sound owner before the inner bridge touches FPSCR. */
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
    stc.l gbr,@-r15
    sts.l mach,@-r15
    sts.l macl,@-r15
    stc.l sr,@-r15
    mov.l @r15,r4
    mov r15,r5
    add #80,r5
    add #-16,r15
    mov.l .Ldispatch,r0
    jsr @r0
    nop
    add #16,r15
    ldc.l @r15+,sr
    lds.l @r15+,macl
    lds.l @r15+,mach
    ldc.l @r15+,gbr
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
    rts
    mov.l @r15+,r0
    .align 2
.Ldispatch: .long C_SYMBOL(kui_armada_ce_excerpt_dispatch)
    .size C_SYMBOL(kui_armada_ce_excerpt_deferred),.-C_SYMBOL(kui_armada_ce_excerpt_deferred)

    .align 2
    .global C_SYMBOL(kui_armada_ce_excerpt_owner_deferred)
    .type C_SYMBOL(kui_armada_ce_excerpt_owner_deferred),@function
C_SYMBOL(kui_armada_ce_excerpt_owner_deferred):
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
    stc.l gbr,@-r15
    sts.l mach,@-r15
    sts.l macl,@-r15
    stc.l sr,@-r15
    mov.l @r15,r4
    mov.l .Lsr_mask,r1
    and r4,r1
    mov.l .Lnormal_sr,r2
    cmp/eq r2,r1
    bf .Lno_fpu
    /* FD1 deliberately traps at this non-delay-slot instruction. The exact
     * CE handler saves the previous FPU owner and loads this thread's state.
     * Clearing FD ourselves would expose another thread's register banks. */
    sts fpscr,r5
    mov.l .Lnon_scalar,r1
    tst r1,r5
    bf .Lrefused_context
    sts.l fpscr,@-r15
    sts.l fpul,@-r15
    /* Scalar transfer sizes and bank0, while keeping caller control/flags. */
    mov.l .Lbank_bit,r1
    not r1,r1
    and r5,r1
    lds r1,fpscr
    fmov.s fr0,@-r15
    fmov.s fr1,@-r15
    fmov.s fr2,@-r15
    fmov.s fr3,@-r15
    fmov.s fr4,@-r15
    fmov.s fr5,@-r15
    fmov.s fr6,@-r15
    fmov.s fr7,@-r15
    fmov.s fr8,@-r15
    fmov.s fr9,@-r15
    fmov.s fr10,@-r15
    fmov.s fr11,@-r15
    fmov.s fr12,@-r15
    fmov.s fr13,@-r15
    fmov.s fr14,@-r15
    fmov.s fr15,@-r15
    frchg
    fmov.s fr0,@-r15
    fmov.s fr1,@-r15
    fmov.s fr2,@-r15
    fmov.s fr3,@-r15
    fmov.s fr4,@-r15
    fmov.s fr5,@-r15
    fmov.s fr6,@-r15
    fmov.s fr7,@-r15
    fmov.s fr8,@-r15
    fmov.s fr9,@-r15
    fmov.s fr10,@-r15
    fmov.s fr11,@-r15
    fmov.s fr12,@-r15
    fmov.s fr13,@-r15
    fmov.s fr14,@-r15
    fmov.s fr15,@-r15
    frchg
    add #-16,r15
    mov.l .Lattempt,r0
    jsr @r0
    nop
    add #16,r15
    /* Native methods may alter FPSCR; select scalar bank1 for restoration. */
    mov #33,r1
    shll2 r1
    add r15,r1
    mov.l @r1,r1
    mov.l .Lbank_bit,r2
    or r2,r1
    lds r1,fpscr
    fmov.s @r15+,fr15
    fmov.s @r15+,fr14
    fmov.s @r15+,fr13
    fmov.s @r15+,fr12
    fmov.s @r15+,fr11
    fmov.s @r15+,fr10
    fmov.s @r15+,fr9
    fmov.s @r15+,fr8
    fmov.s @r15+,fr7
    fmov.s @r15+,fr6
    fmov.s @r15+,fr5
    fmov.s @r15+,fr4
    fmov.s @r15+,fr3
    fmov.s @r15+,fr2
    fmov.s @r15+,fr1
    fmov.s @r15+,fr0
    frchg
    fmov.s @r15+,fr15
    fmov.s @r15+,fr14
    fmov.s @r15+,fr13
    fmov.s @r15+,fr12
    fmov.s @r15+,fr11
    fmov.s @r15+,fr10
    fmov.s @r15+,fr9
    fmov.s @r15+,fr8
    fmov.s @r15+,fr7
    fmov.s @r15+,fr6
    fmov.s @r15+,fr5
    fmov.s @r15+,fr4
    fmov.s @r15+,fr3
    fmov.s @r15+,fr2
    fmov.s @r15+,fr1
    fmov.s @r15+,fr0
    lds.l @r15+,fpul
    lds.l @r15+,fpscr
    bra .Lrestore_integer
    nop
.Lno_fpu:
    mov #-1,r5
.Lrefused_context:
    add #-16,r15
    mov.l .Lattempt,r0
    jsr @r0
    nop
    add #16,r15
.Lrestore_integer:
    ldc.l @r15+,sr
    lds.l @r15+,macl
    lds.l @r15+,mach
    ldc.l @r15+,gbr
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
    rts
    mov.l @r15+,r0
    .align 2
.Lsr_mask: .long 0x700000f0
.Lnormal_sr: .long 0x40000000
.Lnon_scalar: .long 0x00180000
.Lbank_bit: .long 0x00200000
.Lattempt: .long C_SYMBOL(kui_armada_ce_excerpt_attempt)
    .size C_SYMBOL(kui_armada_ce_excerpt_owner_deferred),.-C_SYMBOL(kui_armada_ce_excerpt_owner_deferred)

    /* Read-only SH7750 operand-array access. Called through this function's
     * P2 alias only. Preserve SR, mask IRQs before the MOV, and leave eight
     * explicit instructions before restoring SR and returning to P1 code. */
    .section .text.kui_armada_ce_excerpt_array_read,"ax",@progbits
    .align 2
    .global C_SYMBOL(kui_armada_ce_excerpt_array_read)
    .type C_SYMBOL(kui_armada_ce_excerpt_array_read),@function
C_SYMBOL(kui_armada_ce_excerpt_array_read):
    stc sr,r1
    mov r1,r0
    or #0xf0,r0
    ldc r0,sr
    mov.l @r4,r0
    .rept 8
    nop
    .endr
    ldc r1,sr
    rts
    nop
    .size C_SYMBOL(kui_armada_ce_excerpt_array_read),.-C_SYMBOL(kui_armada_ce_excerpt_array_read)
