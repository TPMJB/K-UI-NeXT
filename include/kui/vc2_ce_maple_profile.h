/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_MAPLE_PROFILE_H
#define KUI_VC2_CE_MAPLE_PROFILE_H

#ifndef KUI_SMASH_SWIRL_VMU
#define KUI_SMASH_SWIRL_VMU 0
#endif
#if KUI_SMASH_SWIRL_VMU != 0 && KUI_SMASH_SWIRL_VMU != 1
#error "Swirl VMU source profile must be explicitly zero or one"
#endif
#if KUI_SMASH_SWIRL_VMU
#include "kui/swirl_ce_maple_profile.h"
#else
/* Source-specific facts from the exact owner kernel. These constants do not
 * establish a live reservation, a mapped callback or guest packet ownership.
 * The bridge below wraps the ordinary worker call; it does not replace the
 * completion prologue or import an Armada driver offset. */
#define KUI_VC2_CE_KERNEL_FILE_BYTES       0x001c1800
#define KUI_VC2_CE_KERNEL_PREFIX_BYTES     0x00000800
#define KUI_VC2_CE_KERNEL_BODY_BYTES       0x001c1000
#define KUI_VC2_CE_KERNEL_BODY_P1          0x8c010000
#define KUI_VC2_CE_KERNEL_BODY_CRC32       0x6d9c21a3
#define KUI_VC2_CE_ROMHDR_P1               0x8c1d063c
#define KUI_VC2_CE_ROMHDR_BYTES            84
#define KUI_VC2_CE_ROMHDR_CRC32            0x89f14aa6
#define KUI_VC2_CE_RAMEND_WORD_P1          0x8c1d0658
#define KUI_VC2_CE_ORIGINAL_RAMEND         0x8cfd0000

#define KUI_VC2_CE_MAPLE_NOMINAL_BASE      0x01ce0000
#define KUI_VC2_CE_MAPLE_TEXT_RECORD_P1    0x8c0a6d58
#define KUI_VC2_CE_MAPLE_TEXT_RECORD_CRC32 0x70e98d32
#define KUI_VC2_CE_MAPLE_TEXT_VSIZE        0x00006c32
#define KUI_VC2_CE_MAPLE_TEXT_EXTENDED     0x00006e00
#define KUI_VC2_CE_MAPLE_TEXT_RVA          0x00001000
#define KUI_VC2_CE_MAPLE_TEXT_P1           0x8c0b1000
#define KUI_VC2_CE_MAPLE_WORKER_PATCH_RVA  0x0000723c
#define KUI_VC2_CE_MAPLE_WORKER_PATCH_P1   0x8c0b723c
#define KUI_VC2_CE_MAPLE_WORKER_SOURCE_CRC 0xae094759
/* Authored NOP; BSR bridge. The unchanged third halfword remains the
 * source worker's argument-setup delay instruction. */
#define KUI_VC2_CE_MAPLE_WORKER_PATCH_WORD 0xb4f90009
#define KUI_VC2_CE_MAPLE_WORKER_RESUME_RVA 0x00007242
#define KUI_VC2_CE_MAPLE_COMPLETION_RVA    0x00006f64
#define KUI_VC2_CE_MAPLE_SETKMODE_RVA      0x00007808
#define KUI_VC2_CE_MAPLE_SETKMODE_ORDINAL  630
#define KUI_VC2_CE_MAPLE_ROOT_RVA          0x000082b8
#define KUI_VC2_CE_MAPLE_WAIT_RVA          0x000077a8
#define KUI_VC2_CE_MAPLE_WAIT_IAT_RVA      0x00008024
#define KUI_VC2_CE_MAPLE_WAIT_ORDINAL      497
/* The ordinary worker waits on the DMA event at +178. Its CreateThread
 * HANDLE is separately stored at +180; only that self-thread handle is an
 * admissible one-ms wait that cannot consume a Maple completion event. */
#define KUI_VC2_CE_MAPLE_DMA_EVENT_OFFSET  0x00000178
#define KUI_VC2_CE_MAPLE_THREAD_HANDLE_OFFSET 0x00000180

#define KUI_VC2_CE_MAPLE_PADDING_RVA       0x00007c34
#define KUI_VC2_CE_MAPLE_PADDING_P1        0x8c0b7c34
#define KUI_VC2_CE_MAPLE_PADDING_BYTES     460
#define KUI_VC2_CE_MAPLE_BRIDGE_RVA        KUI_VC2_CE_MAPLE_PADDING_RVA
#define KUI_VC2_CE_MAPLE_BRIDGE_BYTES      100
#define KUI_VC2_CE_MAPLE_BRIDGE_CALLBACK_OFFSET 0x54
#define KUI_VC2_CE_MAPLE_BRIDGE_PRE_RETURN_RVA 0x00007c58
#define KUI_VC2_CE_MAPLE_BRIDGE_END_RVA    0x00007c98
#define KUI_VC2_CE_MAPLE_RESERVE_P1        0x8cfc4000
#define KUI_VC2_CE_MAPLE_RESERVE_END_P1    0x8cfd0000

/* Verified NK and extension geometry. The scalar audit is independent of
 * the live repeated mapping/cache/owner checks required by a sampler. */
#define KUI_VC2_CE_CURRENT_PROCESS_P1     0x8c1d5890
#define KUI_VC2_CE_CURRENT_THREAD_P1      0x8c1d5894
#define KUI_VC2_CE_PROCESS_TABLE_P1       0x8c1de6e0
#define KUI_VC2_CE_PROCESS_STRIDE         0x0000009c
#define KUI_VC2_CE_PROCESS_COUNT          32
#define KUI_VC2_CE_MAPLE_EXTENSION_BYTES  0x000001a0

#endif

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A read-only source gate for a contiguous, original loaded kernel body.
 * The caller must separately prove that every byte arrived in this exact
 * interval, no kernel instruction has run, and this is a stable snapshot.
 * CRC binds accidental version/copy changes; it is not an authenticity API.
 * In particular, a successful result authorizes no patch or memory claim. */
static inline uint32_t kui_vc2_ce_maple_crc32(const uint8_t *data,size_t bytes) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<bytes;i++) {
        crc^=data[i];
        for(unsigned bit=0;bit<8u;bit++)
            crc=(crc>>1)^((0u-(crc&1u))&UINT32_C(0xedb88320));
    }
    return ~crc;
}
static inline bool kui_vc2_ce_maple_source_valid(const uint8_t *body,size_t bytes) {
    if(!body || bytes!=KUI_VC2_CE_KERNEL_BODY_BYTES ||
       (uintptr_t)body>UINTPTR_MAX-bytes ||
       kui_vc2_ce_maple_crc32(body,bytes)!=KUI_VC2_CE_KERNEL_BODY_CRC32) return false;
    const size_t header=KUI_VC2_CE_ROMHDR_P1-KUI_VC2_CE_KERNEL_BODY_P1;
    const size_t record=KUI_VC2_CE_MAPLE_TEXT_RECORD_P1-KUI_VC2_CE_KERNEL_BODY_P1;
    const size_t worker=KUI_VC2_CE_MAPLE_WORKER_PATCH_P1-KUI_VC2_CE_KERNEL_BODY_P1;
    if(kui_vc2_ce_maple_crc32(body+header,KUI_VC2_CE_ROMHDR_BYTES)!=KUI_VC2_CE_ROMHDR_CRC32 ||
       kui_vc2_ce_maple_crc32(body+record,24u)!=KUI_VC2_CE_MAPLE_TEXT_RECORD_CRC32 ||
       kui_vc2_ce_maple_crc32(body+worker,32u)!=KUI_VC2_CE_MAPLE_WORKER_SOURCE_CRC) return false;
#if !KUI_SMASH_SWIRL_VMU
    const size_t padding=KUI_VC2_CE_MAPLE_PADDING_P1-KUI_VC2_CE_KERNEL_BODY_P1;
    for(size_t i=0;i<KUI_VC2_CE_MAPLE_PADDING_BYTES;i++) if(body[padding+i]) return false;
#endif
    return true;
}
#if KUI_SMASH_SWIRL_VMU
_Static_assert((KUI_SWIRL_CE_MAPLE_RELOCATE_P1&0xfffu)==0u &&
    KUI_SWIRL_CE_MAPLE_RELOCATE_P1+KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES==0x8cfc0000u,
    "Swirl text source is whole reserved pages below helper BSS");
_Static_assert(KUI_VC2_CE_MAPLE_TEXT_VSIZE<=KUI_VC2_CE_MAPLE_BRIDGE_RVA-KUI_VC2_CE_MAPLE_TEXT_RVA &&
    KUI_VC2_CE_MAPLE_TEXT_EXTENDED<=KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES,
    "Swirl bridge and mapped page tail fit the private text source");
#endif
_Static_assert(KUI_VC2_CE_MAPLE_BRIDGE_RVA+KUI_VC2_CE_MAPLE_BRIDGE_BYTES==
    KUI_VC2_CE_MAPLE_BRIDGE_END_RVA,"VC2 bridge has an exclusive end");
_Static_assert(KUI_VC2_CE_MAPLE_BRIDGE_BYTES<=KUI_VC2_CE_MAPLE_PADDING_BYTES,
    "VC2 bridge stays within stored text padding");
_Static_assert(KUI_VC2_CE_MAPLE_TEXT_RVA+KUI_VC2_CE_MAPLE_TEXT_EXTENDED==0x7e00u,
    "VC2 extended executable text remains inside original stored bytes");
#endif
#endif
