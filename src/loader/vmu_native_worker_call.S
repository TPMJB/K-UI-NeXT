/* SPDX-License-Identifier: GPL-3.0-only */
/* Stage-only call gateway; never copied into the worker or a game hook.
 * int call(entry,stack_top_p2,config,result) invokes int entry(config,result).
 * The caller proves memory ownership and the exact relocated exports first.
 * All compiled worker/stage C uses the fixed no-FPU register policy. Neither
 * gateway nor worker may modify floating-point state. */
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
    .section .text.vmu_native_worker_call,"ax"
    .align 2
    .global C(kui_vmu_native_worker_call)
    .type C(kui_vmu_native_worker_call),@function
C(kui_vmu_native_worker_call):
    /* Save the return-value slot before SR so even an SR register-bank
     * restore cannot lose the eventual worker result in caller-saved R0. */
    mov.l r0,@-r15
    stc.l sr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
    sts.l pr,@-r15
    sts.l mach,@-r15
    sts.l macl,@-r15
    stc.l gbr,@-r15

    stc sr,r0
    and #0xf0,r0
    shlr2 r0
    shlr2 r0
    cmp/eq #15,r0
    bf .Linvalid
    mov r4,r0
    tst #1,r0
    bf .Linvalid
    mov.l .Larea_mask,r1
    and r1,r0
    mov.l .Lram_p2,r1
    cmp/eq r1,r0
    bf .Linvalid
    mov r5,r0
    tst #31,r0
    bf .Linvalid
    mov.l .Lstack_min,r1
    cmp/hs r1,r5
    bf .Linvalid
    mov.l .Lram_end,r1
    cmp/hi r1,r5
    bt .Linvalid
    mov r6,r0
    tst r0,r0
    bt .Linvalid
    mov r7,r0
    tst r0,r0
    bt .Linvalid

    /* R8 is unbanked and ABI callee-saved. Keep the old stage frame there;
     * no pointer to that frame is written to private worker memory. */
    mov r15,r8
    mov r4,r9
    mov r5,r15
    mov r6,r4
    mov r7,r5
    jsr @r9
    nop
    mov r8,r15
    bra .Lrestore
    nop
.Linvalid:
    mov #-1,r0
.Lrestore:
    ldc.l @r15+,gbr
    lds.l @r15+,macl
    lds.l @r15+,mach
    lds.l @r15+,pr
    mov.l @r15+,r14
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    mov.l r0,@(4,r15)
    ldc.l @r15+,sr
    nop
    nop
    nop
    mov.l @r15+,r0
    rts
    nop
    .align 2
.Larea_mask: .long 0xff000000
.Lram_p2:
#if KUI_VMU_NATIVE_CACHED_CODE
    .long 0x8c000000
#else
    .long 0xac000000
#endif
.Lstack_min: .long 0xac001000
.Lram_end: .long 0xad000000
    .size C(kui_vmu_native_worker_call),.-C(kui_vmu_native_worker_call)
