/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_VMU_TRANSPORT_H
#define KUI_DISC_VMU_TRANSPORT_H

#include "kui/disc_vmu_storage.h"

/* Independently linked serial save service. Its bus is fixed at link time by
 * KUI_RETAIL_TRANSPORT=0 (SCIF) or 1 (SCI); no GD hook, reader lock, image map,
 * filesystem, game timer, kernel service or launcher callback is retained.
 * The existing native serial backends preserve the borrowed register state.
 *
 * Keep this complete object at its final permanent address, initially zero.
 * The serial backend is a singleton: the module owns one instance and never
 * permits unbracketed accesses to that backend after binding. */
struct kui_disc_vmu_transport {
    struct kui_disc_vmu_storage storage;
};

/* Bind once while IMASK15 is held. The stage has initialized the physical SD
 * card, enabled CRC, ended every stream, verified its CID/capacity and admitted
 * the worker's exact durable image/journal map. prepared may belong to the
 * temporary stage: only protocol values survive; every bus callback is rebound
 * to this independently linked module. This function does no wire traffic,
 * register writes, initialization, acquisition or release.
 *
 * transaction and transaction_context must be the already proved relocated
 * worker entry and its permanent durable context. Neither is inferred from the
 * first caller. An invalid or already-bound destination is left unchanged. */
bool kui_disc_vmu_transport_bind(struct kui_disc_vmu_transport *transport,
    uint32_t storage_transport,const uint8_t cid[16],
    const struct kui_loader_sd *prepared,kui_vmu_sd_transaction transaction,
    void *transaction_context);

/* Same private-stack/IMASK15 contract as kui_disc_vmu_storage_loan. A borrowed
 * port is released exactly once, including callback failure. A restoration or
 * protocol uncertainty faults the session permanently. BUSY does not execute
 * the worker transaction. No GD request is inspected, stopped or resumed. */
int kui_disc_vmu_transport_loan(struct kui_disc_vmu_transport *transport,
    kui_vmu_sd_transaction transaction,void *transaction_context);

#endif
