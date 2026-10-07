/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_ring.h"
#include <stddef.h>

enum kui_cdda_ring_result kui_cdda_ring_init(struct kui_cdda_ring *r,
                                           uint32_t now, uint32_t pos) {
    if(!r || pos >= KUI_CDDA_RING_FRAMES) return KUI_CDDA_RING_ARGUMENT;
    *r = (struct kui_cdda_ring){.last_tick=now,.last_position=pos,
        .min_margin_ticks=UINT32_MAX,.initialized=true,.ready={true,true}};
    return KUI_CDDA_RING_OK;
}
enum kui_cdda_ring_result kui_cdda_ring_observe(struct kui_cdda_ring *r,
                                              uint32_t now, uint32_t pos) {
    if(!r || !r->initialized || pos >= KUI_CDDA_RING_FRAMES)
        return KUI_CDDA_RING_ARGUMENT;
    uint32_t gap = now-r->last_tick;
    if(gap > r->max_service_ticks) r->max_service_ticks=gap;
    if(gap >= KUI_CDDA_HALF_TICKS) return KUI_CDDA_RING_DEADLINE;
    uint32_t advance=(pos-r->last_position)&(KUI_CDDA_RING_FRAMES-1u);
    /* Reject an impossible backwards/reset jump, as well as missing service.
     * The hardware may move slightly between the clock and position reads. */
    uint32_t allowed=(gap/125u)*441u/1000u+64u;
    if(advance >= KUI_CDDA_HALF_FRAMES || advance > allowed)
        return KUI_CDDA_RING_DEADLINE;
    unsigned before=r->last_position/KUI_CDDA_HALF_FRAMES;
    unsigned active=pos/KUI_CDDA_HALF_FRAMES;
    if(before != active) {
        if(!r->ready[active]) return KUI_CDDA_RING_NOT_READY;
        r->ready[before]=false;
    }
    if(advance > UINT32_MAX-r->played) return KUI_CDDA_RING_ARGUMENT;
    r->played+=advance; r->last_tick=now; r->last_position=pos;
    return KUI_CDDA_RING_OK;
}
enum kui_cdda_ring_result kui_cdda_ring_can_fill(struct kui_cdda_ring *r,
                                               unsigned half) {
    if(!r || !r->initialized || half>1) return KUI_CDDA_RING_ARGUMENT;
    unsigned active=r->last_position/KUI_CDDA_HALF_FRAMES;
    if(half==active || r->ready[half]) return KUI_CDDA_RING_NOT_READY;
    uint32_t frames=KUI_CDDA_HALF_FRAMES-r->last_position%KUI_CDDA_HALF_FRAMES;
    /* 12.5 MHz / 44.1 kHz, rounded down, without an overflowing product. */
    uint32_t margin=frames*125000u/441u;
    if(margin<r->min_margin_ticks) r->min_margin_ticks=margin;
    return margin<=KUI_CDDA_MARGIN_TICKS ? KUI_CDDA_RING_DEADLINE : KUI_CDDA_RING_OK;
}
enum kui_cdda_ring_result kui_cdda_ring_commit(struct kui_cdda_ring *r,unsigned half) {
    enum kui_cdda_ring_result result=kui_cdda_ring_can_fill(r,half);
    if(result==KUI_CDDA_RING_OK) r->ready[half]=true;
    return result;
}
