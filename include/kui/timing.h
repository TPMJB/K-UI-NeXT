/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TIMING_H
#define KUI_TIMING_H
#include <stdbool.h>
#include <stdint.h>

enum kui_timing_phase { KUI_TIME_SETUP, KUI_TIME_RESUME, KUI_TIME_CAPTURE,
    KUI_TIME_VERIFY, KUI_TIME_FINISH, KUI_TIME_PHASES };
enum kui_timing_bucket { KUI_TIME_DISC, KUI_TIME_EDC, KUI_TIME_WRITE,
    KUI_TIME_READ, KUI_TIME_SHA256, KUI_TIME_CRC32, KUI_TIME_CHECKPOINT,
    KUI_TIME_BUCKETS };
struct kui_timing_sample { uint64_t us, bytes, calls; };
struct kui_timing {
    uint64_t (*clock_us)(void *);
    void *ctx;
    uint64_t mark, elapsed[KUI_TIME_PHASES];
    struct kui_timing_sample samples[KUI_TIME_PHASES][KUI_TIME_BUCKETS];
    enum kui_timing_phase phase;
    bool active;
};
/* Single-worker wall-time accounting. Intervals must not overlap; checkpoint
 * timing includes its own hashing/sync/metadata rather than counting twice. */
void kui_timing_start(struct kui_timing *, uint64_t (*clock_us)(void *), void *);
void kui_timing_phase(struct kui_timing *, enum kui_timing_phase);
uint64_t kui_timing_begin(struct kui_timing *);
void kui_timing_end(struct kui_timing *, enum kui_timing_bucket, uint64_t start, uint64_t bytes);
void kui_timing_finish(struct kui_timing *);
#endif
