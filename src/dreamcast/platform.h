/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_PLATFORM_H
#define KUI_PLATFORM_H
#include "kui/probe.h"
#include "kui/capture.h"
void kui_log(const char *format, ...);
bool kui_cancelled(void);
bool kui_sd_connect(void);
void kui_sd_disconnect(void);
void kui_disc_probe(void);
void kui_drive_init_bus(void);
void kui_bootstrap_load(kui_cancel_fn cancelled);
bool kui_disc_prepare(struct kui_toc sessions[2]);
enum kui_read_result kui_disc_read_raw(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out);
void kui_capture_start(enum kui_capture_mode mode,const char *build);
void kui_capture_status(void *ctx,const struct kui_capture_progress *progress);
struct kui_memory_stats {
    uint32_t physical,firmware,image,main_stack,heap_system,heap_used,heap_free;
    uint32_t heap_max_system,unclaimed,used,available,sampled_peak,framebuffers;
};
bool kui_memory_snapshot(struct kui_memory_stats *out);
void kui_memory_log(const char *reason);
#endif
