/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_clock.h"

/* Split before multiplying. For the fixed reduced ratios below, every
 * remainder product fits uint32_t (the largest is 1948225000). Detect both
 * whole-part overflow and overflow when adding the fractional part. */
static bool scale_u32(uint32_t value, uint32_t numerator, uint32_t denominator,
                      bool ceil, uint32_t *result) {
    if(!result) return false;
    uint32_t whole=value/denominator;
    uint32_t remainder=value%denominator;
    if(whole>UINT32_MAX/numerator) return false;
    uint32_t base=whole*numerator;
    uint32_t product=remainder*numerator;
    uint32_t fraction=product/denominator;
    if(ceil && product%denominator) ++fraction;
    if(fraction>UINT32_MAX-base) return false;
    *result=base+fraction;
    return true;
}

bool kui_cdda_ticks_to_us(uint32_t ticks, uint32_t *microseconds) {
    /* 1000000/12468720 = 12500/155859. */
    return scale_u32(ticks,12500u,155859u,false,microseconds);
}
bool kui_cdda_ms_to_ticks(uint32_t milliseconds, uint32_t *ticks) {
    /* 12468720/1000 = 311718/25. */
    return scale_u32(milliseconds,311718u,25u,true,ticks);
}
bool kui_cdda_ticks_to_frames(uint32_t ticks, uint32_t *frames) {
    /* 44100/12468720 = 735/207812. */
    return scale_u32(ticks,735u,207812u,false,frames);
}
bool kui_cdda_frames_to_ticks(uint32_t frames, uint32_t *ticks) {
    /* 12468720/44100 = 207812/735. */
    return scale_u32(frames,207812u,735u,false,ticks);
}
bool kui_cdda_frames_to_ticks_ceil(uint32_t frames, uint32_t *ticks) {
    return scale_u32(frames,207812u,735u,true,ticks);
}
