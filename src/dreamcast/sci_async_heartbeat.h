/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SCI_ASYNC_HEARTBEAT_H
#define KUI_SCI_ASYNC_HEARTBEAT_H
#include <stdbool.h>
#include <stdint.h>

struct kui_sci_async_heartbeat_result {
    bool installed, restored, ownership_lost;
    uint32_t total_ticks, dma_ticks;
    /* Largest interval between observed TMU0 IRQ deliveries. The scheduler
     * controls its own interval: this is not worst-case interrupt latency. */
    uint64_t max_gap_us;
};

/* Runtime diagnostic observer of the existing KOS TMU0 IRQ. It never changes
 * timer registers, priorities, the primary callback, or scheduler context.
 * The sampler executes in IRQ context and must be read-only, bounded, and safe
 * there. It must return true only while our DMA is actually incomplete, not
 * merely while a request or its completion interrupt is pending. Its context
 * must outlive end(). This singleton requires serialized foreground callers.
 * A missing existing handler, NULL sampler, or occupied hook returns false.
 * A second start must not be paired with end: it did not acquire a session. */
bool kui_sci_async_heartbeat_start(bool (*dma_active)(void *), void *context);

/* Stop observing and restore the exact prior handler/data if still owned.
 * A foreign replacement is never overwritten. If restoration is unconfirmed,
 * this observer cannot be reused until restart; a retained chained callback
 * can still forward safely without touching the caller's sampler/context.
 * A zero dma_ticks count leaves interrupt-during-DMA progress unconfirmed.
 * The result is a snapshot, so the caller need not keep it alive afterward. */
void kui_sci_async_heartbeat_end(struct kui_sci_async_heartbeat_result *out);
#endif
