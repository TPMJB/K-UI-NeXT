/* SPDX-License-Identifier: GPL-3.0-only */
#if KUI_NATIVE_COLD_SDK_CACHE
/* The standalone's two fresh SDK-family checks retain the complete image and
 * exact original result. Only the pure finder, its16-byte output and private
 * stack enter this scope; live slots, VBR and publication remain outside.
 * This is the existing discovery guard/publication contract, with a complete
 * sixteen-byte output bound and nonoverlap check. The emitted closed finder
 * graph must fit the64KiB stack headroom. Unknown profiles call it unchanged. */
    .text
    .align 2
    .global __kui_native_cached_sdk_family
    .set __kui_native_cached_sdk_family,1
    .global C(kui_native_stage_cached_sdk_family)
    .type C(kui_native_stage_cached_sdk_family),@function
C(kui_native_stage_cached_sdk_family):
    mov.l r8,@-r15
    mov.l r9,@-r15
    sts.l pr,@-r15
    mov #0,r8
    stc sr,r0
    mov.l .Lsdk_sr_mask,r1
    and r1,r0
    cmp/eq r1,r0
    bf .Lsdk_call
    mov.l .Lsdk_mmucr,r1
    mov.l @r1,r0
    tst #1,r0
    bf .Lsdk_call
    mov.l .Lsdk_ccr,r1
    mov.l @r1,r0
    mov.l .Lsdk_expected,r2
    cmp/eq r2,r0
    bf .Lsdk_call
    mov.l .Lsdk_load,r0
    cmp/eq r0,r6
    bf .Lsdk_call
    cmp/eq r4,r6
    bf .Lsdk_call
    mov.l .Lsdk_ram,r0
    cmp/hs r0,r4
    bf .Lsdk_call
    mov.l .Lsdk_ramend,r1
    cmp/hs r1,r4
    bt .Lsdk_call
    mov r1,r2
    sub r4,r2
    cmp/hs r5,r2
    bf .Lsdk_call
    tst r5,r5
    bt .Lsdk_call
    cmp/hs r0,r7
    bf .Lsdk_call
    cmp/hs r1,r7
    bt .Lsdk_call
    mov r1,r2
    sub r7,r2
    mov #16,r0
    cmp/hs r0,r2
    bf .Lsdk_call
    mov r7,r0
    tst #3,r0
    bf .Lsdk_call
    cmp/hs r4,r7
    bf .Lsdk_output_before
    mov r4,r0
    add r5,r0
    cmp/hs r0,r7
    bf .Lsdk_call
    bra .Lsdk_output_checked
    nop
.Lsdk_output_before:
    mov r7,r0
    add #16,r0
    cmp/hi r4,r0
    bt .Lsdk_call
.Lsdk_output_checked:
    mov.l .Lsdk_stack_low,r0
    cmp/hs r0,r15
    bf .Lsdk_call
    mov r15,r0
    tst #3,r0
    bf .Lsdk_call
    add #12,r0
    cmp/hi r1,r0
    bt .Lsdk_call
    mov #1,r8
    mov.l .Lsdk_p2_address,r0
    jmp @r0
    nop
.Lsdk_p2:
    mov.l .Lsdk_ccr,r1
    mov.l .Lsdk_on,r0
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Lsdk_call:
    mov.l .Lsdk_proof,r0
    jsr @r0
    nop
    mov r0,r9
    tst r8,r8
    bt .Lsdk_done
    mov.l .Lsdk_ram,r0
    mov.l .Lsdk_ramend,r1
.Lsdk_purge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lsdk_purge
    mov.l .Lsdk_ccr,r1
    mov.l .Lsdk_restore,r0
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Lsdk_done:
    mov r9,r0
    lds.l @r15+,pr
    mov.l @r15+,r9
    mov.l @r15+,r8
    rts
    nop
    .align 2
.Lsdk_sr_mask: .long 0x400000f0
.Lsdk_mmucr: .long 0xff000010
.Lsdk_ccr: .long 0xff00001c
.Lsdk_expected: .long 0x100
.Lsdk_ram: .long 0x8c000000
.Lsdk_ramend: .long KUI_RETAIL_RAM_END
.Lsdk_load: .long KUI_RETAIL_EXEC_ADDRESS
.Lsdk_stack_low: .long 0x8c010000
.Lsdk_p2_address: .long .Lsdk_p2+0x20000000
.Lsdk_on: .long 0x909
.Lsdk_proof: .long C(kui_native_sdk_sci_family_find)
.Lsdk_restore: .long 0x908
    .size C(kui_native_stage_cached_sdk_family),.-C(kui_native_stage_cached_sdk_family)
#endif
