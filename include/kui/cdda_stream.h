/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_STREAM_H
#define KUI_CDDA_STREAM_H

#include <stdbool.h>
#include <stdint.h>

/* Track-relative stereo frames, with an exclusive end. Progress is the number
 * of frames actually played since this segment started, never prefetched PCM.
 * Keep played when pausing and resuming the same segment. */
struct kui_cdda_stream {
    uint32_t first_frame;
    uint32_t end_frame;
    bool repeat;
    bool initialized;
};

/* Invalid arguments leave the stream unchanged. */
bool kui_cdda_stream_init(struct kui_cdda_stream *, uint32_t first_frame,
                         uint32_t end_frame, bool repeat);

/* Map played frames into a segment without adding first_frame + played.
 * A nonrepeating segment accepts played == span and reports frame == end_frame
 * (EOF), with loops == 0. Repeating segments always report a frame before end.
 * Both output pointers are required and must be distinct. Failure leaves both
 * outputs unchanged. played is bounded to UINT32_MAX; callers must not wrap a
 * cumulative playback count. */
bool kui_cdda_stream_cursor(uint32_t first_frame, uint32_t end_frame,
                            bool repeat, uint32_t played,
                            uint32_t *frame, uint32_t *loops);
bool kui_cdda_stream_position(const struct kui_cdda_stream *, uint32_t played,
                              uint32_t *frame, uint32_t *loops);

/* Elapsed time from a monotonic wrapping 32-bit hardware counter. Poll more
 * often than a complete counter period: missed full wraps cannot be detected.
 * wraps counts observed counter boundaries, not seconds overflow. Fields are
 * read-only for callers after initialization. No 64-bit math or floating point
 * is required, even when elapsed ticks exceed UINT32_MAX. */
struct kui_cdda_stream_clock {
    uint32_t last_tick;
    uint32_t seconds;
    uint32_t subticks;
    uint32_t wraps;
    uint32_t hz;
    bool initialized;
};

/* hz must be nonzero. Both functions leave state unchanged on failure;
 * update rejects overflow of seconds or wraps. */
bool kui_cdda_stream_clock_init(struct kui_cdda_stream_clock *, uint32_t now,
                               uint32_t hz);
bool kui_cdda_stream_clock_update(struct kui_cdda_stream_clock *, uint32_t now);

#endif
