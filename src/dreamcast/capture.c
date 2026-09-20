/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include <kos/timer.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static bool cancelled(void *ctx) { (void)ctx;return kui_cancelled(); }
static uint64_t now(void *ctx) { (void)ctx;return timer_ms_gettime64(); }
static uint64_t now_us(void *ctx) { (void)ctx;return timer_us_gettime64(); }
/* The bench runs the engine dozens of times; its per-run chatter would flood the
 * 768-line report, so only problems get through. */
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
        kui_disc_timing_phase,options,stats};
    return kui_capture_bench(&ops,fad,sectors,audio,mode);
}
enum kui_capture_result kui_capture_start(enum kui_capture_mode mode,const char *build) {
    struct kui_toc sessions[2];struct kui_capture_plan plan;
    kui_options_refresh();   /* logs the options in effect; a bad file keeps defaults */
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
        .resume_size_only=kui_options.resume_size[0],.sample_every=kui_options.sample_readback[0]};
    struct kui_capture_ops ops={NULL,kui_disc_read_raw,cancelled,now,kui_capture_status,
        kui_log,build,now_us,kui_disc_timing_phase,&options,NULL};
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
