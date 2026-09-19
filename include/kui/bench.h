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
};
enum kui_bench_result { KUI_BENCH_FAILED, KUI_BENCH_STOPPED, KUI_BENCH_COMPLETE };

/* Sole I/O worker, media connected. Mounts, runs optical -> hash -> SD in
 * that order (optical first so the drive state matches a fresh INIT plus one
 * warm-up read every time), unmounts. The SD scratch file is always removed. */
enum kui_bench_result kui_bench(const struct kui_bench_ops *ops,
                                const struct kui_options *opt);
#endif
