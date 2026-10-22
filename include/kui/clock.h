/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CLOCK_H
#define KUI_CLOCK_H
#include <stdbool.h>
#include <stdint.h>

/* Local wall time, as stored by the Dreamcast RTC and by FAT. Never apply a
 * second timezone conversion to KOS's local-time Unix-style timestamp. */
struct kui_datetime {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
};
typedef bool (*kui_clock_read_fn)(void *ctx, int64_t *local_seconds);
typedef void (*kui_clock_log_fn)(const char *format, ...);

/* Configure once before filesystem use. A missing source uses the deterministic
 * FAT minimum (1980-01-01), which is also the explicitly reported error fallback. */
void kui_clock_configure(kui_clock_read_fn read, void *ctx, kui_clock_log_fn log);
bool kui_datetime_valid(const struct kui_datetime *value);
bool kui_clock_from_seconds(int64_t local_seconds, struct kui_datetime *out);
bool kui_clock_to_seconds(const struct kui_datetime *value, int64_t *out);
bool kui_clock_now(struct kui_datetime *out);
uint32_t kui_clock_fattime(void);
#endif
