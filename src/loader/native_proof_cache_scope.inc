/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared immutable-image proof scopes. No game, SDK or live I/O state is cached. */
#include "kui/retail_loader_layout.h"
#if KUI_NATIVE_CACHED_EXTENT && !defined(KUI_RETAIL_CE) && !KUI_SMASH_CE_CHILD && !KUI_NATIVE_PHYSICAL_DISC
#if !KUI_VMU_NATIVE_CACHED_CODE
#error "The pure CRC cache scope requires the native instruction-cached relay"
#endif
/* Reusable three-argument immutable CRC scope. The BOOT2 relay has already
 * published RAM, stopped the owner, restored its entry bytes and selected
 * normal-index CCR=0x100. Cache only a bounded P1 RAM source on the known
 * privileged, interrupt-masked, MMU-off profile and a P1 RAM stack. Every
 * unsupported state calls the exact original leaf without changing CCR.
 * The linked leaf/CRC has only an 8-byte private stack write set (bound by
 * the actual-instruction audit). Reserve 128 bytes below this scope's frame
 * and publish every intersecting cache line before disabling operand cache.
 * No output object or other RAM is written in this pure leaf. */
    .text
    .align 2
    .global __kui_native_cached_boot_crc
    .set __kui_native_cached_boot_crc,1
    .global C(kui_native_stage_cached_crc32)
    .type C(kui_native_stage_cached_crc32),@function
C(kui_native_stage_cached_crc32):
    mov.l r8,@-r15
    mov.l r9,@-r15
    sts.l pr,@-r15
    mov #0,r8
    stc sr,r0
    mov.l .Lcrc_sr_mask,r1
    and r1,r0
    cmp/eq r1,r0
    bf .Lcrc_call
    mov.l .Lcrc_mmucr,r1
    mov.l @r1,r0
    tst #1,r0
    bf .Lcrc_call
    mov.l .Lcrc_ccr,r1
    mov.l @r1,r0
    mov.l .Lcrc_expected,r2
    cmp/eq r2,r0
    bf .Lcrc_call
    mov.l .Lcrc_ram,r0
    cmp/hs r0,r5
    bf .Lcrc_call
    mov.l .Lcrc_ramend,r1
    cmp/hs r1,r5
    bt .Lcrc_call
    mov r1,r2
    sub r5,r2
    cmp/hs r6,r2
    bf .Lcrc_call
    tst r6,r6
    bt .Lcrc_call
    add #127,r0
    add #1,r0
    cmp/hs r0,r15
    bf .Lcrc_call
    mov r15,r0
    tst #3,r0
    bf .Lcrc_call
    add #12,r0
    cmp/hi r1,r0
    bt .Lcrc_call
    mov #1,r8
    mov.l .Lcrc_p2_address,r0
    jmp @r0
    nop
.Lcrc_p2:
    mov.l .Lcrc_ccr,r1
    mov.l .Lcrc_on,r0
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Lcrc_call:
    mov.l .Lcrc_proof,r0
    jsr @r0
    nop
    mov r0,r9
    tst r8,r8
    bt .Lcrc_done
    mov r15,r0
    add #-128,r0
    mov #-32,r2
    and r2,r0
    mov r15,r1
    add #12,r1
.Lcrc_purge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lcrc_purge
    mov.l .Lcrc_ccr,r1
    mov.l .Lcrc_restore,r0
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Lcrc_done:
    mov r9,r0
    lds.l @r15+,pr
    mov.l @r15+,r9
    mov.l @r15+,r8
    rts
    nop
    .align 2
.Lcrc_sr_mask: .long 0x400000f0
.Lcrc_mmucr: .long 0xff000010
.Lcrc_ccr: .long 0xff00001c
.Lcrc_expected: .long 0x100
.Lcrc_ram: .long 0x8c000000
.Lcrc_ramend: .long KUI_RETAIL_RAM_END
.Lcrc_p2_address: .long .Lcrc_p2+0x20000000
.Lcrc_on: .long 0x909
.Lcrc_proof: .long C(kui_retail_stage_crc32_leaf)
.Lcrc_restore: .long 0x908
    .size C(kui_native_stage_cached_crc32),.-C(kui_native_stage_cached_crc32)
#endif

#if KUI_NATIVE_CACHED_RESOLUTION
#if !KUI_VMU_NATIVE_CACHED_CODE || !KUI_VMU_NATIVE_CACHE_PROBE
#error "Cached resolution requires the instruction-cached resolution relay"
#endif
/* This entry has exactly the public seven-argument replay ABI. Preserve the
 * caller's three stack words in a new outgoing argument area before changing
 * cache mode. The pure C callee receives the original image, complete size,
 * load address, record, record size, independent digest and private output.
 * No SDK call or live ownership/admission check enters this scope. */
    .text
    .align 2
    .global __kui_native_cached_resolution
    .set __kui_native_cached_resolution,1
    .global C(kui_vmu_native_cached_resolution_replay)
    .type C(kui_vmu_native_cached_resolution_replay),@function
C(kui_vmu_native_cached_resolution_replay):
    mov.l r8,@-r15
    mov.l r9,@-r15
    sts.l pr,@-r15
    add #-12,r15
    mov.l @(24,r15),r0
    mov.l r0,@r15
    mov.l @(28,r15),r0
    mov.l r0,@(4,r15)
    mov.l @(32,r15),r0
    mov.l r0,@(8,r15)
    mov.l .Lgeneric_resolution_p2_address,r0
    jmp @r0
    nop
.Lgeneric_resolution_p2:
    mov.l .Lgeneric_resolution_ccr,r1
    mov.l @r1,r0
    mov r0,r8
    mov.l .Lgeneric_resolution_expected,r2
    cmp/eq r2,r0
    bf .Lgeneric_resolution_call
    mov.l .Lgeneric_resolution_on,r0
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Lgeneric_resolution_call:
    mov.l .Lgeneric_resolution_proof,r0
    jsr @r0
    nop
    mov r0,r9
    mov.l .Lgeneric_resolution_expected,r0
    cmp/eq r0,r8
    bf .Lgeneric_resolution_done
    mov.l .Lgeneric_resolution_ram,r0
    mov.l .Lgeneric_resolution_ramend,r1
.Lgeneric_resolution_purge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lgeneric_resolution_purge
    mov.l .Lgeneric_resolution_off,r0
    or r8,r0
    mov.l .Lgeneric_resolution_ccr,r1
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Lgeneric_resolution_done:
    mov r9,r0
    add #12,r15
    lds.l @r15+,pr
    mov.l @r15+,r9
    mov.l @r15+,r8
    rts
    nop
    .align 2
.Lgeneric_resolution_p2_address: .long .Lgeneric_resolution_p2+0x20000000
.Lgeneric_resolution_ccr: .long 0xff00001c
.Lgeneric_resolution_expected: .long 0x100
.Lgeneric_resolution_on: .long 0x909
.Lgeneric_resolution_proof: .long C(kui_vmu_native_resolution_cache_replay)
.Lgeneric_resolution_ram: .long 0x8c000000
.Lgeneric_resolution_ramend: .long KUI_RETAIL_RAM_END
.Lgeneric_resolution_off: .long 0x808
    .size C(kui_vmu_native_cached_resolution_replay),.-C(kui_vmu_native_cached_resolution_replay)
#endif

#if KUI_NATIVE_CACHED_EXTENT
#if !KUI_VMU_NATIVE_CACHED_CODE
#error "The pure extent cache scope requires the native instruction-cached relay"
#endif
/* Pure immutable-image proof only, called on the relay's P1 high stack.
 * The surrounding relay has already published RAM and selected CCR=0x100.
 * Unknown cache profiles take the original call without changing CCR.
 * Every CCR change executes through P2 and has eight following NOPs. */
    .align 2
    .global __kui_native_cached_extent
    .set __kui_native_cached_extent,1
    .global C(kui_vmu_native_cached_extent)
    .type C(kui_vmu_native_cached_extent),@function
C(kui_vmu_native_cached_extent):
    mov.l r8,@-r15
    mov.l r9,@-r15
    sts.l pr,@-r15
    mov.l .Lextent_p2_address,r0
    jmp @r0
    nop
.Lextent_p2:
    mov.l .Lextent_ccr,r1
    mov.l @r1,r0
    mov r0,r8
    mov.l .Lextent_expected,r2
    cmp/eq r2,r0
    bf .Lextent_call
    mov.l .Lextent_on,r0
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Lextent_call:
    mov.l .Lextent_proof,r0
    jsr @r0
    nop
    mov r0,r9
    mov.l .Lextent_expected,r0
    cmp/eq r0,r8
    bf .Lextent_done
/* Publish the proof's P1 stack/output before returning to operand-cache-off
 * relay code. A full normal-index RAM purge also includes this 12B frame. */
    mov.l .Lextent_ram,r0
    mov.l .Lextent_ramend,r1
.Lextent_purge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lextent_purge
    mov.l .Lextent_off,r0
    or r8,r0
    mov.l .Lextent_ccr,r1
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Lextent_done:
    mov r9,r0
    lds.l @r15+,pr
    mov.l @r15+,r9
    mov.l @r15+,r8
    rts
    nop
    .align 2
.Lextent_p2_address: .long .Lextent_p2+0x20000000
.Lextent_ccr: .long 0xff00001c
.Lextent_expected: .long 0x100
.Lextent_on: .long 0x909
.Lextent_proof: .long C(kui_vmu_native_program_extent)
.Lextent_ram: .long 0x8c000000
.Lextent_ramend: .long KUI_RETAIL_RAM_END
.Lextent_off: .long 0x808
    .size C(kui_vmu_native_cached_extent),.-C(kui_vmu_native_cached_extent)
#endif

#if KUI_SMASH_NATIVE_MENU
#if !KUI_VMU_NATIVE_CACHED_CODE || !KUI_VMU_NATIVE_CACHE_PROBE
#error "Smash native cached resolution requires the instruction-cached resolution relay"
#endif
/* The complete image SHA256 and pinned resolution-record proof are pure:
 * only private stage output/stack is written. Cache operands for that call,
 * then publish every dirty RAM line and restore the relay's exact profile
 * before SDK/coherence validation, hook installation or durable card work.
 * No argument stack is forwarded: the C proof takes no arguments. */
    .align 2
    .global __kui_smash_native_menu_cached_resolution
    .set __kui_smash_native_menu_cached_resolution,1
    .global C(kui_smash_native_menu_cached_resolution_replay)
    .type C(kui_smash_native_menu_cached_resolution_replay),@function
C(kui_smash_native_menu_cached_resolution_replay):
    mov.l r8,@-r15
    mov.l r9,@-r15
    sts.l pr,@-r15
    mov.l .Lresolution_p2_address,r0
    jmp @r0
    nop
.Lresolution_p2:
    mov.l .Lresolution_ccr,r1
    mov.l @r1,r0
    mov r0,r8
    mov.l .Lresolution_expected,r2
    cmp/eq r2,r0
    bf .Lresolution_call
    mov.l .Lresolution_on,r0
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Lresolution_call:
    mov.l .Lresolution_proof,r0
    jsr @r0
    nop
    mov r0,r9
    mov.l .Lresolution_expected,r0
    cmp/eq r0,r8
    bf .Lresolution_done
    mov.l .Lresolution_ram,r0
    mov.l .Lresolution_ramend,r1
.Lresolution_purge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lresolution_purge
    mov.l .Lresolution_off,r0
    or r8,r0
    mov.l .Lresolution_ccr,r1
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Lresolution_done:
    mov r9,r0
    lds.l @r15+,pr
    mov.l @r15+,r9
    mov.l @r15+,r8
    rts
    nop
    .align 2
.Lresolution_p2_address: .long .Lresolution_p2+0x20000000
.Lresolution_ccr: .long 0xff00001c
.Lresolution_expected: .long 0x100
.Lresolution_on: .long 0x909
.Lresolution_proof: .long C(kui_smash_native_menu_resolution_replay)
.Lresolution_ram: .long 0x8c000000
.Lresolution_ramend: .long KUI_RETAIL_RAM_END
.Lresolution_off: .long 0x808
    .size C(kui_smash_native_menu_cached_resolution_replay),.-C(kui_smash_native_menu_cached_resolution_replay)

/* Full-source fallback for an SDK proof that covers only a reclaimed
 * program prefix. Exact same private-stack/operand-publication contract. */
    .align 2
    .global __kui_smash_native_menu_cached_full_source_owner
    .set __kui_smash_native_menu_cached_full_source_owner,1
    .global C(kui_smash_native_menu_cached_full_source_owner)
    .type C(kui_smash_native_menu_cached_full_source_owner),@function
C(kui_smash_native_menu_cached_full_source_owner):
    mov.l r8,@-r15
    mov.l r9,@-r15
    sts.l pr,@-r15
    mov.l .Lfull_owner_p2_address,r0
    jmp @r0
    nop
.Lfull_owner_p2:
    mov.l .Lfull_owner_ccr,r1
    mov.l @r1,r0
    mov r0,r8
    mov.l .Lfull_owner_expected,r2
    cmp/eq r2,r0
    bf .Lfull_owner_call
    mov.l .Lfull_owner_on,r0
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Lfull_owner_call:
    mov.l .Lfull_owner_proof,r0
    jsr @r0
    nop
    mov r0,r9
    mov.l .Lfull_owner_expected,r0
    cmp/eq r0,r8
    bf .Lfull_owner_done
    mov.l .Lfull_owner_ram,r0
    mov.l .Lfull_owner_ramend,r1
.Lfull_owner_purge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lfull_owner_purge
    mov.l .Lfull_owner_off,r0
    or r8,r0
    mov.l .Lfull_owner_ccr,r1
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Lfull_owner_done:
    mov r9,r0
    lds.l @r15+,pr
    mov.l @r15+,r9
    mov.l @r15+,r8
    rts
    nop
    .align 2
.Lfull_owner_p2_address: .long .Lfull_owner_p2+0x20000000
.Lfull_owner_ccr: .long 0xff00001c
.Lfull_owner_expected: .long 0x100
.Lfull_owner_on: .long 0x909
.Lfull_owner_proof: .long C(kui_smash_native_menu_full_source_owner)
.Lfull_owner_ram: .long 0x8c000000
.Lfull_owner_ramend: .long KUI_RETAIL_RAM_END
.Lfull_owner_off: .long 0x808
    .size C(kui_smash_native_menu_cached_full_source_owner),.-C(kui_smash_native_menu_cached_full_source_owner)
#endif

