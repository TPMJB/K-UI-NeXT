/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_RING_H
#define KUI_TOY_PILOT_RING_H
#include <stdbool.h>
#include <stdint.h>

#define KUI_TOY_RING_FRAMES 32768u
#define KUI_TOY_RING_HALF 16384u
#define KUI_TOY_RING_BLOCK 4096u
#define KUI_TOY_RING_BLOCKS 8u
#define KUI_TOY_RING_PROBE_TICKS 391u /* 0.5ms; no unbounded ARM polling. */
#define KUI_TOY_RING_PROBE_READS 16u
#define KUI_TOY_RING_FRESH_TICKS 39063u /* 50ms cooperative publication window. */
#define KUI_TOY_RING_RESERVE_TICKS 3907u /* 5ms: two 2ms G2 scopes + overhead. */
#define KUI_TOY_RING_PHASE_FRAMES 64u

static inline uint32_t kui_toy_ring_ticks(uint32_t frames) {
    return frames*15625u/882u; /* floor: an early ownership deadline */
}
static inline uint32_t kui_toy_ring_frames(uint32_t ticks) {
    return (ticks*882u+15624u)/15625u; /* ceil; callers bound ticks to one ring */
}
struct kui_toy_ring_probe {
    uint32_t previous[2],changes[2],began;
    uint32_t last_seen[2],prior_seen[2],capture_bound[2];
};
static inline void kui_toy_ring_probe_begin(struct kui_toy_ring_probe *p,
    uint32_t left,uint32_t right,uint32_t now) {
    *p=(struct kui_toy_ring_probe){.previous={left,right},.began=now,
        .last_seen={now,now}};
}
/* A single changed publication may contain a hardware read delayed by ARM
 * FIQ. Two changes after our baseline prove the LAST hardware read followed
 * the first publication, hence occurred within this bounded probe. Retain
 * each channel's last observation of the value TWO changes back: the next
 * publication followed that read, and the current capture followed that
 * publication. Times precede BOTH bus reads; observing the intervening
 * changed value is too late to bound an already-captured delayed update. START /
 * generation changes must be excluded by the adapter around this evidence. */
static inline bool kui_toy_ring_probe_observe(struct kui_toy_ring_probe *p,
    uint32_t left,uint32_t right,uint32_t before,uint32_t now) {
    uint32_t values[2]={left,right};
    if(left>=KUI_TOY_RING_FRAMES || right>=KUI_TOY_RING_FRAMES ||
       now-p->began>KUI_TOY_RING_FRESH_TICKS ||
       before-p->began>now-p->began) return false;
    for(unsigned i=0;i<2u;i++) if(values[i]!=p->previous[i]) {
        if(p->changes[i]) p->capture_bound[i]=p->prior_seen[i];
        p->prior_seen[i]=p->last_seen[i];
        if(p->changes[i]<2u) ++p->changes[i];
        p->previous[i]=values[i];
    }
    p->last_seen[0]=p->last_seen[1]=before;
    uint32_t distance=left>right?left-right:right-left;
    return p->changes[0]==2u && p->changes[1]==2u &&
        left/KUI_TOY_RING_HALF==right/KUI_TOY_RING_HALF &&
        distance<=KUI_TOY_RING_PHASE_FRAMES;
}
/* Only after acceptance: use the older independent capture bound. Unsigned
 * ages retain ordering across timer wrap inside the unchanged 50ms probe. */
static inline uint32_t kui_toy_ring_probe_age(const struct kui_toy_ring_probe *p,
    uint32_t now) {
    uint32_t left=now-p->capture_bound[0],right=now-p->capture_bound[1];
    return left>right?left:right;
}
#endif
