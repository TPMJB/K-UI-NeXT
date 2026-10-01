/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_STORAGE_TEST_H
#define KUI_STORAGE_TEST_H
#include "kui/core.h"
#include "kui/storage_error.h"
#include "kui/probe.h"

#define KUI_STORAGE_TEST_SAMPLES 15u
#define KUI_STORAGE_TEST_HISTORY 8u
#define KUI_STORAGE_TEST_PATH 80u
enum kui_storage_test_preset { KUI_STORAGE_TEST_QUICK, KUI_STORAGE_TEST_COMPARE,
    KUI_STORAGE_TEST_SOAK };
enum kui_storage_test_outcome { KUI_STORAGE_TEST_NONE, KUI_STORAGE_TEST_RUNNING,
    KUI_STORAGE_TEST_PASSED, KUI_STORAGE_TEST_STOPPED, KUI_STORAGE_TEST_FAILED };
struct kui_storage_test_request {
    unsigned preset, repeats, soak_minutes;
    char card_label[24];
};
struct kui_storage_test_metadata {
    char build[16];
    unsigned transport;
    bool music_playing;
    unsigned ui_hz;
    int64_t local_seconds; /* 0 means clock unavailable; IDs never depend on it. */
    uint32_t volume_start, volume_sectors, cluster_bytes;
    uint64_t free_bytes;
    char filesystem[8];
};
struct kui_storage_test_latency {
    uint64_t calls, total_us, over_20ms, over_100ms;
    uint32_t min_us, max_us, p95_upper_us; /* Histogram upper bound, not exact p95. */
};
struct kui_storage_test_sample {
    uint32_t chunk_bytes, repeat;
    uint64_t bytes, write_us, read_us, sync_us;
    bool verified;
};
struct kui_storage_test_result {
    uint32_t id;
    struct kui_storage_test_request request;
    struct kui_storage_test_metadata metadata;
    unsigned outcome, sample_count;
    uint64_t elapsed_us, written_bytes, verified_bytes, cycles;
    struct kui_storage_test_latency write_latency, read_latency;
    struct kui_storage_test_sample samples[KUI_STORAGE_TEST_SAMPLES];
    struct kui_storage_errors errors;
    unsigned fatfs_error;
    uint64_t failure_offset;
    char failure_phase[24], message[128], path[KUI_STORAGE_TEST_PATH];
    bool saved, cleanup_failed;
};
struct kui_storage_test_progress {
    unsigned preset, repeat, repeats, step, steps;
    uint64_t done, total, elapsed_us, target_us;
    char phase[24];
};
struct kui_storage_test_ops {
    void *ctx;
    uint64_t (*now_us)(void *);
    bool (*cancelled)(void *);
    void (*progress)(void *, const struct kui_storage_test_progress *);
    void (*errors)(void *, struct kui_storage_errors *);
    kui_log_fn log;
};
struct kui_storage_test_history {
    struct kui_storage_test_result rows[KUI_STORAGE_TEST_HISTORY]; /* Newest first. */
    unsigned count;
    bool baseline_valid;
    struct kui_storage_test_result baseline;
    char message[128];
};
void kui_storage_test_defaults(struct kui_storage_test_request *request);
bool kui_storage_test_request_valid(const struct kui_storage_test_request *request);
const char *kui_storage_test_preset_name(unsigned preset);
const char *kui_storage_test_outcome_name(unsigned outcome);
/* Sole filesystem worker, connected selected device, no other mounted files.
 * Does not select/reconnect transports or alter bench.cfg. Creates its own run
 * directory and scratch file, never overwrites user files. Returns unmounted.
 * metadata arrives with build/transport/music/UI/time; mounted volume details
 * are filled here. Result persistence is performed separately by the caller. */
void kui_storage_test_run(const struct kui_storage_test_request *request,
    const struct kui_storage_test_metadata *metadata,
    const struct kui_storage_test_ops *ops, struct kui_storage_test_result *result);
/* Store helpers own mount/unmount. Begin allocates a unique ID/directory and
 * writes a started marker. Engine calls begin before scratch work. */
bool kui_storage_test_begin(struct kui_storage_test_result *result, kui_log_fn log);
bool kui_storage_test_save(struct kui_storage_test_result *result, kui_log_fn log);
bool kui_storage_test_load_history(struct kui_storage_test_history *history, kui_log_fn log);
bool kui_storage_test_set_baseline(uint32_t id, kui_log_fn log);
/* Same measurement recipe; build/transport/card may differ intentionally.
 * Failed/stopped runs never qualify as a baseline. */
bool kui_storage_test_comparable(const struct kui_storage_test_result *a,
    const struct kui_storage_test_result *b);
/* Median measured KiB/s across the verified samples for a requested chunk.
 * Soak aggregates all cycles into one sample; no unbounded sample allocation. */
uint32_t kui_storage_test_rate(const struct kui_storage_test_result *result,
    uint32_t chunk_bytes, bool write);
#endif
