/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_UI_RATE_H
#define KUI_UI_RATE_H
#include "kui/options.h"
#include <stdbool.h>
#include <stdint.h>

/* Should the UI thread redraw on this loop iteration? Pure, so the PC can test
 * it; main.c calls it once per loop.
 *
 * Idle, unthrottled (KUI_OPT_UI_FULL) and the first iteration after an
 * operation starts or ends always redraw. Otherwise it redraws when 1/hz
 * seconds have passed since the last draw; hz == 0 never does. It is keyed on
 * the time of the last draw, not on a deadline computed when that draw
 * happened, so a cap that changes mid-operation applies at once. (The bench
 * changes it between passes; a precomputed deadline would leave the screen
 * frozen for a whole pass after going from 'full' to a finite rate.) */
static inline bool kui_ui_redraw_due(bool busy, bool was_busy, unsigned hz,
                                     uint64_t now_ms, uint64_t last_draw_ms) {
    if(!busy || hz == KUI_OPT_UI_FULL || busy != was_busy) return true;
    return hz && now_ms - last_draw_ms >= 1000u / hz;
}
/* Every idle shell page polls independently of rendering. Fresh input and
 * published worker results render on this iteration; otherwise 30 Hz is
 * sufficient for clocks/music/status without repainting between every poll.
 * Busy capture/benchmark caps retain their original authority, including 0. */
static inline bool kui_ui_menu_redraw_due(bool busy, bool was_busy, unsigned hz,
        bool changed, uint64_t now_ms, uint64_t last_draw_ms) {
    if(busy) return kui_ui_redraw_due(busy,was_busy,hz,now_ms,last_draw_ms);
    return changed || was_busy || !last_draw_ms || now_ms-last_draw_ms>=33u;
}
static inline unsigned kui_ui_menu_poll_delay(bool busy) {
    return busy?33u:8u;
}
/* A completed idle frame has already spent time drawing and waiting for
 * vertical blank. Yield so other threads run, then sample controls again;
 * do not add another 8 ms before observing a press that arrived during draw.
 * A non-drawing poll still sleeps, and operation pacing stays unchanged. */
static inline unsigned kui_ui_menu_wait_ms(bool busy,bool drew) {
    return !busy && drew?0u:kui_ui_menu_poll_delay(busy);
}
/* held contains only navigation bits; action buttons are never repeated.
 * An initial edge is already in pressed. Direction changes reset the hold
 * timer, while a late loop emits one step rather than a burst of catch-up. */
static inline unsigned kui_ui_navigation_repeat(unsigned held,unsigned pressed,
        unsigned *previous_held,uint64_t *repeat_at,uint64_t now_ms) {
    if(held!=*previous_held) {
        *previous_held=held;*repeat_at=now_ms+250u;
    } else if(held && now_ms>=*repeat_at) {
        pressed|=held;*repeat_at=now_ms+80u;
    }
    return pressed;
}
/* Games polls controls faster while idle without repainting the whole screen
 * on every poll. Input and completed listings/details appear immediately;
 * operation caps and their start/end edges keep the shared rule above. */
static inline bool kui_ui_games_redraw_due(bool games, bool busy, bool was_busy,
        unsigned hz, bool changed, uint64_t now_ms, uint64_t last_draw_ms) {
    if(!games || busy) return kui_ui_redraw_due(busy,was_busy,hz,now_ms,last_draw_ms);
    return kui_ui_menu_redraw_due(busy,was_busy,hz,changed,now_ms,last_draw_ms);
}
static inline unsigned kui_ui_poll_delay(bool games, bool busy) {
    return games && !busy ? 8u : 33u;
}
#endif
