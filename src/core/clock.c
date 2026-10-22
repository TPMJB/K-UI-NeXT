/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/clock.h"
#include <stddef.h>

static kui_clock_read_fn clock_read;
static void *clock_ctx;
static kui_clock_log_fn clock_log;
static bool warned;

static bool leap(unsigned year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
static unsigned month_days(unsigned year, unsigned month) {
    static const uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1] + (month == 2 && leap(year));
}
bool kui_datetime_valid(const struct kui_datetime *v) {
    return v && v->year >= 1980 && v->year <= 2107 && v->month >= 1 && v->month <= 12 &&
        v->day >= 1 && v->day <= month_days(v->year, v->month) &&
        v->hour <= 23 && v->minute <= 59 && v->second <= 59;
}
bool kui_clock_from_seconds(int64_t seconds, struct kui_datetime *out) {
    /* Inclusive FAT date range. Check before division/casts to avoid wraparound. */
    if(!out || seconds < INT64_C(315532800) || seconds > INT64_C(4354819199)) return false;
    uint32_t days = (uint32_t)(seconds / 86400);
    unsigned year = 1970, month = 1;
    while(days >= 365u + leap(year)) { days -= 365u + leap(year); ++year; }
    while(days >= month_days(year, month)) { days -= month_days(year, month); ++month; }
    uint32_t within = (uint32_t)(seconds % 86400);
    *out = (struct kui_datetime){(uint16_t)year, (uint8_t)month, (uint8_t)(days + 1),
        (uint8_t)(within / 3600), (uint8_t)(within / 60 % 60), (uint8_t)(within % 60)};
    return true;
}
bool kui_clock_to_seconds(const struct kui_datetime *v, int64_t *out) {
    if(!out || !kui_datetime_valid(v)) return false;
    uint32_t days = 0;
    for(unsigned year = 1970; year < v->year; ++year) days += 365u + leap(year);
    for(unsigned month = 1; month < v->month; ++month) days += month_days(v->year, month);
    days += v->day - 1u;
    *out = (int64_t)days * 86400 + v->hour * 3600 + v->minute * 60 + v->second;
    return true;
}
void kui_clock_configure(kui_clock_read_fn read, void *ctx, kui_clock_log_fn log) {
    clock_read = read; clock_ctx = ctx; clock_log = log; warned = false;
}
bool kui_clock_now(struct kui_datetime *out) {
    int64_t seconds;
    return out && clock_read && clock_read(clock_ctx, &seconds) &&
        kui_clock_from_seconds(seconds, out);
}
uint32_t kui_clock_fattime(void) {
    struct kui_datetime v;
    if(!kui_clock_now(&v)) {
        v = (struct kui_datetime){1980,1,1,0,0,0};
        if(!warned && clock_log)
            clock_log("CLOCK WARNING: RTC unavailable/out of range; new file dates use 1980-01-01");
        warned = true;
    } else warned = false;
    return ((uint32_t)v.year - 1980u) << 25 | (uint32_t)v.month << 21 |
        (uint32_t)v.day << 16 | (uint32_t)v.hour << 11 |
        (uint32_t)v.minute << 5 | (uint32_t)v.second / 2;
}
