/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_BENCH_H
#define KUI_BENCH_H
#include "kui/capture.h"
#include "kui/options.h"

/* Isolated component benchmarks.
 *
 * Why this exists: a capture entangles four costs (optical read, SD write,
 * SHA-256, CRC32) in one long run on one disc at one radial position, so a
 * change to any one of them cannot be measured cleanly. These benches time
 * each component alone against a RAM buffer, in seconds rather than minutes,
 * so you can build a model of the ceilings first and spend a real capture
 * only to confirm the model.
 *
 * Every result is one log line of the same shape:
 *   BENCH <name> <detail> bytes=N us=N kib_s=N.N
 * Nothing overlaps: each line is one component and nothing else. */

/* How CPU time split over an interval, from the scheduler's per-thread
 * accounting (milliseconds; wall is microseconds). Filled by the platform; the
 * bench takes two and logs the difference. The point of it: every rate in
 * this file is wall-clock, and on a single-CPU console any other thread that
 * runs during a measurement silently lowers it. ui_ms says how much. */
struct kui_cpu_census { uint64_t wall_us, worker_ms, ui_ms, total_ms; };
void kui_cpu_census_log(kui_log_fn log, const char *what, const char *detail,
                        const struct kui_cpu_census *from, const struct kui_cpu_census *to);

/* What a probe read observed about the firmware while it ran. Poll calls are
 * bucketed by how long each call took: short calls are the CPU spinning on a
 * busy drive, long ones are the PIO transfer itself, so the split says how
 * much of an optical read is work and how much is waiting. */
#define KUI_PROBE_BUCKETS 5u
static inline unsigned kui_probe_bucket(uint64_t us) {   /* <25 <100 <400 <1600 more */
    return us < 25 ? 0u : us < 100 ? 1u : us < 400 ? 2u : us < 1600 ? 3u : 4u;
}
struct kui_probe_stats {
    uint64_t cmd_us, last_us;               /* read command time, total and last call */
    uint64_t submit_us, pause_us, polls, pauses;
    uint64_t poll_n[KUI_PROBE_BUCKETS], poll_us[KUI_PROBE_BUCKETS];
};

struct kui_bench_ops {
    void *ctx;
    /* Optical raw read; same contract as kui_capture_ops.read. NULL skips
     * the optical bench so hash and SD benches still run without a disc. */
    enum kui_read_result (*read)(void *, uint32_t fad, unsigned sectors, uint8_t *out);
    /* Tear down the SD link and bring it back on the requested transport
     * (use_sci: 0 SCIF, 1 SCI) with CRC read-verification set. Returns the
     * interface actually in use, which differs from the request when the
     * adapter cannot do SCI and the platform falls back, or -1 if the card
     * did not come back at all. NULL means no transport sweep: the SD benches
     * run once on whatever link the caller already opened. */
    int (*reconnect)(void *, unsigned use_sci, bool crc);
    bool (*cancelled)(void *);
    uint64_t (*now_us)(void *);
    kui_log_fn log;
    /* Optional; NULL disables the feature that needs it. */
    /* Cap UI redraws per second while busy (KUI_OPT_UI_FULL = unthrottled). */
    void (*set_ui)(void *, unsigned hz);
    void (*cpu_mark)(void *, struct kui_cpu_census *);
    /* Optical read for the sweep: up to KUI_OPT_SWEEP_CHUNK_MAX sectors, same
     * PIO command and guards as capture, no copy out. service_us > 0 spins
     * that long after every firmware poll instead of the normal yield policy.
     * *data points at the sectors just read, valid until the next call.
     * Stats accumulate; the caller zeroes them. */
    enum kui_read_result (*read_probe)(void *, uint32_t fad, unsigned sectors,
        unsigned service_us, const uint8_t **data, struct kui_probe_stats *stats);
    /* Run the real capture engine on `sectors` sectors from `fad` as one track,
     * publishing nothing (kui_capture_bench). NEW makes a job whose directory comes
     * back in stats; RESUME re-opens it, which times the resume check. */
    enum kui_capture_result (*capture_run)(void *, uint32_t fad, unsigned sectors, bool audio,
        enum kui_capture_mode mode, const struct kui_capture_options *options,
        struct kui_capture_stats *stats);
};
enum kui_bench_result { KUI_BENCH_FAILED, KUI_BENCH_STOPPED, KUI_BENCH_COMPLETE };

/* Sole I/O worker, media connected. Mounts, runs optical -> hash -> SD in
 * that order (optical first so the drive state matches a fresh INIT plus one
 * warm-up read every time), unmounts. The SD scratch file is always removed. */
enum kui_bench_result kui_bench(const struct kui_bench_ops *ops,
                                const struct kui_options *opt);
#endif
