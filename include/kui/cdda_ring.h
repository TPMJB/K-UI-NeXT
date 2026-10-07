/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_RING_H
#define KUI_CDDA_RING_H
#include <stdbool.h>
#include <stdint.h>

#define KUI_CDDA_RING_FRAMES 16384u
#define KUI_CDDA_HALF_FRAMES 8192u
#define KUI_CDDA_CLOCK_HZ 12500000u
#define KUI_CDDA_HALF_TICKS 2321995u
#define KUI_CDDA_MARGIN_TICKS 100000u /* 8 ms before an active-half boundary. */
enum kui_cdda_ring_result { KUI_CDDA_RING_OK, KUI_CDDA_RING_ARGUMENT,
    KUI_CDDA_RING_DEADLINE, KUI_CDDA_RING_NOT_READY };
struct kui_cdda_ring {
    uint32_t last_tick, last_position, played, max_service_ticks;
    uint32_t min_margin_ticks;
    bool initialized, ready[2];
};
/* Positions refer to samples actually played, never the SD prefetch cursor.
 * Require a service observation at least once per half. This conservative
 * limit makes missing a complete ring impossible without a deadline error.
 * A monotonic wrapping 12.5 MHz clock is explicitly owned by the harness. */
enum kui_cdda_ring_result kui_cdda_ring_init(struct kui_cdda_ring *, uint32_t now,
                                           uint32_t position);
enum kui_cdda_ring_result kui_cdda_ring_observe(struct kui_cdda_ring *, uint32_t now,
                                              uint32_t position);
enum kui_cdda_ring_result kui_cdda_ring_can_fill(struct kui_cdda_ring *, unsigned half);
enum kui_cdda_ring_result kui_cdda_ring_commit(struct kui_cdda_ring *, unsigned half);
#endif
