/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CLOCK_PLATFORM_H
#define KUI_CLOCK_PLATFORM_H
#include "kui/clock.h"

/* Installs the read-only cached KOS system clock for FAT and reports its value.
 * Call after logging is ready, before loading settings or any other SD writes. */
void kui_clock_start(kui_clock_log_fn log);
/* Only call after explicit user confirmation. Sets and validates the actual
 * hardware RTC; false preserves an error outcome, never reports success. */
bool kui_clock_set_local(const struct kui_datetime *value);
#endif
