/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_RUNTIME_H
#define KUI_VMU_NATIVE_RUNTIME_H
#include "kui/vmu_native_discovery.h"

#define KUI_VMU_NATIVE_RUNTIME_VERSION 1u
#define KUI_VMU_NATIVE_TX_BANK_BYTES 0x3000u
#define KUI_VMU_NATIVE_RX_SLOTS 48u
#define KUI_VMU_NATIVE_RX_SLOT_BYTES 1024u
enum kui_vmu_native_rx_layout {
    KUI_VMU_NATIVE_RX_UNIFORM48=0,
    KUI_VMU_NATIVE_RX_EXPLICIT48=1
};

enum kui_vmu_native_runtime_phase {
    KUI_VMU_NATIVE_BEFORE_SUBMIT=0,
    /* Completion hook before tail-calling the original SDK consumer.
     * The submission path has already advanced its bank selectors. */
    KUI_VMU_NATIVE_BEFORE_CONSUMER=1
};
enum kui_vmu_native_runtime_result {
    KUI_VMU_NATIVE_RUNTIME_VALID=0,
    KUI_VMU_NATIVE_RUNTIME_INVALID=-1,
    KUI_VMU_NATIVE_RUNTIME_READ=-2,
    KUI_VMU_NATIVE_RUNTIME_POINTER=-3,
    KUI_VMU_NATIVE_RUNTIME_STATE=-4,
    KUI_VMU_NATIVE_RUNTIME_CONFLICT=-5,
    KUI_VMU_NATIVE_RUNTIME_CONTEXT=-6
};

/* These addresses require an independently validated allocator/lifetime
 * adapter. Passing values here does not prove that the game owns them.
 * The lease excludes its allocator header, which is protected separately.
 * No fixed high-RAM lease or title-specific address is assumed. */
struct kui_vmu_native_runtime_config {
    uint32_t version,bytes;
    uint32_t lease_address,lease_bytes,lease_header_bytes,required_lease_bytes;
    uint32_t gun_flag,timer1_callback,timer2_callback;
    /* Explicit mode supports the SDK variable initializer: fc8 can point to
     * a pointer array rather than contiguous RX storage. An initializer
     * adapter must supply its exact expected fc8 and all48 table entries;
     * unused entries are zero. Every nonzero buffer must still be P2. */
    uint32_t rx_layout,rx_base,rx_slots[KUI_VMU_NATIVE_RX_SLOTS];
};
struct kui_vmu_native_runtime_boundary {
    uint32_t phase,worker_sr,inherited_sr,inherited_pr;
    uint32_t sdk_source_physical,dma_source_physical,dma_bytes;
};
struct kui_vmu_native_runtime_snapshot {
    uint32_t manager,lock,tx_base,rx_base,bank;
    uint32_t sdk_source_physical,dma_source_physical;
    uint32_t lease_guard_physical,lease_guard_bytes;
    uint32_t rx_slots_nonzero;
};
/* Read exactly one aligned word or one byte. False means inaccessible or
 * otherwise unproved. The validator stops before following a failed read.
 * Only the four named Maple MMIO words and validated RAM spans are read. */
typedef bool (*kui_vmu_native_runtime_read)(void *context,uint32_t address,
    unsigned width,uint32_t *value);

/* Timer setters may install an application callback after initial binding.
 * Permit null or a complete aligned instruction in the already verified
 * executable, never arbitrary RAM, loader/lease memory or an MMIO address.
 * This does not call the callback or authorize its opaque argument. Callers
 * still establish timer-cell provenance and run every check under IMASK15. */
bool kui_vmu_native_timer_callback_owned(const struct kui_vmu_native_discovery *,uint32_t);

/* A read-only boundary check, not permission to patch, submit DMA or write
 * saves. Discovery must first be bound to its immutable executable. The
 * caller must establish lease ownership, timer-cell provenance and hook
 * lifetime independently on this launch, and execute this under IMASK15.
 * Existing discovery requirement bits remain unchanged. Snapshot is written
 * only on success and must occupy caller-owned storage. No state is cached;
 * repeat at both boundaries and retain the existing descriptor/payload guards. */
enum kui_vmu_native_runtime_result kui_vmu_native_runtime_check(
    const struct kui_vmu_native_discovery *driver,
    const struct kui_vmu_native_runtime_config *config,
    const struct kui_vmu_native_runtime_boundary *boundary,
    kui_vmu_native_runtime_read read,void *context,
    struct kui_vmu_native_runtime_snapshot *snapshot);
#endif
