/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include <kos/timer.h>

/* The console side of the bench: everything that needs KOS, the GD-ROM
 * syscalls, or the SD adapter lives here; src/core/bench.c never sees them.
 * This mirrors src/dreamcast/capture.c, which plays the same role for the
 * capture engine. */

struct kui_options kui_options;   /* last loaded /KUI/bench.cfg; defaults until then */

static bool cancelled(void *ctx) { (void)ctx; return kui_cancelled(); }
static uint64_t now_us(void *ctx) { (void)ctx; return timer_us_gettime64(); }

/* Re-read bench.cfg from the card and echo it into the log. Called at the
 * start of every operation that consumes an option, so a report always
 * shows the values that were actually in effect. The yield quantum is
 * applied to disc.c here because it affects captures, not just benches. */
bool kui_options_refresh(void) {
    kui_options_default(&kui_options);
    bool ok = false;
    if(kui_sd_connect()) {
        FATFS fs;
        if(kui_mount(&fs, kui_log)) {
            ok = kui_options_load(&kui_options, "0:/KUI/bench.cfg", kui_log);
            f_mount(NULL, "0:", 0);
        }
        kui_sd_disconnect();
    }
    kui_options_log(&kui_options, kui_log);
    kui_disc_set_yield_us(kui_options.yield_us);
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
    if(kui_cancelled()) return KUI_BENCH_STOPPED;
    if(!kui_sd_connect()) return KUI_BENCH_FAILED;
    struct kui_bench_ops ops = {NULL, disc ? kui_disc_read_raw : NULL, cancelled, now_us, kui_log};
    enum kui_bench_result result = kui_bench(&ops, &kui_options);
    kui_sd_disconnect();
    /* Prints the OPTICAL capture subtimers (submit/poll/wait) for the bench
     * reads, the same breakdown a capture report shows. */
    if(disc) kui_disc_timing_report();
    return result;
}
