/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_SD_BENCH_H
#define KUI_RETAIL_SD_BENCH_H
#include "sd_reader.h"
#include "retail_display.h"
#include "kui/retail_image.h"

#define KUI_SD_BENCH_BLOCKS 40u
#define KUI_SD_BENCH_HZ 12500000u
struct kui_sd_bench_stat { uint64_t ticks; uint32_t max_ticks, blocks; };
struct kui_sd_bench_result {
    struct kui_sd_bench_stat stats[3][2];
    uint32_t lba, crc, failed_lba, failed_method;
    enum kui_loader_sd_result card_result;
};
enum kui_sd_bench_status {
    KUI_SD_BENCH_OK, KUI_SD_BENCH_WINDOW, KUI_SD_BENCH_IO,
    KUI_SD_BENCH_MISMATCH, KUI_SD_BENCH_CLOCK
};
/* Standalone, serialized diagnostic only. Clock counts upward modulo 2^32.
 * Each transfer acquires/releases pins. No filesystem writes or game handoff.
 * Static buffers belong to high staging memory, never the low resident. */
enum kui_sd_bench_status kui_retail_sd_bench_run(struct kui_loader_sd *,
    const struct kui_retail_manifest *, struct kui_sd_bench_result *,
    uint32_t (*clock_ticks)(void));
/* Deliberately an ordinary external declaration: both stage variants retain
 * the audited bootstrap/relay code. The diagnostic itself ends on its screen. */
void kui_retail_sd_benchmark(struct kui_loader_sd *,
    const struct kui_retail_manifest *, const struct retail_display_state *);
#endif
