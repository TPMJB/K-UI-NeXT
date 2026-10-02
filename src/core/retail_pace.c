/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_pace.h"

void kui_retail_pace_sample(struct kui_retail_pace *p, uint32_t status,
                            uint32_t vblank, uint32_t load, uint32_t fb) {
    uint32_t line = status & 0x3ffu, vbi = vblank & 0x3ffu;
    uint32_t period = ((load >> 16) & 0x3ffu) + 1u;
    /* Vblank-in is an interrupt position, not the counter period: interlaced
     * modes can have vbi=260 with a 525- or 625-tick counter. Invalidate any
     * measurement spanning changed or inconsistent geometry. */
    if(period != p->period || vbi != p->vbi || line >= period ||
       p->line >= period || period < 64u || vbi >= period) {
        ++p->epoch;
        p->per = p->still = p->spin = 0;
    /* Count only wraps within unchanged, valid geometry. Samples a whole
     * period apart can hide wraps; reads therefore sample every SD block. */
    } else if(line < p->line) { ++p->frames; ++p->still; }
    if(fb != p->fb) { p->fb = fb; p->still = 0; }
    p->period = period;
    p->vbi = vbi;
    p->line = line;
}

uint32_t kui_retail_pace_budget(const struct kui_retail_pace *p, uint32_t normal,
                                uint32_t maximum) {
    uint32_t vbi = p->vbi, line = p->line, frame = p->period;
    if(!p->per || frame < 64u || vbi >= frame || line >= frame)
        return normal;
    /* Moving buffers do not prove the game is busy: loading screens may
     * flip too. Permit a small batch when its measured cost fits half a
     * frame, capped at four sectors. This bounds the predicted duration,
     * not the next-vblank crossing or an unexpected card stall. The normal
     * step remains the floor even when it takes longer than this allowance. */
    uint32_t left = frame / 2u;
    if(p->still < KUI_RETAIL_PACE_STILL_FRAMES) {
        if(maximum > 4u) maximum = 4u;
    } else {
        /* Still screen: retain the second-vblank allowance, less an eighth
         * of a frame for card latency and the game's own handler. */
        left = (line < vbi ? vbi - line : frame - line + vbi) + frame - frame / 8u;
    }
    uint32_t n = normal;
    while(n < maximum && (n + 1u) * p->per <= left * 16u) ++n;
    return n;
}

void kui_retail_pace_measure(struct kui_retail_pace *p, uint32_t frames,
                             uint32_t line, uint32_t epoch, uint32_t sectors) {
    uint32_t wraps = p->frames - frames;
    if(!sectors || epoch != p->epoch || wraps > 3u || line >= p->period ||
       p->line >= p->period || p->period < 64u || p->vbi >= p->period ||
       (!wraps && p->line < line)) return;
    /* Each line is below the actual period (at most 1024). With at most
     * three wraps this subtraction cannot underflow or overflow. */
    uint32_t per = (wraps * p->period + p->line - line) * 16u / sectors;
    /* Follow slower steps at once and faster ones gradually. */
    p->per = per >= p->per ? per : p->per - (p->per - per) / 8u;
}

int kui_retail_pace_spin(struct kui_retail_pace *p) {
    int near = p->frames == p->spin_frames && p->line - p->spin_line <= 1u;
    p->spin = near ? p->spin + 1u : 1u;
    p->spin_frames = p->frames;
    p->spin_line = p->line;
    if(p->spin < KUI_RETAIL_PACE_SPIN_CALLS) return 0;
    p->spin = 0;
    return 1;
}
