/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_timing.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static unsigned checks;
#define CHECK(expr) do { ++checks; assert(expr); } while (0)

static void check_measure(uint32_t base, uint32_t elapsed,
                          uint32_t first_width, uint32_t last_width,
                          uint32_t frames)
{
    struct kui_cdda_timing_snapshot start = {base, base + first_width, 71u};
    struct kui_cdda_timing_snapshot end = {
        base + elapsed, base + elapsed + last_width, 71u + frames
    };
    struct kui_cdda_timing_result result = {0};
    CHECK(kui_cdda_timing_measure(&start, &end, 100000u, 752600000u, &result));
    CHECK(result.played_frames == frames);
    CHECK(result.lower_ticks == elapsed - first_width);
    CHECK(result.upper_ticks == elapsed + last_width);
    CHECK(result.start_read_ticks == first_width);
    CHECK(result.end_read_ticks == last_width);
}

int main(void)
{
    /* Exact-rate, relative offset and unequal read latencies. A timer wrap is
     * only a representation boundary and must not change the measurement. */
    check_measure(10u, 750000000u, 3000u, 7000u, 2646000u);
    check_measure(10u, 750000000u, 3000u, 7000u, 2651821u);
    check_measure(UINT32_MAX - 100000000u, 750000000u, 12000u, 1u, 2646000u);
    check_measure(UINT32_MAX - 2u, 750000000u, 3u, 0u, 2646000u);

    struct kui_cdda_timing_snapshot start = {100u, 120u, 7u};
    struct kui_cdda_timing_snapshot end = {1100u, 1120u, 12u};
    const struct kui_cdda_timing_result sentinel = {1u, 2u, 3u, 4u, 5u};
    struct kui_cdda_timing_result result = sentinel;
    CHECK(!kui_cdda_timing_measure(NULL, &end, 30u, 2000u, &result));
    CHECK(!kui_cdda_timing_measure(&start, NULL, 30u, 2000u, &result));
    CHECK(!kui_cdda_timing_measure(&start, &end, 30u, 2000u, NULL));
    CHECK(!kui_cdda_timing_measure(&start, &end, 0u, 2000u, &result));
    CHECK(!kui_cdda_timing_measure(&start, &end, 30u, 0u, &result));
    CHECK(!kui_cdda_timing_measure(&start, &end, 30u, UINT32_C(0x80000000), &result));
    CHECK(!kui_cdda_timing_measure(&start, &end, 2001u, 2000u, &result));
    CHECK(!kui_cdda_timing_measure(&start, &end, 19u, 2000u, &result));
    CHECK(!kui_cdda_timing_measure(&start, &end, 30u, 1019u, &result));
    end.played_frames = start.played_frames;
    CHECK(!kui_cdda_timing_measure(&start, &end, 30u, 2000u, &result));
    end.played_frames = start.played_frames - 1u;
    CHECK(!kui_cdda_timing_measure(&start, &end, 30u, 2000u, &result));
    end = (struct kui_cdda_timing_snapshot){120u, 140u, 12u};
    CHECK(!kui_cdda_timing_measure(&start, &end, 30u, 2000u, &result));
    end = (struct kui_cdda_timing_snapshot){119u, 130u, 12u};
    CHECK(!kui_cdda_timing_measure(&start, &end, 30u, 2000u, &result));
    end = (struct kui_cdda_timing_snapshot){90u, 110u, 12u};
    CHECK(!kui_cdda_timing_measure(&start, &end, 30u, 2000u, &result));
    CHECK(result.played_frames == sentinel.played_frames &&
          result.lower_ticks == sentinel.lower_ticks &&
          result.upper_ticks == sentinel.upper_ticks &&
          result.start_read_ticks == sentinel.start_read_ticks &&
          result.end_read_ticks == sentinel.end_read_ticks);
    puts("CDDA paired timing bounds passed.");
    return 0;
}
