/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_VMU_SD_H
#define KUI_ARMADA_CE_VMU_SD_H
#include "kui/vmu_durable.h"
#include "kui/vmu_deferred_io.h"
#include "kui/armada_ce_vmu_sd_pio.h"

#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME || defined(KUI_ARMADA_CE_VMU_SD_TEST)
/* Fixed source-owned reader entrypoints, bound once before requests. begin
 * returns0 only after excluding DATA/audio, stopping CMD18 and successfully
 * releasing the old reader port;1 is a pre-acquisition refusal with no wire
 * traffic. Every successful begin receives exactlyone end, even on failure.
 * end(false), or any failed end, quarantines the reader permanently. The
 * saved-SR guard has the same short, non-I/O contract as the deferred queue;
 * worker_ready proves the ordinary worker/private stack, MD1/BL0/IMASK0 and
 * the absence of a held Maple fence. It must not accept an IRQ context. */
struct kui_armada_ce_vmu_sd_ops {
    int (*begin)(struct kui_loader_sd *);
    int (*end)(bool healthy);
    struct kui_vmu_deferred_guard guard;
};
/* Static final-location private P2 state. Zero once, initialize once, never
 * move/reinitialize after an attempt. No guest pointer, loan/card pointer or
 * callback buffer survives a transaction. The handoff is a sealed fresh
 * pre-NK checkpoint, independently matched to the immutable card profile;
 * this endpoint never admits a guessed LBA or recovers a dirty journal. */
struct kui_armada_ce_vmu_sd {
    const struct kui_armada_ce_vmu_sd *self;
    struct kui_vmu_durable_handoff handoff;
    struct kui_vmu_store store;
    struct kui_armada_ce_vmu_sd_pio pio;
    struct kui_armada_ce_vmu_sd_ops ops;
    _Alignas(32) uint8_t bounce[512];
    uint32_t magic,started,busy,faulted,bound,operation,block,pending_crc;
    uint32_t reads,commits,bus_busy,failures,identity_checks,sector_reads,sector_writes;
    uint32_t last_lba;
    int last_result,last_sd_result,last_store_result,last_loan_result;
};
_Static_assert(sizeof(struct kui_armada_ce_vmu_sd)<=4096u,"CE SD endpoint RAM bound");

/* No I/O. Callbacks/context, state and handoff must have disjoint lifetimes
 * and remain valid for the complete installed runtime. The copied handoff's
 * runtime block CRCs are advanced only after successful physical readback,
 * identity/health checks and BOTH writer and reader release success. */
int kui_armada_ce_vmu_sd_init(struct kui_armada_ce_vmu_sd *,
    const struct kui_vmu_durable_handoff *,const struct kui_armada_ce_vmu_sd_ops *);
/* Bind as the deferred mailbox's real provider. Both execute in the verified
 * unmasked worker. BUSY is possible only before reader acquisition. Acquired
 * failures latch forever and never return BUSY. COMMIT OK means the existing
 * redo journal/base barriers, exact512-byte readback, actual CID/geometry and
 * successful hardware/reader restoration all completed. No ACK is generated
 * here. Reads preserve the output unless the complete lease succeeds. */
int kui_armada_ce_vmu_sd_read(void *,uint16_t,uint8_t [512]);
int kui_armada_ce_vmu_sd_commit(void *,uint16_t,const uint8_t [512]);
#endif
#endif
