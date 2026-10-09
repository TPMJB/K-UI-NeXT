/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_ring.h"
#include <assert.h>
#include <stdio.h>

static void check_reference_clock(void) {
    uint32_t ticks;
    assert(kui_cdda_frames_to_ticks(KUI_CDDA_HALF_FRAMES,&ticks));
    assert(ticks==KUI_CDDA_HALF_TICKS);
    assert(kui_cdda_ms_to_ticks(8,&ticks));
    assert(ticks==KUI_CDDA_MARGIN_TICKS);
    struct kui_cdda_ring r;
    assert(kui_cdda_ring_init(&r,0,0)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,KUI_CDDA_HALF_TICKS-1u,8191)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_init(&r,0,0)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,KUI_CDDA_HALF_TICKS,8191)==KUI_CDDA_RING_DEADLINE);

    /* One frame beyond the read-latency allowance must still fail. */
    assert(kui_cdda_ms_to_ticks(1,&ticks));
    uint32_t expected=(uint32_t)((uint64_t)ticks*KUI_CDDA_SAMPLE_HZ/KUI_CDDA_TMU_HZ);
    assert(kui_cdda_ring_init(&r,0,0)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,ticks,expected+64u)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_init(&r,0,0)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,ticks,expected+65u)==KUI_CDDA_RING_DEADLINE);

    /* Test both sides of the 8 ms margin using the independent wide ratio. */
    for(uint32_t remaining=350;remaining<=354;++remaining) {
        assert(kui_cdda_ring_init(&r,0,KUI_CDDA_RING_FRAMES-remaining)==KUI_CDDA_RING_OK);
        r.ready[0]=false;
        uint32_t margin=(uint32_t)((uint64_t)remaining*KUI_CDDA_TMU_HZ/KUI_CDDA_SAMPLE_HZ);
        enum kui_cdda_ring_result want=margin<=KUI_CDDA_MARGIN_TICKS ?
            KUI_CDDA_RING_DEADLINE : KUI_CDDA_RING_OK;
        assert(kui_cdda_ring_can_fill(&r,0)==want);
        assert(r.min_margin_ticks==margin);
    }
}

static void check_wrap_and_maximum(void) {
    struct kui_cdda_ring r;
    uint32_t start=UINT32_MAX-KUI_CDDA_HALF_TICKS/2u;
    assert(kui_cdda_ring_init(&r,start,8190)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,start+KUI_CDDA_HALF_TICKS-1u,8195)==KUI_CDDA_RING_OK);
    assert(r.played==5 && !r.ready[0]);
    assert(r.max_service_ticks==KUI_CDDA_HALF_TICKS-1u);
    assert(kui_cdda_ring_init(&r,start,8190)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,start+KUI_CDDA_HALF_TICKS,8195)==KUI_CDDA_RING_DEADLINE);
    assert(kui_cdda_ring_init(&r,100,0)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,99,0)==KUI_CDDA_RING_DEADLINE);
    assert(r.max_service_ticks==UINT32_MAX);

    assert(kui_cdda_ring_init(&r,0,8191)==KUI_CDDA_RING_OK);
    r.played=UINT32_MAX;
    assert(kui_cdda_ring_observe(&r,1000,8192)==KUI_CDDA_RING_ARGUMENT);
    assert(r.played==UINT32_MAX && r.last_tick==0 && r.last_position==8191);
    assert(r.ready[0] && r.ready[1]);
    r.played=UINT32_MAX-1u;
    assert(kui_cdda_ring_observe(&r,1000,8192)==KUI_CDDA_RING_OK);
    assert(r.played==UINT32_MAX && !r.ready[0]);
}
int main(void) {
    check_reference_clock();
    check_wrap_and_maximum();
    struct kui_cdda_ring r;
    assert(kui_cdda_ring_init(NULL,0,0)==KUI_CDDA_RING_ARGUMENT);
    assert(kui_cdda_ring_init(&r,UINT32_MAX-1000,8190)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,0,8195)==KUI_CDDA_RING_OK);
    assert(r.played==5 && !r.ready[0]);
    assert(kui_cdda_ring_can_fill(&r,1)==KUI_CDDA_RING_NOT_READY);
    assert(kui_cdda_ring_can_fill(&r,0)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_commit(&r,0)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_can_fill(&r,0)==KUI_CDDA_RING_NOT_READY);
    assert(kui_cdda_ring_observe(&r,KUI_CDDA_HALF_TICKS,8195)==KUI_CDDA_RING_DEADLINE);
    assert(kui_cdda_ring_init(&r,0,8191)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,1000,8192)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,2201000,16000)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_can_fill(&r,0)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,2300000,16380)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_commit(&r,0)==KUI_CDDA_RING_DEADLINE);
    assert(kui_cdda_ring_observe(&r,2302000,3)==KUI_CDDA_RING_NOT_READY);
    assert(kui_cdda_ring_init(&r,0,100)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,100,99)==KUI_CDDA_RING_DEADLINE);
    assert(kui_cdda_ring_init(&r,0,16380)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,3000,6)==KUI_CDDA_RING_OK);
    assert(r.played==10 && !r.ready[1]);
    assert(kui_cdda_ring_init(&r,0,8191)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,KUI_CDDA_HALF_TICKS-1,0)==KUI_CDDA_RING_DEADLINE);
    assert(kui_cdda_ring_init(&r,0,0)==KUI_CDDA_RING_OK);
    assert(kui_cdda_ring_observe(&r,2*KUI_CDDA_HALF_TICKS,0)==KUI_CDDA_RING_DEADLINE);
    puts("CDDA ring timeline/deadline checks passed");
}
