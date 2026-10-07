/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_TIMING_H
#define KUI_CDDA_TIMING_H
#include <stdbool.h>
#include <stdint.h>

/* One checked AICA position observation, bracketed by the owned timer. The
 * played count is cumulative hardware frames, never the source read cursor. */
struct kui_cdda_timing_snapshot {
    uint32_t before_tick, after_tick, played_frames;
};
struct kui_cdda_timing_result {
    uint32_t played_frames, lower_ticks, upper_ticks;
    uint32_t start_read_ticks, end_read_ticks;
};

/* The caller guarantees less than one timer period between endpoints and an
 * unwrapped, non-overflowing frame count maintained by frequent observations.
 * max_elapsed_ticks must be below half the timer period. A single numerical
 * timer wrap is supported. Output is unchanged on failure. No frequency is
 * inferred: these raw bounds measure AICA relative to the owned timer. */
bool kui_cdda_timing_measure(const struct kui_cdda_timing_snapshot *start,
                             const struct kui_cdda_timing_snapshot *end,
                             uint32_t max_read_ticks,
                             uint32_t max_elapsed_ticks,
                             struct kui_cdda_timing_result *result);
#endif
