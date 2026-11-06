/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_VMU_STAGE_CLOCK_H
#define KUI_VC2_CE_VMU_STAGE_CLOCK_H
#include <stdbool.h>
#include <stdint.h>
/* Cold-stage diagnostic only. An idle, flag-clear TMU2 may be borrowed with
 * IRQs masked. No active timer, input capture, priority or other channel is
 * changed. Failure to borrow a clock never grants storage authority. */
struct kui_vc2_ce_vmu_stage_clock {
    uint32_t active,valid,constant,counter;
    uint16_t control;
};
bool kui_vc2_ce_vmu_stage_clock_start(struct kui_vc2_ce_vmu_stage_clock *);
uint32_t kui_vc2_ce_vmu_stage_clock_ticks(struct kui_vc2_ce_vmu_stage_clock *);
bool kui_vc2_ce_vmu_stage_clock_stop(struct kui_vc2_ce_vmu_stage_clock *);
/* PCLK is the pinned KOS Dreamcast clock 199499520/4 Hz; TMU divider 64.
 * Unsigned deltas remain unambiguous for the admitted <~91 minute interval. */
uint32_t kui_vc2_ce_vmu_stage_clock_ms(uint32_t ticks);
#endif
