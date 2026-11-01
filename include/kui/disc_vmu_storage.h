/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_VMU_STORAGE_H
#define KUI_DISC_VMU_STORAGE_H

#include "kui/vmu_sd_lease.h"
#include "sd_reader.h"
#include <stdbool.h>
#include <stdint.h>

/* The real-disc VMU owns an SD descriptor independently of any GD reader.
 * These operations belong to a separately linked SCI or SCIF adapter. They
 * never intercept a GD syscall, stop a GD request or borrow a GD reader lock.
 *
 * An acquire result of NOT_READY or UNSUPPORTED guarantees no register write
 * and no ownership. Every other failed acquisition must restore its own
 * partial work and/or latch faulted(). A successful acquisition is released
 * exactly once, even when the transaction or its descriptor checks fail.
 * healthy() describes the acquired port; faulted() remains meaningful after
 * release, including a final restoration failure. stream_active() is a local
 * ownership check: it must not start/stop hardware or wait for a stream.
 * All operations are bounded and callable with IMASK15 held. */
struct kui_disc_vmu_storage_ops {
    uint32_t (*current_sr)(void *context);
    bool (*stream_active)(void *context);
    enum kui_loader_sd_result (*acquire)(void *context);
    void (*release)(void *context);
    bool (*healthy)(void *context);
    bool (*faulted)(void *context);
};

struct kui_disc_vmu_storage_config {
    uint32_t transport; /* Exactly KUI_STORAGE_SCI or KUI_STORAGE_SCIF. */
    uint8_t cid[16];
    struct kui_loader_sd card;
    kui_vmu_sd_transaction transaction;
    void *transaction_context;
};

/* Caller-owned, permanent resident state. Zero the complete object before its
 * one bind; do not copy, rebind, edit or reset a bound/faulted instance. The
 * stage proves the exact transaction's worker entry/range and its context,
 * the physical CID/capacity, and the selected image/journal ownership before
 * binding. This component does not turn an arbitrary function pointer into
 * a proved worker or authorize arbitrary raw sectors.
 *
 * The callback receives only a scoped copy of card. It may issue the admitted
 * VMU protocol, update last_command/last_response or invalidate ready. It may
 * not change the bus, capacity, high_capacity or speed, retain the copy,
 * initialize a card, change interrupt state, or acquire/release the port.
 * No changed card descriptor is adopted back into the owner. */
struct kui_disc_vmu_storage {
    uintptr_t self;
    uint32_t state;
    struct kui_disc_vmu_storage_config config;
    struct kui_disc_vmu_storage_ops ops;
    void *platform_context;
    bool busy, fault;
    uint32_t acquired, completed, busy_refusals, faults;
    int last_transaction_result;
    uint8_t last_command, last_response;
};

/* Bind once while IMASK15 is held. Requires an already initialized, fast,
 * ready, nonempty SCI/SCIF card and a nonzero/non-FF admitted CID. Invalid,
 * overlapping or nonzero destination objects refuse without mutation. No
 * acquire, release, card initialization or wire traffic occurs here. */
bool kui_disc_vmu_storage_bind(struct kui_disc_vmu_storage *owner,
    const struct kui_disc_vmu_storage_config *config,
    const struct kui_disc_vmu_storage_ops *ops,void *platform_context);

/* Exact worker callback/context only. Invalid caller state refuses before
 * platform/storage work. BUSY means the transaction did not run and a later
 * request is safe. Any uncertainty after acquisition permanently faults the
 * owner; all later requests refuse without reacquisition. A zero callback
 * result is acknowledged only after successful port restoration. */
int kui_disc_vmu_storage_loan(struct kui_disc_vmu_storage *owner,
    kui_vmu_sd_transaction transaction,void *transaction_context);

#endif
