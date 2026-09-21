/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_PLATFORM_H
#define KUI_PLATFORM_H
#include "kui/probe.h"
#include "kui/capture.h"
#include "kui/bench.h"

/* True when compiling for the Dreamcast's SH-4. GCC defines a DIFFERENT macro for each SH-4 mode:
 * __SH4__ only for plain -m4, __SH4_SINGLE__ for -m4-single (which is how KOS builds), plus
 * __SH4_SINGLE_ONLY__ and __SH4_NOFPU__. Testing __SH4__ alone is false on the real build, and the
 * code silently took the host-test branch there (twice in CI: it pulled KOS's cache header in again
 * and failed to compile). KOS's own -D_arch_dreamcast=1 is the most direct signal. This is the ONLY
 * place the raw macros may appear (tests/test_asm_audit.py enforces it). */
#if defined(_arch_dreamcast) || defined(__DREAMCAST__) || defined(__SH4__) || defined(__SH4_SINGLE__) || \
    defined(__SH4_SINGLE_ONLY__) || defined(__SH4_NOFPU__)
#define KUI_ON_CONSOLE 1
#endif
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
/* Bench-only optical read for the sweep (see kui/bench.h). */
enum kui_read_result kui_disc_read_probe(void *ctx,uint32_t fad,unsigned sectors,unsigned service_us,
    const uint8_t **data,struct kui_probe_stats *stats);
/* Split GD-ROM DMA read: begin returns at once, end waits and must always follow a true begin.
 * `out` must be 32-byte aligned and stay untouched until end returns. In every build. */
bool kui_disc_read_begin(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out);
bool kui_disc_read_pending(void *ctx);
enum kui_read_result kui_disc_read_end(void *ctx);
#ifdef KUI_EXPERIMENTAL_DMA
/* Bench-only GD-ROM DMA read of raw sectors (EXPERIMENTAL, opt-in build; even sector counts only). */
enum kui_read_result kui_disc_read_probe_dma(void *ctx,uint32_t fad,unsigned sectors,
    const uint8_t **data,struct kui_probe_stats *stats);
#endif
/* Cap UI redraws per second while an operation runs. KUI_OPT_UI_FULL restores
 * the unthrottled loop. Input is still polled at the same rate, so B still stops. */
void kui_ui_set_hz(unsigned hz);
/* Scheduler CPU accounting snapshot: how the CPU split between the worker, the
 * UI thread and everything else. Called from the worker thread. */
void kui_cpu_census_mark(struct kui_cpu_census *out);
/* Bench: the real capture engine on `sectors` sectors from `fad` (see kui_capture_bench). */
enum kui_capture_result kui_capture_bench_run(uint32_t fad,unsigned sectors,bool audio,
    enum kui_capture_mode mode,const struct kui_capture_options *options,struct kui_capture_stats *stats);
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
