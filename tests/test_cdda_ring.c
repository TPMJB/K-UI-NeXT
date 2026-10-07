/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_ring.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
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
