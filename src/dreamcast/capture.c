/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include <kos/timer.h>
static bool cancelled(void *ctx) { (void)ctx;return kui_cancelled(); }
static uint64_t now(void *ctx) { (void)ctx;return timer_ms_gettime64(); }
static uint64_t now_us(void *ctx) { (void)ctx;return timer_us_gettime64(); }
enum kui_capture_result kui_capture_start(enum kui_capture_mode mode,const char *build) {
    struct kui_toc sessions[2];struct kui_capture_plan plan;
    kui_disc_timing_reset();
    if(!kui_disc_prepare(sessions) || kui_cancelled())
        return kui_cancelled()?KUI_CAPTURE_STOPPED:KUI_CAPTURE_FAILED;
    if(!kui_plan_tracks(sessions,&plan)) {
        kui_log("Unsupported TOC for this GDI profile; no dump files written");return KUI_CAPTURE_FAILED;
    }
    if(!kui_sd_connect()) return KUI_CAPTURE_FAILED;
    struct kui_capture_ops ops={NULL,kui_disc_read_raw,cancelled,now,kui_capture_status,
        kui_log,build,now_us,kui_disc_timing_phase};
    enum kui_capture_result result=kui_capture(&plan,&ops,mode);
    kui_sd_disconnect();
    kui_disc_timing_report();
    return result;
}
