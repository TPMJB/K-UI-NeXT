/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/clock_platform.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

static time_t cached = 1790208000, rtc = 1790208000;
static unsigned writes, logs;
static int failure;
time_t time(time_t *out) { if(out) *out = cached; return cached; }
time_t rtc_unix_secs(void) { return rtc; }
int rtc_set_unix_secs(time_t value) {
    ++writes;
    if(failure == 1) return -1;
    rtc = value + (failure == 2 ? 2 : 0);
    cached = value + (failure == 3 ? 2 : 0);
    return 0;
}
static void log_line(const char *format, ...) { (void)format; ++logs; }
int main(void) {
    kui_clock_start(log_line);
    struct kui_datetime value;
    assert(kui_clock_now(&value) && writes == 0 && logs == 1);
    assert(value.year == 2026 && value.month == 9 && value.day == 24);
    value = (struct kui_datetime){2026,9,23,17,42,0};
    assert(kui_clock_set_local(&value) && writes == 1);
    assert(kui_clock_now(&value) && value.hour == 17 && value.minute == 42);
    failure = 1; assert(!kui_clock_set_local(&value));
    failure = 2; assert(!kui_clock_set_local(&value));
    failure = 3; assert(!kui_clock_set_local(&value));
    unsigned before = writes;
    assert(!kui_clock_set_local(NULL));
    value = (struct kui_datetime){2086,2,6,6,28,16};
    assert(!kui_clock_set_local(&value) && before == writes);
    value = (struct kui_datetime){1970,1,1,0,0,0};
    assert(!kui_clock_set_local(&value) && before == writes);
    failure = 0; value = (struct kui_datetime){2086,2,6,6,28,15};
    assert(kui_clock_set_local(&value));
    cached = (time_t)-1;
    assert(!kui_clock_now(&value));
    kui_clock_start(log_line);
    assert(logs == 2);
    cached = 3663815296LL;
    assert(!kui_clock_now(&value));
    puts("PASS console clock: read-only startup, explicit setter, RTC bounds, readback failures");
    return 0;
}
