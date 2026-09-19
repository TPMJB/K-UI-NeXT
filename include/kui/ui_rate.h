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
#endif
