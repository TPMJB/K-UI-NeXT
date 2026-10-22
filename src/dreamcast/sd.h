/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RUNTIME_SD_H
#define KUI_RUNTIME_SD_H
#include "kui/sci_async_probe.h"
#include "sci_async_heartbeat.h"
#include "../loader/sd_reader.h"
enum kui_sd_async_recovery_phase {
    KUI_SD_ASYNC_RECOVERY_NONE, KUI_SD_ASYNC_RECOVERY_ACQUIRE,
    KUI_SD_ASYNC_RECOVERY_INITIALIZE, KUI_SD_ASYNC_RECOVERY_READ
};
/* Runtime Diagnostics only. The sole storage worker pauses all consumers and
 * owns the connected SCI card with all files closed. This wrapper unmounts
 * the volume before raw reads and leaves it unmounted on every exit. */
#define KUI_SD_ASYNC_STRESS_SECTORS 16u
#define KUI_SD_ASYNC_STRESS_US UINT64_C(60000000)
#define KUI_SD_ASYNC_STRESS_READ_LIMIT 262144u
/* Speed comparison: the same real data, 1 MiB, read first by the ordinary
 * reader (CMD18 runs, CPU-fed) and then by the async reader (one CMD17 per
 * block, polled with no other work). */
#define KUI_SD_ASYNC_SPEED_BLOCKS 2048u
/* Then the async reader again, as CMD18 streams (see begin_stream). */
/* CMD18 measurements after the async pass, on the same blocks: a continuous
 * 16 KiB capture from the first block and a 64-block per-block resume run
 * from block 64. Each CRC-valid block must equal the async pass's copy. */
#define KUI_SD_ASYNC_CAPTURE_BYTES 16384u
#define KUI_SD_ASYNC_RESUME_OFFSET 64u
#define KUI_SD_ASYNC_RESUME_BLOCKS 64u
#define KUI_SD_ASYNC_COMPARE_BLOCKS (KUI_SD_ASYNC_RESUME_OFFSET+KUI_SD_ASYNC_RESUME_BLOCKS)
struct kui_sd_async_result {
    struct kui_sci_async_probe_result probe;
    bool baseline_verified, recovery_verified, recovery_reinitialized, restart_required;
    uint32_t baseline_crc32;
    enum kui_sd_async_recovery_phase recovery_phase;
    enum kui_loader_sd_result recovery_result;
    uint8_t recovery_command, recovery_response;
    bool recovery_command_valid, recovery_bus_healthy, recovery_data_match;
    /* Sustained runtime diagnostic: baselines are obtained through ordinary
     * CRC-checked reads before the reusable reader takes its lease. Timing
     * excludes baseline preparation and the final normal-reader recovery. */
    bool sustained, duration_complete, iteration_limit;
    /* Set by the caller after the run: shell redraws were left running. */
    bool screen_active;
    /* Main-thread framebuffer quiet window: acknowledged only after its final
     * frame and store-queue drain. The worker merges these after recovery. */
    bool video_quiet_requested, video_quiet_acknowledged, video_sq_drained;
    uint32_t video_frames_during, video_redraws_skipped;
    uint32_t baseline_sectors, distinct_lbas_verified, distinct_payloads;
    uint32_t baseline_lbas[KUI_SD_ASYNC_STRESS_SECTORS];
    uint32_t baseline_crcs[KUI_SD_ASYNC_STRESS_SECTORS];
    uint32_t baseline_reads[KUI_SD_ASYNC_STRESS_SECTORS];
    struct kui_sci_async_heartbeat_result heartbeat;
    uint32_t read_cycles, poll_calls, worker_yields;
    uint64_t target_us, stress_elapsed_us, read_elapsed_us, max_read_us;
    /* Speed comparison. speed_lba starts /KUI/runtime.kui when found,
     * otherwise the volume's data area; both passes read the same blocks. */
    bool speed, speed_file_found, speed_match;
    uint32_t speed_lba, speed_blocks, speed_async_blocks;
    uint32_t speed_normal_crc, speed_async_crc;
    uint64_t speed_normal_us, speed_async_us;
    /* The same blocks through the async reader's CMD18 streams, in runs
     * as long as the ordinary reader's; CRC32 must equal the ordinary pass. */
    bool speed_stream_match;
    uint32_t speed_stream_blocks, speed_stream_crc;
    uint64_t speed_stream_us;
    bool stream_ran, resume_ran, stream_match, resume_match;
    struct kui_sci_async_stream stream;
    struct kui_sci_async_resume resume;
    char message[128];
};
const char *kui_sd_async_recovery_name(enum kui_sd_async_recovery_phase phase);
void kui_sd_async_probe(struct kui_sd_async_result *out,
    bool (*cancelled)(void *),void *cancel_ctx);
void kui_sd_async_stress(struct kui_sd_async_result *out,
    bool (*cancelled)(void *),void *cancel_ctx);
void kui_sd_async_speed(struct kui_sd_async_result *out,
    bool (*cancelled)(void *),void *cancel_ctx);
#endif
