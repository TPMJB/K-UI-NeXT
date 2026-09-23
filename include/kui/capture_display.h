/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CAPTURE_DISPLAY_H
#define KUI_CAPTURE_DISPLAY_H
#include "kui/capture.h"
/* Presentation only. Observes the accepted engine's existing progress/log
 * messages; never schedules a read or changes persisted retry accounting. */
struct kui_capture_display {
    char title[129];
    uint32_t retries, retry_fad;
    unsigned retry_attempt, retry_limit;
};
void kui_capture_display_start(struct kui_capture_display *out,const char *title);
void kui_capture_display_progress(struct kui_capture_display *out,const struct kui_capture_progress *p);
void kui_capture_display_log(struct kui_capture_display *out,const char *text);
#endif
