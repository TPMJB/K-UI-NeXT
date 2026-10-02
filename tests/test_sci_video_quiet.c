/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/sci_video_quiet.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    struct kui_sci_video_quiet q={0};uint32_t seen;bool pending,skipped;
    /* A request arriving during an old frame cannot count that frame as its
     * final notice, even if the UI has not reached the drain yet. */
    assert(kui_sci_video_quiet_draw_begin(&q));
    uint32_t first=kui_sci_video_quiet_request(&q);
    assert(!kui_sci_video_quiet_ack(&q,first));
    assert(kui_sci_video_quiet_draw_due(&q,false,&seen,&pending,&skipped));
    assert(seen==first && pending && !skipped);
    assert(kui_sci_video_quiet_draw_begin(&q));
    /* The real UI calls ack only after drawing and sq_wait return. */
    assert(!q.acknowledged);
    assert(kui_sci_video_quiet_ack(&q,seen));
    uint32_t start=q.total_frames;
    assert(!kui_sci_video_quiet_ack(&q,seen));
    assert(!kui_sci_video_quiet_draw_due(&q,false,&seen,&pending,&skipped));
    assert(!pending && !skipped && q.skipped==0);
    for(unsigned i=0;i<120;i++) {
        assert(!kui_sci_video_quiet_draw_due(&q,true,&seen,&pending,&skipped));
        assert(skipped && !pending);
    }
    assert(q.skipped==120 && q.total_frames-start==0);
    assert(!kui_sci_video_quiet_draw_begin(&q));
    assert(q.skipped==121 && q.total_frames-start==0);
    kui_sci_video_quiet_withdraw(&q,first);
    assert(!q.requested && !q.acknowledged && q.redraw);
    assert(kui_sci_video_quiet_draw_due(&q,false,&seen,&pending,&skipped));
    assert(!pending && !skipped && !q.redraw);
    assert(kui_sci_video_quiet_draw_begin(&q));

    /* Timeout/cancel while the UI is drawing/draining withdraws the request;
     * its eventual acknowledgment cannot authorize DMA or resurrect quiet. */
    uint32_t abandoned=kui_sci_video_quiet_request(&q);
    assert(kui_sci_video_quiet_draw_begin(&q));
    kui_sci_video_quiet_withdraw(&q,abandoned);
    assert(!kui_sci_video_quiet_ack(&q,abandoned));
    assert(!q.requested && !q.acknowledged);
    uint32_t next=kui_sci_video_quiet_request(&q);
    assert(next!=abandoned && !kui_sci_video_quiet_ack(&q,next));
    assert(kui_sci_video_quiet_draw_begin(&q));
    assert(!kui_sci_video_quiet_ack(&q,abandoned));
    kui_sci_video_quiet_withdraw(&q,abandoned);
    assert(q.requested && kui_sci_video_quiet_ack(&q,next));
    kui_sci_video_quiet_withdraw(&q,next);

    /* Frame subtraction and the nonzero generation also survive wrap. */
    q.generation=UINT32_MAX;q.total_frames=UINT32_MAX;
    next=kui_sci_video_quiet_request(&q);assert(next==1);
    assert(kui_sci_video_quiet_draw_begin(&q) && q.total_frames==0);
    assert(kui_sci_video_quiet_ack(&q,next));
    assert(q.total_frames-q.first_frame==0);
    kui_sci_video_quiet_withdraw(&q,next);
    puts("PASS SCI display quiet: fresh final frame, ack gate, no active-window draws, cancel/timeout/stale ack, redraw and wrap");
    return 0;
}
