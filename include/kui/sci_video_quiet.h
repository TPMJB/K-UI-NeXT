/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SCI_VIDEO_QUIET_H
#define KUI_SCI_VIDEO_QUIET_H
#include <stdbool.h>
#include <stdint.h>

/* Call under the main/worker mutex. Only the UI thread may begin a frame or
 * acknowledge a request. It acknowledges only after drawing the final notice
 * and draining store queues; this state helper does not perform those actions. */
struct kui_sci_video_quiet {
    uint32_t generation, total_frames, requested_frame, first_frame, skipped;
    bool requested, acknowledged, redraw;
};
static inline uint32_t kui_sci_video_quiet_request(struct kui_sci_video_quiet *q) {
    if(++q->generation==0) ++q->generation;
    q->requested=true;q->acknowledged=false;q->skipped=0;
    q->requested_frame=q->total_frames;
    return q->generation;
}
static inline void kui_sci_video_quiet_withdraw(struct kui_sci_video_quiet *q,uint32_t generation) {
    if(q->generation!=generation) return;
    q->requested=false;q->acknowledged=false;q->redraw=true;
}
static inline bool kui_sci_video_quiet_ack(struct kui_sci_video_quiet *q,uint32_t generation) {
    if(!q->requested || q->acknowledged || q->generation!=generation ||
       q->total_frames==q->requested_frame) return false;
    q->first_frame=q->total_frames;q->acknowledged=true;
    return true;
}
static inline bool kui_sci_video_quiet_draw_begin(struct kui_sci_video_quiet *q) {
    if(q->requested && q->acknowledged) {++q->skipped;return false;}
    ++q->total_frames;return true;
}
static inline bool kui_sci_video_quiet_draw_due(struct kui_sci_video_quiet *q,bool due,
        uint32_t *generation,bool *pending,bool *skipped) {
    *generation=q->generation;*pending=q->requested && !q->acknowledged;*skipped=false;
    if(q->redraw) {due=true;q->redraw=false;}
    if(*pending) due=true;
    if(q->requested && q->acknowledged && due) {++q->skipped;*skipped=true;return false;}
    return due;
}
#endif
