/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_READONLY_OVERLAY_H
#define KUI_VMU_READONLY_OVERLAY_H
#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_READONLY_OVERLAY_FRAMES 32u
#define KUI_VMU_READONLY_OVERLAY_REPLY 524u

/* A snapshot of one real DMA packet, excluding one-word SDK padding. The
 * caller proves the pointer maps to this physical RX address, the capacity is
 * an owned SDK bank slot, and P2 accesses are coherent with the SDK consumer.
 * This generic helper cannot prove those properties from a pointer. */
struct kui_vmu_readonly_overlay_frame {
    uint32_t header, payload0, payload1;
    uint32_t rx_physical, rx_capacity;
    uint8_t *rx_bytes;
    /* Only filtered-transport A1 WRITE: caller-owned immutable128-byte
     * snapshot from the original validated pre-DMA packet. prepare() copies
     * it into private staging and retains no access to this pointer. */
    const uint8_t *write_payload;
};

typedef int (*kui_vmu_readonly_overlay_handler)(void *, const uint8_t *, size_t,
    uint8_t *, size_t, size_t *);
typedef int (*kui_vmu_readonly_overlay_admit)(void *, uint32_t, uint32_t, uint32_t);

enum kui_vmu_readonly_overlay_result {
    KUI_VMU_OVERLAY_OK = 0,
    KUI_VMU_OVERLAY_ARGUMENT = -1,
    KUI_VMU_OVERLAY_STATE = -2,
    KUI_VMU_OVERLAY_SPAN = -3,
    KUI_VMU_OVERLAY_OVERLAP = -4,
    KUI_VMU_OVERLAY_REQUEST = -5,
    KUI_VMU_OVERLAY_PARENT = -6,
    KUI_VMU_OVERLAY_CONFLICT = -7,
    KUI_VMU_OVERLAY_HANDLER = -8
};

enum kui_vmu_readonly_overlay_flag {
    KUI_VMU_OVERLAY_BAD_ARGUMENT = 1u,
    KUI_VMU_OVERLAY_BAD_STATE = 2u,
    KUI_VMU_OVERLAY_BAD_SPAN = 4u,
    KUI_VMU_OVERLAY_BAD_OVERLAP = 8u,
    KUI_VMU_OVERLAY_BAD_REQUEST = 16u,
    KUI_VMU_OVERLAY_BAD_PARENT_COUNT = 32u,
    KUI_VMU_OVERLAY_BAD_PARENT_REPLY = 64u,
    KUI_VMU_OVERLAY_PHYSICAL_CONFLICT = 128u,
    KUI_VMU_OVERLAY_BAD_HANDLER = 256u
};

struct kui_vmu_readonly_overlay_stats {
    uint32_t prepared, completed, advertisements, replies, backend_calls;
    uint32_t devinfo, allinfo, media, reads, failures, flags;
    uint32_t last_request, last_reply, last_rx;
};

/* Private owned state: allocate in the proved worker BSS, never its stack or
 * guest/source/RX memory. Callers may inspect admitted/pending and stats only.
 * The handler must write solely its supplied staging buffer and private state;
 * it must not access guest RX memory or publish responses itself. */
struct kui_vmu_readonly_overlay {
    struct kui_vmu_readonly_overlay_stats stats;
    kui_vmu_readonly_overlay_handler handler;
    kui_vmu_readonly_overlay_admit admit;
    void *context;
    uint32_t magic, admitted, pending;
    uint32_t frame_count, parent_index, target_count;
    struct kui_vmu_readonly_overlay_frame frames[KUI_VMU_READONLY_OVERLAY_FRAMES];
    uint16_t reply_bytes[KUI_VMU_READONLY_OVERLAY_FRAMES];
    uint32_t staged[KUI_VMU_READONLY_OVERLAY_FRAMES][KUI_VMU_READONLY_OVERLAY_REPLY / 4u];
};

_Static_assert(sizeof(struct kui_vmu_readonly_overlay) <= 20u * 1024u,
    "Read-only overlay requires bounded private BSS");

int kui_vmu_readonly_overlay_init(struct kui_vmu_readonly_overlay *,
    kui_vmu_readonly_overlay_handler, void *context);

/* Explicit filtered-transport variant. Non-NULL admit returns1 only for an
 * allowed metadata shape, performs NO I/O/protocol staging, and may latch a
 * terminal capacity fault. The caller MUST prove ALL A1 packets were removed
 * from the submitted hardware chain; hardware cannot update their RX slots.
 * Consequently complete() does not interpret those stale A1 RX bytes as a
 * physical reply. The real parent's A1-absent guard remains mandatory.
 * C/D are allowed only here, with exact MEMCARD shape; C payloads are copied
 * before DMA. The backend must establish its own commit contract: the
 * volatile backend acknowledges private RAM only; the prepared durable
 * backend requires its exact scoped SCI loan and completed journal/readback
 * verification before ACK. This overlay grants no storage ownership itself.
 * Existing init() keeps the read-only physical-timeout admission contract. */
int kui_vmu_readonly_overlay_init_backend(struct kui_vmu_readonly_overlay *,
    kui_vmu_readonly_overlay_handler, kui_vmu_readonly_overlay_admit, void *context);

/* Before MDST: snapshots must describe the entire real packet batch. The
 * physical source span includes ALL descriptors, payload and SDK padding up
 * to LAST. The protected span includes worker code/data/stack and its allocator
 * header. All spans are physical main RAM addresses; no aliases are accepted.
 * Exactly one main-A controller request is required in every admitted batch.
 * Default A1 accepts only DEVINFO, ALLINFO, MEMINFO and BLOCKREAD. The explicit
 * filtered backend also permits exact WRITE/SYNC subject to its admit gate.
 * Unknown commands and invalid shapes terminate before hardware starts.
 * Every full RX slot must be disjoint from source, protected state and all
 * other slots. This checks geometry, not SDK bank ownership.
 *
 * Call once per batch under the caller's proved SDK ownership. No response,
 * source descriptor, MMIO or SDK bookkeeping is modified. A failed call is
 * terminal; explicit fresh initialization is required for a new experiment. */
int kui_vmu_readonly_overlay_prepare(struct kui_vmu_readonly_overlay *,
    const struct kui_vmu_readonly_overlay_frame *, size_t frame_count,
    uint32_t source_physical, uint32_t source_bytes,
    uint32_t protected_physical, uint32_t protected_bytes);

/* After the unchanged physical DMA completes, before its SDK consumer: the
 * caller must independently enforce the exact bank, source, idle/software DMA,
 * lock, IMASK and cache-coherence proof. No MMIO/IRQ/cache operations occur here.
 * Validate the complete batch and stage all A1 replies before any guest store.
 * The real parent must be a valid controller response with A1 absent, and every
 * A1 response in default mode must be the hardware FFFFFFFF timeout; filtered
 * mode instead relies on caller-proved exclusion of every A1 wire packet.
 * Payloads publish
 * before their headers; the parent's advertised A1 bit is written last. Parent
 * payload and all unrelated responses remain unchanged. Any failure clears
 * admission and preserves every RX byte. Backend private state may have
 * advanced before a later staging failure; failure is terminal. */
int kui_vmu_readonly_overlay_complete(struct kui_vmu_readonly_overlay *);

/* Diagnostic completion for an explicitly filtered backend only. Retain the
 * same parent-response/A1-conflict admission before any external store. The
 * caller has proved that every A1 wire packet was excluded, so stale A1 RX
 * headers are replaced with the hardware FFFFFFFF no-response marker. Only
 * those four header bytes change: every payload, parent and unrelated RX byte
 * remains intact. No backend or storage callback runs and A1 is not advertised.
 * Full caller-owned DMA/bank/lock/IMASK/cache proofs remain mandatory.
 * completed increments, but virtual replies/advertisements/backend_calls do
 * not. Invalid/default physical-timeout mode calls fail without RX writes. */
int kui_vmu_readonly_overlay_complete_absent(struct kui_vmu_readonly_overlay *);
#endif
