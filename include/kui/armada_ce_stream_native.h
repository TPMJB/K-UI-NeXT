/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_STREAM_NATIVE_H
#define KUI_ARMADA_CE_STREAM_NATIVE_H
#include "kui/armada_ce_live_sound.h"
#include <stdbool.h>
#include <stdint.h>

#define KUI_ARMADA_CE_STREAM_VENEER_BYTES 72u
#define KUI_ARMADA_CE_STREAM_VENEER_PUBLISH_BYTES 96u
#define KUI_ARMADA_CE_STREAM_PRIORITY 2u /* Above kernel normal3; below priority1 sound service. */

/* Testable boundary. All read32/write32 addresses are canonical P1 main RAM;
 * publish_code may OCBP only the newly allocated caller-owned code lines.
 * Native CE calls use the existing eight-word Microsoft-SH ABI bridge.
 * NULL ops selects the production implementation on the SH target. */
struct kui_armada_ce_stream_native_ops {
    void *context;
    uint32_t (*call)(void *,uint32_t,const uint32_t [8]);
    bool (*read32)(void *,uint32_t,uint32_t *);
    bool (*write32)(void *,uint32_t,uint32_t);
    bool (*publish_code)(void *,uint32_t,uint32_t);
    uint32_t (*ttb)(void *);
    uint32_t (*ccr)(void *);
    uint32_t (*sr)(void *);
};
enum kui_armada_ce_stream_native_status {
    KUI_ARMADA_CE_STREAM_NATIVE_IDLE,
    KUI_ARMADA_CE_STREAM_NATIVE_RUNNING,
    KUI_ARMADA_CE_STREAM_NATIVE_COMPLETE,
    KUI_ARMADA_CE_STREAM_NATIVE_ARGUMENT=16,
    KUI_ARMADA_CE_STREAM_NATIVE_CONTEXT,
    KUI_ARMADA_CE_STREAM_NATIVE_ALLOCATION,
    KUI_ARMADA_CE_STREAM_NATIVE_MAPPING,
    KUI_ARMADA_CE_STREAM_NATIVE_PUBLICATION,
    KUI_ARMADA_CE_STREAM_NATIVE_CREATE,
    KUI_ARMADA_CE_STREAM_NATIVE_PRIORITY,
    KUI_ARMADA_CE_STREAM_NATIVE_RESUME,
    KUI_ARMADA_CE_STREAM_NATIVE_WAIT,
    KUI_ARMADA_CE_STREAM_NATIVE_CLOSE,
    KUI_ARMADA_CE_STREAM_NATIVE_FREE,
    KUI_ARMADA_CE_STREAM_NATIVE_LOCK
};
/* Caller provides static storage. A nonzero thread or allocation is a retained
 * native lease, even after a refusal. No COM reference is held here; only the
 * worker acquires/releases its DirectSound references. Serialize start/reap.
 * Worker completion is cooperative: this module never terminates a thread. */
struct kui_armada_ce_stream_native {
    struct kui_armada_ce_live_stream_api api;
    uint32_t status,last_result,thread,thread_id;
    uint32_t allocation,allocation_owner,allocation_p1,page_word;
    uint32_t worker,argument,resumed,consumer_cs;
};
bool kui_armada_ce_stream_native_start(struct kui_armada_ce_stream_native *,
    const struct kui_armada_ce_live_stream_api *,uint32_t worker_p1,
    uint32_t argument_p1,const struct kui_armada_ce_stream_native_ops *);
/* Wait0 must report WAIT_OBJECT_0 before a created thread's code may be freed.
 * Timeout/failure retains both leases; callers may retry in the owner context. */
bool kui_armada_ce_stream_native_reap(struct kui_armada_ce_stream_native *,
    const struct kui_armada_ce_stream_native_ops *);
bool kui_armada_ce_stream_native_ticks(struct kui_armada_ce_stream_native *,
    const struct kui_armada_ce_stream_native_ops *,uint32_t *);
bool kui_armada_ce_stream_native_sleep(struct kui_armada_ce_stream_native *,
    const struct kui_armada_ce_stream_native_ops *,uint32_t milliseconds);
/* Only108: excludes native stereo host consumption while the sole worker
 * checks its own buffer+116 and calls Lock(flags0), copies RAM, and Unlock.
 * Play/Stop/SetPosition/Release, SD reads, and waits MUST be outside this CS.
 * This does not assert a native88->108 order or serialize other controllers.
 * device must be the worker's retained, revalidated native device reference. */
bool kui_armada_ce_stream_native_write_begin(struct kui_armada_ce_stream_native *,
    const struct kui_armada_ce_stream_native_ops *,uint32_t device);
bool kui_armada_ce_stream_native_write_end(struct kui_armada_ce_stream_native *,
    const struct kui_armada_ce_stream_native_ops *);
#endif
