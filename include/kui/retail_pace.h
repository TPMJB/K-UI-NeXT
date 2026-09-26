/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_PACE_H
#define KUI_RETAIL_PACE_H
#include <stdint.h>

/* Read pacing for the retail GD service. The SD reader is synchronous: a
 * game that calls EXEC once per video frame gets one step per frame, however
 * idle it is. Once the displayed framebuffer has not changed for STILL_FRAMES
 * scanline wraps (a static or black loading screen), a step may continue
 * until shortly before the second vertical-blank interrupt from now. Crossing
 * one vblank delays that interrupt but does not lose it, so a step made in the
 * game's own vblank handler reads for about two frames' worth of time.
 *
 * While the game presents new frames, each step costs the game frame time.
 * This diagnostic build alternates the in-play step between one sector and
 * the normal size every TRIAL_FRAMES frames and counts, for each, frames,
 * framebuffer flips (frames the game presented) and sectors read.
 *
 * Inputs are raw PowerVR register values: SPG_STATUS (scanline in bits 9:0),
 * SPG_VBLANK_INT (vblank-in line in bits 9:0) and FB_R_SOF1. Only the
 * counter's wraps and positions are used; no mode table or timer is needed.
 * A game calling CHECK in a tight loop with no EXEC in between is waiting on
 * the read; SPIN_CALLS such calls within one scanline permit one step there.
 * While the picture moves, that step must also end before the next vblank. */
#define KUI_RETAIL_PACE_STILL_FRAMES 12u
#define KUI_RETAIL_PACE_SPIN_CALLS 16u
#define KUI_RETAIL_PACE_TRIAL_FRAMES 120u

/* Counters since the picture last started moving after a still screen: in
 * DOA2, since a fight began. Per trial slot (0: one-sector steps, 1: normal
 * steps): frames, framebuffer flips and sectors read. */
struct kui_retail_pace_phase {
    uint32_t slot, count; /* current trial slot and frames spent in it */
    uint32_t frames[2], flips[2], sectors[2];
    uint32_t execs, steps, spins, crossings;
};
struct kui_retail_pace {
    uint32_t line, top, vbi, fb, frames, still;
    uint32_t per; /* Scanlines per sector, times 16; zero until measured. */
    uint32_t spin, spin_line, spin_frames;
    struct kui_retail_pace_phase phase;
};
/* Record one register sample. Call it at every hook entry and during reads
 * (at least once per frame of work) so scanline wraps are counted. */
void kui_retail_pace_sample(struct kui_retail_pace *, uint32_t status,
    uint32_t vblank, uint32_t fb);
/* Sectors the next step may read, never more than maximum. On a still screen
 * with a measured step time, fill toward the second vblank from normal
 * upward. While the picture moves, an EXEC gets its trial size (one sector
 * or normal), and a spin step gets what fits before the next vblank, which
 * may be zero (skip it). */
uint32_t kui_retail_pace_budget(const struct kui_retail_pace *, uint32_t normal,
    uint32_t maximum, int spin);
/* A step that began at (frames, line) and read sectors has just finished;
 * the latest sample is its end. spin marks a step run for a spinning CHECK. */
void kui_retail_pace_measure(struct kui_retail_pace *, uint32_t frames,
    uint32_t line, uint32_t sectors, int spin);
/* Call on each CHECK while a read is pending, after sampling. Returns nonzero
 * when the game is spinning on CHECK; the caller then asks for a spin budget.
 * The caller clears spin on every EXEC. */
int kui_retail_pace_spin(struct kui_retail_pace *);
#endif
