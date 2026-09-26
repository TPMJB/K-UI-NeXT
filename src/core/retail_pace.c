/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_pace.h"

void kui_retail_pace_sample(struct kui_retail_pace *p, uint32_t status,
                            uint32_t vblank, uint32_t fb) {
    struct kui_retail_pace_phase *q = &p->phase;
    uint32_t line = status & 0x3ffu, vbi = vblank & 0x3ffu;
    /* The counter only increases within a frame, so a smaller value means
     * at least one wrap. Samples a whole frame apart can hide one: counts
     * are then low, which only delays pacing. */
    if(line < p->line) {
        ++p->frames; ++p->still; ++q->frames[q->slot];
        if(++q->count == KUI_RETAIL_PACE_TRIAL_FRAMES) { q->count = 0; q->slot ^= 1u; }
    }
    if(fb != p->fb) {
        if(p->still >= KUI_RETAIL_PACE_STILL_FRAMES)
            for(uint32_t *w = (uint32_t *)q; w < (uint32_t *)(q + 1); ++w) *w = 0;
        p->fb = fb; p->still = 0; ++q->flips[q->slot];
    }
    if(vbi != p->vbi) { p->vbi = vbi; p->top = 0; p->per = 0; }
    if(line > p->top) p->top = line;
    p->line = line;
}

/* Frame length in scanlines. Until a sample at or after the vblank line is
 * seen, vbi+1 underestimates it, which only shortens a step. Standard modes
 * wrap within a few lines of vblank-in; the cap keeps one stray reading (for
 * example during a mode change) from inflating every later budget. */
static uint32_t frame_lines(const struct kui_retail_pace *p) {
    uint32_t cap = p->vbi + p->vbi / 8u, top = p->top > cap ? cap : p->top;
    return (top > p->vbi ? top : p->vbi) + 1u;
}

/* Scanlines from line to the next vblank-in; line must be below frame. */
static uint32_t to_vblank(const struct kui_retail_pace *p, uint32_t line, uint32_t frame) {
    return line < p->vbi ? p->vbi - line : frame - line + p->vbi;
}

uint32_t kui_retail_pace_budget(const struct kui_retail_pace *p, uint32_t normal,
                                uint32_t maximum, int spin) {
    uint32_t line = p->line, frame = frame_lines(p);
    int still = p->still >= KUI_RETAIL_PACE_STILL_FRAMES;
    if(!still) {
        if(!p->phase.slot) normal = 1u;
        if(!spin) return normal;
        maximum = normal;
    }
    if(!p->per || p->vbi < 64u || line >= frame) return still ? normal : 0u;
    /* On a still screen, up to the second vblank-in from now; otherwise the
     * next. Less an eighth of a frame for card latency and the game's own
     * handler. */
    uint32_t left = to_vblank(p, line, frame) + (still ? frame : 0u), guard = frame / 8u;
    left = left > guard ? left - guard : 0u;
    uint32_t n = still ? normal : 0u;
    while(n < maximum && (n + 1u) * p->per <= left * 16u) ++n;
    return n;
}

void kui_retail_pace_measure(struct kui_retail_pace *p, uint32_t frames,
                             uint32_t line, uint32_t sectors, int spin) {
    struct kui_retail_pace_phase *q = &p->phase;
    if(!sectors) return;
    ++q->steps; q->sectors[q->slot] += sectors;
    if(spin) ++q->spins;
    uint32_t wraps = p->frames - frames, frame = frame_lines(p);
    if(wraps > 3u || (!wraps && p->line < line) || line >= frame) return;
    uint32_t elapsed = wraps * frame + p->line - line;
    /* A step that ran past the next vblank-in delayed the game's frame. */
    if(elapsed >= to_vblank(p, line, frame)) ++q->crossings;
    uint32_t per = elapsed * 16u / sectors;
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
