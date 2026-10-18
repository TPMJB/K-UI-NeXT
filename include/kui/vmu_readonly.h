/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_READONLY_H
#define KUI_VMU_READONLY_H

#include "kui/vmu_emulation.h"

#define KUI_VMU_READONLY_ADDRESS 0x01u
#define KUI_VMU_READONLY_MAX_RESPONSE_BYTES 524u
#define KUI_VMU_READONLY_STATE_BYTES_MAX 2048u

enum kui_vmu_readonly_result {
    KUI_VMU_READONLY_HANDLED = 1,
    KUI_VMU_READONLY_REFUSED = 0,
    KUI_VMU_READONLY_ARGUMENT = -1,
    KUI_VMU_READONLY_CAPACITY = -2,
    KUI_VMU_READONLY_INTERNAL = -3
};

/* A storage-only A1 with a synthetic blank default or a verified fixed-image
 * reader. Caller-owned and single caller; no direct MMIO/SD, allocation,
 * full-card mirror, writes or persistence. Initialize
 * in its final location: protocol callbacks refer back to this object. Do not
 * move/copy it after init or alter its exposed private protocol state. */
struct kui_vmu_readonly {
    kui_vmu_emulation_t protocol;
    uint32_t requests, reads, refused, commit_attempts;
    uint8_t ready, faulted;
    int (*reader)(void *,uint16_t,uint8_t[512]);
    void *reader_context;
};

_Static_assert(sizeof(struct kui_vmu_readonly)<=KUI_VMU_READONLY_STATE_BYTES_MAX,
    "Read-only blank-card state exceeds its owned memory budget");

/* Zero on success; reinitializes caller-owned memory without external I/O. */
int kui_vmu_readonly_init(struct kui_vmu_readonly *state);
/* Read-only fixed-image variant. Callback is resident/caller-owned and returns
 * zero only with all512 bytes verified. Any nonzero result is terminal; this
 * first bridge never invents a Maple retry response. No callback may write
 * storage or retain the output pointer. No callback runs during init. */
int kui_vmu_readonly_init_reader(struct kui_vmu_readonly *state,
    int (*read_block)(void *,uint16_t,uint8_t[512]),void *context);

/* CPU/DMA bytes, exact destination A1/source host0. Only DEVINFO, ALLINFO,
 * GETMINFO and BLOCKREAD are admitted, with exact packet lengths, MEMCARD
 * function and valid partition/phase/block fields. Everything else, including
 * RESET/KILL/LCD/WRITE/SYNC, is REFUSED before the protocol is entered.
 * REFUSED and negative results set *response_bytes=0. Capacity failures occur
 * before protocol/state changes; refusals only increment refused. The commit
 * callback is a fault sentinel and can never acknowledge a write.
 * Request, response, response_bytes and state must not overlap. The caller
 * owns/validates each buffer; this function installs no retail bridge. */
int kui_vmu_readonly_handle(struct kui_vmu_readonly *state,
    const uint8_t *request, size_t request_bytes,
    uint8_t *response, size_t response_capacity, size_t *response_bytes);

#endif
