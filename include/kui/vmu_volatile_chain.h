/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_VOLATILE_CHAIN_H
#define KUI_VMU_VOLATILE_CHAIN_H

#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_VOLATILE_CHAIN_FRAMES 32u
#define KUI_VMU_VOLATILE_CHAIN_ENTRIES 256u
#define KUI_VMU_VOLATILE_CHAIN_BYTES 512u
#define KUI_VMU_VOLATILE_CHAIN_SOURCE_BYTES UINT32_C(0x3000)
#define KUI_VMU_VOLATILE_CHAIN_PASSTHROUGH_BYTES KUI_VMU_VOLATILE_CHAIN_SOURCE_BYTES
#define KUI_VMU_VOLATILE_CHAIN_RX_BYTES 1024u
#define KUI_VMU_VOLATILE_CHAIN_LAST UINT32_C(0x80000000)

/* One real descriptor captured by the caller's admitted SDK parser. Exact
 * one-word 00000700 padding is omitted. payload0 is required for physical
 * GETCOND. For a physical request with more than one payload word, payload
 * owns the entire immutable captured payload, including payload0. No virtual
 * request payload is copied into the physical DMA chain. */
struct kui_vmu_volatile_chain_frame {
    uint32_t control, rx, header, payload0;
    const uint8_t *payload;
};

/* Private, immutable between build and verify. CRC is an accidental-change
 * guard, not authentication or proof of the original SDK source contents. */
struct kui_vmu_volatile_chain_plan {
    uint32_t source_physical, source_bytes, output_physical, output_bytes;
    uint32_t output_capacity, frame_count, physical_count, virtual_count;
    uint32_t padding_count, crc32, virtual_address;
};
_Static_assert(sizeof(struct kui_vmu_volatile_chain_frame)<=24u,
    "Captured chain frames must remain compact");
_Static_assert(sizeof(struct kui_vmu_volatile_chain_plan)==44u,
    "Filtered chain metadata must remain bounded");

enum kui_vmu_volatile_chain_result {
    KUI_VMU_VOLATILE_CHAIN_OK = 0,
    KUI_VMU_VOLATILE_CHAIN_ARGUMENT = -1,
    KUI_VMU_VOLATILE_CHAIN_SPAN = -2,
    KUI_VMU_VOLATILE_CHAIN_OVERLAP = -3,
    KUI_VMU_VOLATILE_CHAIN_DESCRIPTOR = -4,
    KUI_VMU_VOLATILE_CHAIN_UNSUPPORTED = -5,
    KUI_VMU_VOLATILE_CHAIN_CAPACITY = -6,
    KUI_VMU_VOLATILE_CHAIN_EMPTY = -7,
    KUI_VMU_VOLATILE_CHAIN_CHANGED = -8
};

/* Pure bounded preparation: filter EVERY port-A unit-1 packet, regardless of
 * command, including BLOCKWRITE/BLOCKSYNC. Retain only MAIN A-D DEVINFO or
 * ALLINFO with no payload, or GETCOND with the one-word controller function.
 * Other one-hot physical subunits admit INFO/ALLINFO, or one-word GETCOND for
 * controller, memory-card, LCD, clock or rumble functions. No physical writes, RESET/KILL,
 * SETCOND, storage MEDIA/READ, or unknown function queries are admitted.
 * Refuse all other commands and patterns. The retained bytes
 * match the captured descriptors except LAST moves to the last retained one.
 * At least one retained packet is required; this API never starts an empty DMA.
 *
 * The caller must prove that the immutable snapshots describe the ENTIRE SDK
 * chain through LAST, that omitted words are exactly 00000700, and that source
 * bytes include all descriptors, full payloads and padding. It must separately
 * capture complete virtual write payloads before hardware starts. This compact
 * API cannot prove those source facts. A source hash/ownership guard remains
 * the caller's responsibility.
 *
 * Source and output physical addresses are 32-byte aligned main RAM. Output
 * capacity is a whole private allocation, 12..512 bytes, and its pointer is
 * 32-byte aligned. Each RX owns a disjoint 1024-byte slot. Neither source nor
 * any RX slot may overlap that full output allocation. The caller proves the
 * pointer maps to output_physical and uses coherent P2 stores on SH4; recognized
 * SH4 main-RAM aliases are checked here, but arbitrary host pointers cannot
 * establish physical ownership. Plan/snapshots/output must remain exclusive.
 *
 * Validate the complete batch before changing plan or output. Failure changes
 * neither. No source/RX store, MMIO, cache/IRQ operation, allocation or storage
 * callback occurs. Padding is stripped, never copied. The caller alone owns
 * MDSTAR selection, one MDST start, idle completion, source restoration and SDK
 * publication ordering. */
int kui_vmu_volatile_chain_build(struct kui_vmu_volatile_chain_plan *,
    const struct kui_vmu_volatile_chain_frame *, size_t frame_count,
    uint32_t source_physical, uint32_t source_bytes,
    uint8_t *output, size_t output_capacity, uint32_t output_physical);

/* Explicit native transport. virtual_address is 1 or 2. The build's virtual
 * slot (A2, or A1 in a KUI_VMU_VIRTUAL_A1 build) filters only itself and also
 * retains exact physical VMU MEDIA/READ/WRITE/SYNC, RESET, LCD BWRITE and
 * LCD/clock SETCOND requests to the other slot, including their complete
 * captured payloads. Those source descriptors and reply slots remain owned by
 * the physical SDK device, and output capacity may be up to
 * KUI_VMU_VOLATILE_CHAIN_PASSTHROUGH_BYTES. The other address keeps the legacy
 * behavior above, with no physical card. Payload snapshots must be disjoint
 * from plan, frame metadata, output and all RX allocations and remain immutable
 * through build. The caller separately validates every physical response before
 * allowing the SDK to consume it. */
int kui_vmu_volatile_chain_build_address(struct kui_vmu_volatile_chain_plan *,
    const struct kui_vmu_volatile_chain_frame *, size_t frame_count,
    uint32_t source_physical, uint32_t source_bytes,
    uint8_t *output, size_t output_capacity, uint32_t output_physical,
    uint8_t virtual_address);

/* Before completion publication, recheck the immutable private chain's CRC,
 * complete descriptor structure, physical-only whitelist, LAST and RX geometry.
 * This performs no stores and does not replace the caller's live SDK/MMIO/bank
 * ownership checks or its original-source guard. Capacity must still describe
 * the same full owned output allocation used by build. */
int kui_vmu_volatile_chain_verify(const struct kui_vmu_volatile_chain_plan *,
    const uint8_t *output, size_t output_capacity);

#endif
