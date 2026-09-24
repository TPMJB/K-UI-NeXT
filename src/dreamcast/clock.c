/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/clock_platform.h"
#include <arch/rtc.h>
#include <time.h>

/* Pinned KOS include/kos/rtc.h specifies local wall time. time() uses its
 * boot-time RTC sample plus the elapsed timer, avoiding G2 reads per f_sync. */
static bool read_local(void *ctx, int64_t *seconds) {
    (void)ctx;
    time_t now = time(NULL);
    if(now == (time_t)-1) return false;
    *seconds = (int64_t)now;
    /* The Dreamcast's 32-bit counter begins in 1950. Values beyond the last
     * representable instant are not a valid console RTC, even if FAT fits. */
    return *seconds >= INT64_C(315532800) && *seconds <= INT64_C(3663815295);
}
void kui_clock_start(kui_clock_log_fn log) {
    kui_clock_configure(read_local, NULL, log);
    struct kui_datetime now;
    if(log && kui_clock_now(&now))
        log("Clock: %04u-%02u-%02u %02u:%02u:%02u local (RTC); new file dates follow it",
            (unsigned)now.year, (unsigned)now.month, (unsigned)now.day,
            (unsigned)now.hour, (unsigned)now.minute, (unsigned)now.second);
    else if(log)
        log("CLOCK WARNING: RTC invalid; check Settings clock. New file dates use 1980-01-01");
}
bool kui_clock_set_local(const struct kui_datetime *value) {
    int64_t seconds;
    if(!kui_clock_to_seconds(value, &seconds) || seconds > INT64_C(3663815295)) return false;
    time_t requested = (time_t)seconds;
    if((int64_t)requested != seconds || rtc_set_unix_secs(requested) != 0) return false;
    /* KOS also updates its cached boot time. Check that both the RTC and the
     * timestamp source agree, allowing one second for a rollover while setting. */
    int64_t actual = (int64_t)rtc_unix_secs(), cached;
    return actual >= seconds && actual <= seconds + 1 && read_local(NULL, &cached) &&
        cached >= seconds && cached <= seconds + 1;
}
