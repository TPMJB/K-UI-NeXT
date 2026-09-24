/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"

/* The overlapped GD-ROM DMA read, in every build since it was proven on hardware (Trips 11-12).
 * Whether a capture uses it is capture_read= (default dma). */
static bool capture_read_begin(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    return kui_disc_read_begin(ctx,fad,sectors,out);
}
static enum kui_read_result capture_read_end(void *ctx) { return kui_disc_read_end(ctx); }
#define KUI_CAPTURE_DMA_OPS capture_read_begin,capture_read_end
#include <kos/timer.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static struct kui_capture_stats last_stats;
const struct kui_capture_stats *kui_capture_last_stats(void) { return &last_stats; }
static bool cancelled(void *ctx) { (void)ctx;return kui_cancelled(); }
static uint64_t now(void *ctx) { (void)ctx;return timer_ms_gettime64(); }
static uint64_t now_us(void *ctx) { (void)ctx;return timer_us_gettime64(); }
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
        kui_disc_timing_phase,KUI_CAPTURE_DMA_OPS,options,stats};
    return kui_capture_bench(&ops,fad,sectors,audio,mode);
}
static enum kui_capture_result capture_start(enum kui_capture_mode mode,const char *build,
    const char *destination,bool quick_resume) {
    memset(&last_stats,0,sizeof(last_stats));
    char parent[KUI_DEST_ROOT_CAP];
    if(!kui_destination_normalize(parent,destination)) {
        kui_log("Capture refused: invalid or too-long destination; no dump writes");
        return kui_cancelled()?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
    }
    /* These local values live through the synchronous capture call; the engine
     * never borrows the UI's mutable destination buffer. */
    const struct kui_capture_output output={parent,true};
    struct kui_toc sessions[2];struct kui_capture_plan plan;
    /* Missing configuration is valid; unreadable/rejected configuration must
     * not silently weaken a user's requested verification policy. */
    if(!kui_options_refresh()) {
        kui_log("Capture refused: preferences or bench.cfg could not be loaded; no dump writes");
        return kui_cancelled()?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
    }
    kui_disc_timing_reset();
    if(!kui_disc_prepare(sessions) || kui_cancelled())
        return kui_cancelled()?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
    if(!kui_plan_tracks(sessions,&plan)) {
        kui_log("Unsupported TOC for this GDI profile; no dump files written");return KUI_CAPTURE_FAILED;
    }
    if(!kui_sd_connect()) return KUI_CAPTURE_FAILED;
    /* The first value of each capture_* list in bench.cfg; the defaults are the engine as
     * it has always been. A resumed job keeps the hash mode it started with. */
    struct kui_capture_options options={
        .crc_only=kui_options.capture_crc_only[0],.skip_end_readback=!kui_options.end_readback[0],
        .resume_size_only=(quick_resume && mode==KUI_CAPTURE_RESUME)||kui_options.resume_size[0],
        .sample_every=kui_options.sample_readback[0],
        .read_dma=kui_options.capture_dma[0],.output=&output};
    struct kui_capture_ops ops={NULL,kui_disc_read_raw,cancelled,now,kui_capture_status,
        kui_log,build,now_us,kui_disc_timing_phase,KUI_CAPTURE_DMA_OPS,&options,&last_stats};
    /* Whole-operation CPU split: how much of this capture the UI thread took. */
    struct kui_cpu_census cpu_before,cpu_after;
    kui_cpu_census_mark(&cpu_before);
    enum kui_capture_result result=kui_capture(&plan,&ops,mode);
    kui_cpu_census_mark(&cpu_after);
    kui_cpu_census_log(kui_log,"operation","(setup+capture+verify)",&cpu_before,&cpu_after);
    kui_sd_disconnect();
    kui_disc_timing_report();
    return result;
}
enum kui_capture_result kui_capture_start(enum kui_capture_mode mode,const char *build,
    const char *destination) {
    return capture_start(mode,build,destination,false);
}
enum kui_capture_result kui_capture_resume_quick(const char *build,const char *destination) {
    /* This opt-in changes only this call's local options. Checkpoint identity,
     * size checks and SHA-256 job compatibility stay with the existing engine;
     * no preferences or bench.cfg values are rewritten. */
    return capture_start(KUI_CAPTURE_RESUME,build,destination,true);
}
