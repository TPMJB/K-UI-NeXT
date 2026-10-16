/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_MAPLE_DMA_H
#define KUI_VMU_MAPLE_DMA_H

#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_MAPLE_DMA_MAX_FRAMES 32u
#define KUI_VMU_MAPLE_DMA_REPLY_BYTES 1024u
#define KUI_VMU_MAPLE_DMA_LAST UINT32_C(0x80000000)
#define KUI_VMU_MAPLE_DMA_RAM_BASE UINT32_C(0x0c000000)
#define KUI_VMU_MAPLE_DMA_RAM_END UINT32_C(0x0d000000)

/* A caller-owned region, not an arbitrary guest-address-to-pointer cast.
 * Use uncached P2 aliases on the SH4. Every accepted response gets a complete
 * 1024-byte slot, since physical Maple devices control their reply length. */
struct kui_vmu_maple_dma_region {
    uint32_t physical_base;
    uint8_t *bytes;
    size_t size;
};

struct kui_vmu_maple_dma_frame {
    const uint8_t *request;
    uint8_t *response;
    size_t request_bytes;
    uint8_t is_virtual;
    uint8_t is_parent;
};

struct kui_vmu_maple_dma_plan {
    struct kui_vmu_maple_dma_frame frames[KUI_VMU_MAPLE_DMA_MAX_FRAMES];
    size_t frame_count;
    size_t virtual_count;
    size_t hardware_count;
    size_t source_bytes;
    size_t hardware_bytes;
    uint8_t address;
    uint8_t state; /* Private: zero/failed, prepared, completing, completed. */
};

enum kui_vmu_maple_dma_result {
    KUI_VMU_MAPLE_DMA_OK = 0,
    KUI_VMU_MAPLE_DMA_INVALID = -1,
    KUI_VMU_MAPLE_DMA_UNSUPPORTED = -2,
    KUI_VMU_MAPLE_DMA_CAPACITY = -3,
    KUI_VMU_MAPLE_DMA_HANDLER = -4,
    KUI_VMU_MAPLE_DMA_STATE = -5
};

typedef int (*kui_vmu_maple_dma_handler_fn)(void *context,
    const uint8_t *request, size_t request_bytes,
    uint8_t *response, size_t response_capacity, size_t *response_bytes);

/* Parse and validate the entire bounded source chain before producing any
 * hardware chain or touching responses. Only normal, unicast descriptors are
 * accepted. The control low byte is the payload word count; the next two words
 * are physical RX address and Maple header bytes {cmd,dst,src,payload_words}.
 * Other pattern bits (gun/reset/NOP/etc.) are explicitly unsupported.
 *
 * The caller keeps source/response regions immutable and excludes other Maple
 * users through complete(). hardware_chain must be 32-byte aligned and must
 * not alias source, plan, or response regions. Response regions may not overlap.
 * Virtual commands are removed; physical packets and RX addresses are preserved.
 * No MMIO, cache operation, IRQ operation, storage callback, or allocation occurs.
 * A plan with hardware_count==0 needs software completion, not an empty DMA. */
int kui_vmu_maple_dma_prepare(struct kui_vmu_maple_dma_plan *plan,
    uint8_t virtual_address, const uint8_t *source_chain, size_t source_capacity,
    uint8_t *hardware_chain, size_t hardware_capacity,
    const struct kui_vmu_maple_dma_region *regions, size_t region_count);

/* Call once, only after the filtered physical DMA has completed (or directly
 * for an all-virtual plan), and before exposing completion to the original
 * caller. Dispatches virtual requests in original order and adds the selected
 * subunit bit to successful physical parent replies. It never fabricates a
 * controller or changes controller payload bytes. A failed handler makes the
 * plan terminal; retrying could repeat an already committed save transaction.
 * A handled 4-byte all-FF response is the hardware no-response marker (KILL).
 * Storage callbacks may block: this API is NOT an interrupt-safe scheduler. */
int kui_vmu_maple_dma_complete(struct kui_vmu_maple_dma_plan *plan,
    kui_vmu_maple_dma_handler_fn handler, void *context);

#endif
