/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_VMU_READONLY_H
#define KUI_ARMADA_CE_VMU_READONLY_H
#include "kui/vmu_readonly.h"

#ifndef KUI_ARMADA_CE_VMU_READONLY_HOST_TEST
#define KUI_ARMADA_CE_VMU_READONLY_HOST_TEST 0
#endif

#if KUI_VMU_READONLY_ADDRESS != 2
#error "The Armada CE read-only experiment requires the virtual A2 address"
#endif

/* An allocation-free protocol adapter, not a transport/SD hook. Native
 * readonly packet handling is unchanged. The exact CE owner adds CMD13
 * after a completed read: its one-read-phase path uses phase0; phase1 is
 * the already supported native read-finalization shape. Neither can stage
 * or acknowledge a storage write. Capacity/ownership/coherence of the real
 * guest RX buffer must be proved by the separate CE transport adapter.
 *
 * Initialize in its final location. Never move/copy an initialized object;
 * the backend callbacks refer to its embedded state. Private fields are
 * exposed only for static placement and diagnostics, never caller editing. */
struct kui_armada_ce_vmu_readonly {
    const struct kui_armada_ce_vmu_readonly *self;
    struct kui_vmu_readonly backend;
    uint32_t magic,requests,reads,read_finalizations,refused;
    uint16_t read_block;
    uint8_t read_valid,faulted;
};

_Static_assert(sizeof(struct kui_armada_ce_vmu_readonly)<=2048u,
    "CE read-only protocol state exceeds its private memory budget");

int kui_armada_ce_vmu_readonly_init(struct kui_armada_ce_vmu_readonly *);
#if KUI_ARMADA_CE_VMU_READONLY_HOST_TEST
/* Host fault model only; absent from the production experiment. Callback
 * has the native readonly contract: all512 bytes verified on success, no
 * writes or retained output pointer, no I/O during init/metadata/finalization.
 * An I/O failure permanently faults this instance. */
int kui_armada_ce_vmu_readonly_init_reader(struct kui_armada_ce_vmu_readonly *,
    int (*read_block)(void *,uint16_t,uint8_t[512]),void *context);
#endif

/* Native readonly return codes and CPU/DMA packet representation. Exactly
 * A2 destination02 and host00. DEVINFO/ALLINFO/MEMINFO/BLOCKREAD retain their
 * strict native shapes. CMD13 additionally requires partition0, phase0/1,
 * and the last successful read's block. Repeated matching finalizations are
 * idempotent. A failed/malformed addressed read or rejected mutating request
 * clears that proof; a mismatched finalization also clears it. No reset,
 * kill, LCD, write, write-phase4 sync or unrelated command is dispatched to
 * the unrestricted encoder. Output bytes remain private until transport
 * publication. Request/response/length/state must be disjoint; invalid spans
 * and overlap fail before any write, including to response_bytes. */
int kui_armada_ce_vmu_readonly_handle(struct kui_armada_ce_vmu_readonly *,
    const uint8_t *request,size_t request_bytes,
    uint8_t *response,size_t response_capacity,size_t *response_bytes);
#endif
