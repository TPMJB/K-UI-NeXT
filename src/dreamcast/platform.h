/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_PLATFORM_H
#define KUI_PLATFORM_H
#include "kui/probe.h"
#include "kui/capture.h"
#include "kui/bench.h"
void kui_log(const char *format, ...);
bool kui_cancelled(void);
/* Transport for the NEXT kui_sd_connect(): use_sci picks KOS SD_IF_SCI
 * (synchronous serial, DMA capable) over the SD_IF_SCIF bit-bang default;
 * check_crc verifies the data-block CRC16 on reads. */
void kui_sd_set_params(unsigned use_sci, bool check_crc);
/* Which transport the last successful connect opened, after any fallback. */
unsigned kui_sd_active_sci(void);
bool kui_sd_connect(void);
void kui_sd_disconnect(void);
void kui_disc_probe(void);
void kui_drive_init_bus(void);
void kui_bootstrap_load(kui_cancel_fn cancelled);
bool kui_disc_prepare(struct kui_toc sessions[2]);
enum kui_read_result kui_disc_read_raw(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out);
enum kui_capture_result kui_capture_start(enum kui_capture_mode mode,const char *build);
void kui_disc_timing_reset(void);
void kui_disc_timing_phase(void *ctx,bool capturing);
void kui_disc_timing_report(void);
/* PIO service quantum for pause_worker; 0 restores the built-in default. */
void kui_disc_set_yield_us(unsigned us);
/* src/dreamcast/bench.c: options loaded from /KUI/bench.cfg and the bench entry. */
extern struct kui_options kui_options;
bool kui_options_refresh(void);
enum kui_bench_result kui_bench_start(void);
void kui_capture_status(void *ctx,const struct kui_capture_progress *progress);
struct kui_memory_stats {
    uint32_t physical,firmware,image,main_stack,heap_system,heap_used,heap_free;
    uint32_t heap_max_system,unclaimed,used,available,sampled_peak,framebuffers;
};
bool kui_memory_snapshot(struct kui_memory_stats *out);
void kui_memory_log(const char *reason);
#endif
