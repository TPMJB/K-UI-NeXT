/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_VOLATILE_H
#define KUI_VMU_VOLATILE_H
#include "kui/vmu_emulation.h"

#define KUI_VMU_VOLATILE_BLOCKS 16u
#define KUI_VMU_VOLATILE_STATE_BYTES_MAX (10u * 1024u)

enum kui_vmu_volatile_result {
    KUI_VMU_VOLATILE_HANDLED = 1,
    KUI_VMU_VOLATILE_REFUSED = 0,
    KUI_VMU_VOLATILE_ARGUMENT = -1,
    KUI_VMU_VOLATILE_CAPACITY = -2,
    KUI_VMU_VOLATILE_INTERNAL = -3,
    KUI_VMU_VOLATILE_FULL = -4
};

/* Explicitly VOLATILE A1 experiment: final write ACK means committed to this
 * private RAM cache, NEVER durable storage. Reset/relaunch loses every change.
 * Reuses the protocol's four-phase transaction state machine with deliberately
 * RAM-only commit semantics; the durable backing-store contract is unchanged.
 * No allocation, MMIO, SD write, eviction or full-card mirror. Initialize in
 * its final location and do not copy/mutate private state after initialization.
 * Single caller with disjoint state/request/response/result buffers required. */
struct kui_vmu_volatile {
    kui_vmu_emulation_t protocol;
    uint8_t cache[KUI_VMU_VOLATILE_BLOCKS][512];
    uint16_t blocks[KUI_VMU_VOLATILE_BLOCKS];
    uint32_t requests, reads, base_reads, cache_reads, refused;
    uint32_t write_phases, ram_commits, cached_blocks, full_faults;
    int (*reader)(void *, uint16_t, uint8_t[512]);
    void *reader_context;
    uint8_t ready, faulted;
};
_Static_assert(sizeof(struct kui_vmu_volatile)<=KUI_VMU_VOLATILE_STATE_BYTES_MAX,
    "Volatile cache exceeds its private memory budget");

/* Reader returns zero only after all512 bytes have been verified. It may not
 * write storage or retain its output pointer. No callback runs during init. */
int kui_vmu_volatile_init(struct kui_vmu_volatile *,
    int (*read_base)(void *, uint16_t, uint8_t[512]), void *context);

/* Metadata-only pre-MDST gate: CPU/DMA little-endian header and first two
 * payload words. No I/O, protocol staging or cache commits occur. Exact A1,
 * host0 and MEMCARD only: INFO/ALLINFO, MEDIA, READ, four128-byte WRITE phases,
 * and SYNC phases1(read status) or4(RAM commit). Unknown/reset/LCD are refused.
 * A new uncached WRITE with sixteen occupied slots latches terminal FULL
 * before any phase ACK, including when called directly by handle(). */
int kui_vmu_volatile_admit(struct kui_vmu_volatile *, uint32_t header,
    uint32_t payload0, uint32_t payload1);

/* Exact request lengths and fields are rechecked. Successful C/D responses
 * are4 bytes; output capacity is checked before protocol/cache changes.
 * Unexpected protocol replies, base-reader errors and cache overflow are
 * terminal, with *response_bytes=0. Phase-ACK count includes identical retries;
 * ram_commits counts actual RAM copies (a repeated final SYNC does not copy).
 * Reads see only committed cache bytes or the immutable base image. */
int kui_vmu_volatile_handle(struct kui_vmu_volatile *,
    const uint8_t *request, size_t request_bytes,
    uint8_t *response, size_t response_capacity, size_t *response_bytes);
#endif
