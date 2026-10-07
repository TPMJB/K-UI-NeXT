/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_CLOCK_H
#define KUI_CDDA_CLOCK_H
#include <stdbool.h>
#include <stdint.h>

/* Fixed reference from KallistiOS timer.c, commit
 * fcfa7d869471591ca1c777543261a7bfea7cb726: measured CPU 199499520 Hz,
 * PCLK = CPU/4, and the owned TMU's TCR.TPSC=0 selects PCLK/4.
 * This reference is not an absolute clock measurement of the running console.
 * Audio pitch and clock configuration remain unchanged. */
#define KUI_CDDA_TMU_HZ 12468720u
#define KUI_CDDA_SAMPLE_HZ 44100u

/* Bounded 32-bit integer conversions, with no floating-point or 64-bit runtime
 * arithmetic. Floor conversions never overstate elapsed time or remaining
 * playback time. Ceil conversions never shorten a requested delay/budget.
 * False means a null output or a result exceeding UINT32_MAX; the output is
 * unchanged on failure. Inputs are durations, not absolute wrapping timestamps.
 * Obtain a tick duration by unsigned subtraction of two TMU observations, with
 * observations less than one complete 32-bit counter period apart. */
bool kui_cdda_ticks_to_us(uint32_t ticks, uint32_t *microseconds);
bool kui_cdda_ms_to_ticks(uint32_t milliseconds, uint32_t *ticks);
bool kui_cdda_ticks_to_frames(uint32_t ticks, uint32_t *frames);
bool kui_cdda_frames_to_ticks(uint32_t frames, uint32_t *ticks);
bool kui_cdda_frames_to_ticks_ceil(uint32_t frames, uint32_t *ticks);
#endif
