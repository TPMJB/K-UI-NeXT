/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_DEFERRED_IO_H
#define KUI_VMU_DEFERRED_IO_H
#include "kui/vmu_emulation.h"
#include <stdbool.h>

/* One CPU, one worker, one protocol caller. All shared storage must have one
 * coherent alias (P2 on SH). Every metadata transition and protocol callback
 * is serialized by a SHORT saved-SR guard. enter(false) changes no state;
 * leave MUST restore the saved state exactly. Neither may wait or do I/O.
 * worker_ready must verify the actual thread/owner/IMASK0 execution context.
 * These callbacks are trusted platform boundaries, not ownership detection. */
struct kui_vmu_deferred_guard {
    void *context;
    bool (*enter)(void *,uint32_t *saved);
    void (*leave)(void *,uint32_t saved);
    bool (*worker_ready)(void *);
};

enum kui_vmu_deferred_state {
    KUI_VMU_DEFERRED_IDLE,
    KUI_VMU_DEFERRED_PENDING,
    KUI_VMU_DEFERRED_RUNNING,
    KUI_VMU_DEFERRED_DONE,
    KUI_VMU_DEFERRED_FAULT
};
enum kui_vmu_deferred_operation {
    KUI_VMU_DEFERRED_READ=1,
    KUI_VMU_DEFERRED_COMMIT=2
};

/* Zero once before init; static final-location allocation, never copy,
 * reinitialize, cancel or free a live instance. The provider is bound for
 * this instance's entire lifetime and owns its device/session arbitration.
 * No guest pointer or callback-supplied buffer survives a callback. */
struct kui_vmu_deferred_io {
    const struct kui_vmu_deferred_io *self;
    struct kui_vmu_emulation_io provider;
    struct kui_vmu_deferred_guard guard;
    uint32_t magic,state,ticket,operation,block;
    uint32_t submissions,completions,provider_busy,failures;
    uint8_t bytes[512];
};
int kui_vmu_deferred_io_init(struct kui_vmu_deferred_io *,
    const struct kui_vmu_emulation_io *,const struct kui_vmu_deferred_guard *);

/* Bind these as the protocol engine's io callbacks, with context=this object.
 * A new job returns BUSY. Only an identical commit may consume its successful
 * completion; a different request remains BUSY and cannot overwrite it.
 * The protocol must retry a BUSY read of the same block before another read;
 * BUSY commits are already immutable in kui_vmu_emulation. A read copies its
 * complete verified result only on success. A provider/storage ERROR latches
 * FAULT; invalid arguments or an unavailable guard return ERROR without
 * changing queue state. Post-I/O guard failure quarantines RUNNING. */
int kui_vmu_deferred_io_read(void *,uint16_t,uint8_t [512]);
int kui_vmu_deferred_io_commit(void *,uint16_t,const uint8_t [512]);

/* Call only from the verified external worker, NEVER under the Maple fence
 * or its IMASK guard. Claims under the short guard, releases it, executes ONE
 * provider transaction, then publishes its result under the guard. Returns1
 * for a completed job,0 for idle/inflight/pre-acquisition BUSY,-1 for a fault,
 * -2 for an invalid worker context/guard (no provider invocation).
 *
 * Provider BUSY promises no acquisition/I/O. COMMIT OK promises full durable
 * journal/base barriers, exact readback, identity/health AND successful bus
 * restoration/release. An uncertain/acquired error MUST return ERROR, never
 * BUSY. Provider calls may be interrupted by Maple, but cannot retain bytes,
 * change this object or call its worker recursively. No platform SD provider
 * is supplied here: the native IMASK15 lease is not a valid CE worker bridge. */
int kui_vmu_deferred_io_work(struct kui_vmu_deferred_io *);
/* Owner's irreversible fault latch, protected by the same short guard. Does
 * not cancel or free an executing provider, acknowledge it, clear a ticket or
 * admit another operation. An already running provider must still release
 * its device; its later completion is rejected. There is no reset API. */
int kui_vmu_deferred_io_poison(struct kui_vmu_deferred_io *);
#endif
