/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_loader_layout.h"
#include "kui/smash_card_session.h"
#if KUI_VMU_MAPLE_OBSERVE && KUI_VMU_READONLY_PROOF == 4
#include "kui/vmu_maple_observe_profile.h"
#endif
#ifndef KUI_RETAIL_STAGE_BIN
#define KUI_RETAIL_STAGE_BIN "build/retail/stage.bin"
#endif
#ifdef KUI_RETAIL_CE
#define STAGE_ADDRESS KUI_RETAIL_CE_STAGE_ADDRESS
#define PACKAGE_MAGIC KUI_RETAIL_CE_PACKAGE_MAGIC
#else
#define STAGE_ADDRESS KUI_RETAIL_STAGE_ADDRESS
#if KUI_RETAIL_ZSO
#if KUI_ZSO_CODECS == 2
#define PACKAGE_MAGIC "KUIRLB01"
#else
#define PACKAGE_MAGIC "KUIRZB01"
#endif
#else
#define PACKAGE_MAGIC KUI_RETAIL_PACKAGE_MAGIC
#endif
#endif
    .section .entry,"ax"
    .align 2
    .global _start
_start:
#if KUI_SMASH_NATIVE_GIN_ADOPT
    /* This opt-in capsule is read into original-ROM scratch, preserving
     * GIN's already loaded MDE and active register bank. Its independently
     * linked verifier must finish before the stage reclaims high MDE RAM. */
    mov.l .Lstack,r15
    mov.l .Lverify_cache_p2,r0
    jmp @r0
    nop
.Lverify_cache:
    mov.l .Lcache_register,r0
    mov.l .Lcache_profile,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov.l .Lverify,r0
    jsr @r0
    nop
    tst r0,r0
    bt .Lverified
    mov.l .Lverify_failure,r1
    mov.l r0,@r1
.Lverify_halt:
    bra .Lverify_halt
    nop
.Lverified:
#elif !KUI_SMASH_CE_CHILD
    mov.l .Lsr,r0
    ldc r0,sr
#endif
    mov.l .Lsource,r0
    mov.l .Ltarget,r1
    mov.l .Llength,r2
    shlr2 r2
.Lcopy:
    mov.l @r0+,r3
    mov.l r3,@r1
    add #4,r1
    dt r2
    bf .Lcopy
    mov.l .Lstack,r15
    mov.l .Lmap,r4
    mov.l .Ltarget,r0
    jmp @r0
    nop
    .align 2
.Lsr: .long 0x500000f0
#if KUI_SMASH_NATIVE_GIN_ADOPT
.Lsource: .long 0xacc02000
#else
.Lsource: .long __retail_stage_blob_start
#endif
.Ltarget:
#if KUI_SMASH_NATIVE_GIN_ADOPT
    .long STAGE_ADDRESS+0x20000000
#else
    .long STAGE_ADDRESS
#endif
.Llength: .long __retail_stage_blob_end-__retail_stage_blob_start
.Lstack: .long KUI_RETAIL_STAGE_STACK
.Lmap:
#if KUI_SMASH_NATIVE_GIN_ADOPT
    .long 0x8cc01000
.Lverify_cache_p2: .long .Lverify_cache-_start+0xacc00000
.Lcache_register: .long 0xff00001c
.Lcache_profile: .long 0x00000909
.Lverify: .long 0x8cc00300
.Lverify_failure: .long 0xacd00000
#else
    .long __retail_map
#endif
#if KUI_SMASH_NATIVE_GIN_ADOPT
    .section .smash_native_gin_capability,"a"
    .ascii "KUINAD01"
    .long 1,16
    .section .smash_native_gin_header,"a"
    .ascii "KUISNGI1"
    .long 1,160,3,0
    .zero 136
    .section .smash_native_gin_verify,"ax"
    .balign 4
    .incbin KUI_SMASH_NATIVE_GIN_VERIFY_BIN
#endif
#if KUI_SMASH_CE_CHILD
#if KUI_SMASH_CE_RETURN
    .section .smash_ce_return_capability,"a"
    .long 0x5349554b,0x31524543,1,16
#endif
    /* The original post-GIN SR already blocks interrupts. Keep its active
     * register bank; all integer state is sealed in this launch-local record.
     * Only the authenticated collection handoff fills the blank record. */
    .section .smash_ce_gin_header,"a"
    .ascii "KUISGIN1"
    .long 1,160,1+KUI_SMASH_SWIRL_VMU,0
    .zero 136
#endif
#if KUI_SMASH_NATIVE_MENU && !KUI_SMASH_NATIVE_GIN_ADOPT
    /* A matching cold handoff can inspect this in its existing first512B
     * capsule probe, before overwriting or preserving any source image. */
    .section .cold_image_header,"a"
    .ascii "KUICAP01"
    .long 1,24,1848288,~1848288
#endif
#if KUI_SMASH_CARD_SESSION
    .section .collection_session_header,"a"
    .ascii "KUISCR02"
    .long KUI_SMASH_CARD_RECEIPT_VERSION,96,0,0
    .zero 72
#endif
    .section .package_header,"a"
    .ascii PACKAGE_MAGIC
    .long KUI_RETAIL_PACKAGE_VERSION
    .long KUI_RETAIL_HEADER_BYTES
    .long KUI_RETAIL_MAP_OFFSET
    .long KUI_RETAIL_MAP_BYTES
    .long STAGE_ADDRESS
    .long __retail_stage_blob_end-__retail_stage_blob_start
    .long STAGE_ADDRESS
    .long KUI_RETAIL_EXEC_ADDRESS
    .long KUI_RETAIL_EXEC_MAX_BYTES
    .long KUI_RETAIL_STAGE_STACK
    .long KUI_RETAIL_STAGE_BLOB_OFFSET
    .long KUI_RETAIL_RESIDENT_ADDRESS
    .long KUI_RETAIL_STANDARD_LIMIT
    .long 0
#if KUI_VMU_MAPLE_OBSERVE && KUI_VMU_READONLY_PROOF == 4
    .section .vmu_launch_header,"a"
    .balign 4
    .global __vmu_launch_capability
    .type __vmu_launch_capability,@object
__vmu_launch_capability:
    .ascii "KUIVMU04"
    .long 1,64,KUI_VMU_MAPLE_PROFILE,4,1
#if KUI_VMU_MAPLE_PROFILE == KUI_VMU_MAPLE_PROFILE_SILVER
    .long 0x5e1b611a,0x770d2fed,0x7d83cf24,2956128,45000,547556
#else
    .long 0xe25531d1,0x38ba2868,0xcdc493b3,748444,45000,548634
#endif
    .long 0,0,0
    .size __vmu_launch_capability,.-__vmu_launch_capability
#endif
    .section .retail_map,"a"
    .global __retail_map
__retail_map:
    .space KUI_RETAIL_MAP_BYTES
    .section .stage_blob,"a"
    .align 2
    .global __retail_stage_blob_start,__retail_stage_blob_end
__retail_stage_blob_start:
    .incbin KUI_RETAIL_STAGE_BIN
    .align 2
__retail_stage_blob_end:
