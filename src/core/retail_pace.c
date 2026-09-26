/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_pace.h"

void kui_retail_pace_sample(struct kui_retail_pace *p, uint32_t status,
                            uint32_t vblank, uint32_t fb) {
    uint32_t line = status & 0x3ffu, vbi = vblank & 0x3ffu;
    /* The counter only increases within a frame, so a smaller value means
     * at least one wrap. Samples a whole frame apart can hide one: counts
     * are then low, which only delays pacing. */
    if(line < p->line) { ++p->frames; ++p->still; }
    if(fb != p->fb) { p->fb = fb; p->still = 0; }
    if(vbi != p->vbi) { p->vbi = vbi; p->top = 0; p->per = 0; }
    if(line > p->top) p->top = line;
    p->line = line;
}

/* Frame length in scanlines. Until a sample at or after the vblank line is
 * seen, vbi+1 underestimates it, which only shortens a step. */
static uint32_t frame_lines(const struct kui_retail_pace *p) {
    return (p->top > p->vbi ? p->top : p->vbi) + 1u;
}

uint32_t kui_retail_pace_budget(const struct kui_retail_pace *p, uint32_t normal,
                                uint32_t maximum) {
    uint32_t vbi = p->vbi, line = p->line, frame = frame_lines(p);
    if(p->still < KUI_RETAIL_PACE_STILL_FRAMES || !p->per || vbi < 64u)
        return normal;
    /* Scanlines until the second vblank-in from now, less an eighth of a
     * frame for card latency and the game's own handler. */
    uint32_t left = (line < vbi ? vbi - line : frame - line + vbi) + frame - frame / 8u;
    uint32_t n = normal;
    while(n < maximum && (n + 1u) * p->per <= left * 16u) ++n;
    return n;
}

void kui_retail_pace_measure(struct kui_retail_pace *p, uint32_t frames,
                             uint32_t line, uint32_t sectors) {
    uint32_t wraps = p->frames - frames;
    if(!sectors || wraps > 3u || (!wraps && p->line < line)) return;
    uint32_t per = (wraps * frame_lines(p) + p->line - line) * 16u / sectors;
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
