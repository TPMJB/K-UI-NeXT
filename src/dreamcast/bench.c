/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/settings.h"
#include <kos/thread.h>
#include <kos/timer.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The console side of the bench: everything that needs KOS, the GD-ROM
 * syscalls, or the SD adapter lives here; src/core/bench.c never sees them.
 * This mirrors src/dreamcast/capture.c, which plays the same role for the
 * capture engine. */

/* The split DMA read is in every build (the pipeline section uses it). The blocking probe and
 * its competing thread are research tools and stay in the opt-in experimental build only. */
static bool read_begin(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    return kui_disc_read_begin(ctx,fad,sectors,out);
}
static enum kui_read_result read_end(void *ctx) { return kui_disc_read_end(ctx); }
#ifdef KUI_EXPERIMENTAL_DMA
#define KUI_DMA_OPS(disc) ((disc) ? kui_disc_read_probe_dma : NULL), spin, spin_count, sleep_ms, \
                          ((disc) ? read_begin : NULL), ((disc) ? read_end : NULL)
#else
#define KUI_DMA_OPS(disc) NULL, NULL, NULL, NULL, ((disc) ? read_begin : NULL), ((disc) ? read_end : NULL)
#endif

struct kui_options kui_options;   /* last loaded /KUI/bench.cfg; defaults until then */

static bool cancelled(void *ctx) { (void)ctx; return kui_cancelled(); }

/* Drop the SD link and reopen it on the requested transport. kui_sd_connect
 * falls back to SCIF when SCI will not initialise, so report what it actually
 * opened rather than what was asked for. */
static int reconnect(void *ctx, unsigned use_sci, bool crc) {
    (void)ctx;
    kui_sd_disconnect();
    kui_sd_set_params(use_sci, crc);
    if(!kui_sd_connect()) return -1;
    return (int)kui_sd_active_sci();
}
static uint64_t now_us(void *ctx) { (void)ctx; return timer_us_gettime64(); }
static uint64_t now(void *ctx) { (void)ctx; return timer_ms_gettime64(); }
/* The bench runs the engine dozens of times; its per-run chatter would flood the
 * bounded diagnostic report, so only problems get through. */
static void bench_log(const char *format,...) {
    char line[128];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);
    if(strstr(line,"fail")||strstr(line,"FAIL")||strstr(line,"mismatch")||strstr(line,"Insufficient")||
       strstr(line,"refused")||strstr(line,"exhausted")||strstr(line,"Unexpected")) kui_log("%s",line);
}
enum kui_capture_result kui_capture_bench_run(uint32_t fad,unsigned sectors,bool audio,
    enum kui_capture_mode mode,const struct kui_capture_options *options,struct kui_capture_stats *stats) {
    /* The card is the bench's, not this function's: bench_capture (src/core/bench.c) opens it,
     * mounts it to check free space before the first run and again after every run to delete
     * the job, and the console closes it when the bench ends. Opening and closing it here left
     * it closed for both of those. */
    struct kui_capture_ops ops={NULL,kui_disc_read_raw,cancelled,now,NULL,bench_log,"000000000000",now_us,
        kui_disc_timing_phase,read_begin,read_end,options,stats};
    return kui_capture_bench(&ops,fad,sectors,audio,mode);
}
static void set_ui(void *ctx, unsigned hz) { (void)ctx; kui_ui_set_hz(hz); }

/* KOS's own CRC16 (kernel/net/net_crc.c), the one the SD driver runs over every block it
 * writes. Declared here instead of including <kos/net.h>, which pulls in the network stack
 * for one prototype. It is already linked: the SD driver calls it. */
extern uint16_t net_crc16ccitt(const uint8_t *data, int size, uint16_t start);
static uint16_t crc16_kos(void *ctx, const uint8_t *data, size_t bytes, uint16_t start) {
    (void)ctx;
    return net_crc16ccitt(data, (int)bytes, start);
}

#ifdef KUI_EXPERIMENTAL_DMA   /* the competing thread exists for the DMA probe experiment (Trip 6) */
/* A CPU-bound thread at the worker's priority: what an SD-writing thread would be. It bumps a
 * counter every 64 iterations while enabled and sleeps while not, so the count is a direct
 * measure of the CPU it was given. The counter is one 32-bit word, read atomically, that
 * cannot wrap for hours (a 64-bit one could be read half-updated when the thread is
 * preempted between its two stores). Created on first use and kept for the rest of the boot,
 * asleep, at the cost of one idle wake-up every 10 ms. */
static volatile bool spin_enabled;
static volatile uint32_t spin_total;
static kthread_t *spin_thread;
static void *spin_main(void *arg) {
    (void)arg;
    for(;;) {
        if(!spin_enabled) { thd_sleep(10); continue; }
        for(unsigned i = 0; i < 64; ++i) __asm__ volatile("" ::: "memory");
        ++spin_total;
    }
    return NULL;
}
static void spin(void *ctx, bool on) {
    (void)ctx;
    if(on && !spin_thread) spin_thread = thd_create(1, spin_main, NULL);
    spin_enabled = on;
}
static uint64_t spin_count(void *ctx) { (void)ctx; return spin_total; }
static void sleep_ms(void *ctx, unsigned ms) { (void)ctx; thd_sleep((int)ms); }
#endif
static void cpu_mark(void *ctx, struct kui_cpu_census *out) { (void)ctx; kui_cpu_census_mark(out); }
static enum kui_capture_result capture_run(void *ctx, uint32_t fad, unsigned sectors, bool audio,
    enum kui_capture_mode mode, const struct kui_capture_options *options, struct kui_capture_stats *stats) {
    (void)ctx;
    return kui_capture_bench_run(fad, sectors, audio, mode, options, stats);
}

/* Re-read bench.cfg from the card and echo it into the log. Called at the
 * start of every operation that consumes an option, so a report always
 * shows the values that were actually in effect. The yield quantum is
 * applied to disc.c here because it affects captures, not just benches. */
bool kui_options_refresh(void) {
    kui_options_default(&kui_options);
    /* Read the file over the transport KOS itself defaults to. If a previous
     * run left sd_if=sci set and that adapter cannot do SCI, this is what
     * guarantees bench.cfg is still readable to change the value back. */
    kui_sd_set_params(0, true);
    bool ok = false;
    if(kui_sd_connect()) {
        FATFS fs;
        if(kui_mount(&fs, kui_log)) {
            struct kui_settings settings;
            ok = kui_settings_load(&settings, kui_log);
            if(ok) {
                kui_options.capture_crc_only[0] = settings.crc_only;
                kui_options.end_readback[0] = settings.end_readback;
                kui_log("Settings: capture_hash=%s end_readback=%s memory=%s",
                    settings.crc_only ? "crc32" : "both", settings.end_readback ? "on" : "off",
                    settings.show_memory ? "on" : "off");
                kui_log("Option precedence: defaults, saved settings, explicit bench.cfg keys");
                ok = kui_options_overlay(&kui_options, "0:/KUI/bench.cfg", kui_log);
            }
            f_mount(NULL, "0:", 0);
        }
        kui_sd_disconnect();
    }
    kui_options_log(&kui_options, kui_log);
    kui_disc_set_yield_us(kui_options.yield_us);
    /* The first ui_hz value applies to the whole operation; a bench overrides it
     * per pass. Defaults to 2 (see kui_options_default for the measurements behind
     * that); 'ui_hz=full' restores the unthrottled loop. */
    kui_ui_set_hz(kui_options.ui_hz[0]);
    /* A rejected overlay leaves defaults plus saved settings intact; all
     * accepted transport settings still came through the parser. */
    return ok;
}

enum kui_bench_result kui_bench_start(void) {
    if(!kui_options_refresh()) {
        kui_log("Bench refused: bench.cfg rejected or SD unavailable; see lines above");
        return KUI_BENCH_FAILED;
    }
    struct kui_toc sessions[2];
    kui_disc_timing_reset();
    bool disc = kui_disc_prepare(sessions);
    if(disc) kui_disc_timing_phase(NULL, true);   /* single-read capture policy */
    else kui_log("No readable disc; hash and SD benches will still run");
    /* Say so before a long run measures the wrong thing (see kui_bench_fad_note). */
    if(disc && (kui_options.sections & KUI_SEC_CAPTURE)) {
        const char *note = kui_bench_fad_note(sessions,
            kui_options.capture_fad ? kui_options.capture_fad : kui_options.optical_fad,
            kui_options.capture_audio);
        if(note) kui_log("BENCH WARNING: %s", note);
    }
    if(kui_cancelled()) return KUI_BENCH_STOPPED;
    /* No connect here: kui_bench opens the link itself once per swept
     * transport, so the SD benches and the link always agree. */
    struct kui_bench_ops ops = {NULL, disc ? kui_disc_read_raw : NULL, reconnect,
                                cancelled, now_us, kui_log, set_ui, cpu_mark,
                                disc ? kui_disc_read_probe : NULL, disc ? capture_run : NULL,
                                crc16_kos, KUI_DMA_OPS(disc)};
    enum kui_bench_result result = kui_bench(&ops, &kui_options);
    kui_sd_disconnect();
    /* Prints the OPTICAL capture subtimers (submit/poll/wait) for the bench
     * reads, the same breakdown a capture report shows. */
    if(disc) kui_disc_timing_report();
    return result;
}
