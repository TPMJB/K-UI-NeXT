/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/clock.h"
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int64_t seconds;
static bool read_ok = true;
static unsigned warnings;
static bool read_clock(void *ctx, int64_t *out) {
    assert(ctx == &seconds); *out = seconds; return read_ok;
}
static void warning(const char *format, ...) {
    assert(strstr(format, "1980-01-01")); ++warnings;
}
static void equals(const struct kui_datetime *a, const struct kui_datetime *b) {
    assert(a->year == b->year && a->month == b->month && a->day == b->day &&
        a->hour == b->hour && a->minute == b->minute && a->second == b->second);
}
static void check(int64_t value, struct kui_datetime expected) {
    struct kui_datetime actual; int64_t back;
    assert(kui_clock_from_seconds(value, &actual)); equals(&actual, &expected);
    assert(kui_clock_to_seconds(&expected, &back) && back == value);
}
int main(void) {
    check(INT64_C(315532800), (struct kui_datetime){1980,1,1,0,0,0});
    check(INT64_C(951835245), (struct kui_datetime){2000,2,29,14,40,45});
    check(INT64_C(4354819199), (struct kui_datetime){2107,12,31,23,59,59});
    check(INT64_C(3663815295), (struct kui_datetime){2086,2,6,6,28,15});
    struct kui_datetime value = {2026,9,23,23,59,59};
    assert(kui_clock_to_seconds(&value, &seconds));
    kui_clock_configure(read_clock, &seconds, warning);
    uint32_t expected = 46u << 25 | 9u << 21 | 23u << 16 | 23u << 11 | 59u << 5 | 29u;
    assert(kui_clock_fattime() == expected && warnings == 0);
    /* FAT truncates odd seconds; the clock itself keeps the full second. */
    struct kui_datetime actual; assert(kui_clock_now(&actual)); equals(&value, &actual);
    read_ok = false;
    assert(!kui_clock_now(&actual));
    assert(kui_clock_fattime() == ((1u << 21) | (1u << 16)));
    assert(kui_clock_fattime() == ((1u << 21) | (1u << 16)) && warnings == 1);
    read_ok = true;
    assert(kui_clock_fattime() == expected);
    seconds = INT64_MAX;
    assert(kui_clock_fattime() == ((1u << 21) | (1u << 16)) && warnings == 2);
    kui_clock_configure(NULL, NULL, NULL);
    assert(!kui_clock_now(&actual));
    assert(kui_clock_fattime() == ((1u << 21) | (1u << 16)));
    assert(!kui_clock_from_seconds(INT64_MIN, &actual));
    assert(!kui_clock_from_seconds(INT64_MAX, &actual));
    assert(!kui_clock_from_seconds(315532799, &actual));
    assert(!kui_clock_from_seconds(INT64_C(4354819200), &actual));
    assert(!kui_clock_from_seconds(315532800, NULL));
    assert(!kui_clock_now(NULL));
    assert(!kui_clock_to_seconds(NULL, &seconds));
    assert(!kui_clock_to_seconds(&value, NULL));
    struct kui_datetime bad[] = {
        {1979,12,31,0,0,0}, {2108,1,1,0,0,0}, {2026,0,1,0,0,0},
        {2026,13,1,0,0,0}, {2026,1,0,0,0,0}, {2026,1,32,0,0,0},
        {2026,4,31,0,0,0}, {2026,2,29,0,0,0}, {2100,2,29,0,0,0},
        {2026,1,1,24,0,0}, {2026,1,1,0,60,0}, {2026,1,1,0,0,60}
    };
    for(unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        assert(!kui_datetime_valid(&bad[i]));
        assert(!kui_clock_to_seconds(&bad[i], &seconds));
    }
    /* Every date representable by FAT round-trips, including 2000 and 2100. */
    for(int64_t day = 315532800; day <= INT64_C(4354819199); day += 86400) {
        assert(kui_clock_from_seconds(day, &actual));
        assert(kui_clock_to_seconds(&actual, &seconds) && seconds == day);
    }
    puts("PASS clock: FAT range, leap days, local-time packing, warning/fallback, all dates");
    return 0;
}
