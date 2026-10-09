/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_stream.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static unsigned checks;
#define CHECK(expr) do { ++checks; assert(expr); } while (0)

static uint32_t random_state = 0x514dcda1u;
static uint32_t random_u32(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

static void check_cursor(uint32_t first, uint32_t end, bool repeat,
                         uint32_t played)
{
    uint32_t frame = 123u, loops = 456u;
    bool valid = first < end;
    uint64_t span = valid ? (uint64_t)end - first : 0;
    if (valid && !repeat && played > span)
        valid = false;
    CHECK(kui_cdda_stream_cursor(first, end, repeat, played, &frame, &loops)
          == valid);
    if (valid) {
        CHECK(frame == (uint64_t)first + (repeat ? played % span : played));
        CHECK(loops == (repeat ? played / span : 0));
        CHECK(frame <= end);
        if (repeat)
            CHECK(frame < end);
    } else {
        CHECK(frame == 123u && loops == 456u);
    }
}

static void test_cursor(void)
{
    static const uint32_t bounds[] = {
        0, 1, 2, 32767, 32768, 65535, 65536, UINT32_MAX / 2,
        UINT32_MAX / 2 + 1, UINT32_MAX - 1, UINT32_MAX
    };
    struct kui_cdda_stream stream = {0};
    uint32_t frame = 123u, loops = 456u;
    size_t i, j, k;

    CHECK(!kui_cdda_stream_init(NULL, 0, 1, false));
    CHECK(!kui_cdda_stream_position(NULL, 0, &frame, &loops));
    CHECK(!kui_cdda_stream_position(&stream, 0, &frame, &loops));
    CHECK(frame == 123u && loops == 456u);
    CHECK(kui_cdda_stream_init(&stream, 27, 100, true));
    CHECK(!kui_cdda_stream_init(&stream, 100, 100, false));
    CHECK(stream.first_frame == 27 && stream.end_frame == 100 && stream.repeat);
    CHECK(kui_cdda_stream_position(&stream, 146, &frame, &loops));
    CHECK(frame == 27 && loops == 2);
    CHECK(kui_cdda_stream_position(&stream, 147, &frame, &loops));
    CHECK(frame == 28 && loops == 2);
    CHECK(!kui_cdda_stream_cursor(0, 1, false, 0, NULL, &loops));
    CHECK(!kui_cdda_stream_cursor(0, 1, false, 0, &frame, NULL));
    CHECK(!kui_cdda_stream_cursor(0, 1, false, 0, &frame, &frame));
    CHECK(frame == 28 && loops == 2);

    for (i = 0; i < sizeof(bounds) / sizeof(bounds[0]); ++i)
        for (j = 0; j < sizeof(bounds) / sizeof(bounds[0]); ++j)
            for (k = 0; k < sizeof(bounds) / sizeof(bounds[0]); ++k) {
                check_cursor(bounds[i], bounds[j], false, bounds[k]);
                check_cursor(bounds[i], bounds[j], true, bounds[k]);
            }

    /* Multiple loop boundaries, exact EOF, and high-start addition overflow. */
    check_cursor(50, 123, false, 73);
    check_cursor(50, 123, false, 74);
    check_cursor(50, 123, true, 72);
    check_cursor(50, 123, true, 73);
    check_cursor(50, 123, true, 74);
    check_cursor(UINT32_MAX - 1, UINT32_MAX, true, UINT32_MAX);
    check_cursor(UINT32_MAX - 100, UINT32_MAX, true, UINT32_MAX);
    check_cursor(0, UINT32_MAX, false, UINT32_MAX);
    check_cursor(0, UINT32_MAX, true, UINT32_MAX);
    for (i = 0; i < 100000; ++i) {
        uint32_t a = random_u32(), b = random_u32(), played = random_u32();
        uint32_t first = a < b ? a : b, end = a < b ? b : a;
        check_cursor(first, end, false, played);
        check_cursor(first, end, true, played);
    }
}

static bool same_clock(const struct kui_cdda_stream_clock *a,
                       const struct kui_cdda_stream_clock *b)
{
    return a->last_tick == b->last_tick && a->seconds == b->seconds &&
           a->subticks == b->subticks && a->wraps == b->wraps &&
           a->hz == b->hz && a->initialized == b->initialized;
}

static void test_clock(void)
{
    struct kui_cdda_stream_clock clock = {0}, before;
    uint64_t elapsed = 0, absolute;
    uint32_t start = UINT32_MAX - 1000u;
    unsigned i;

    CHECK(!kui_cdda_stream_clock_init(NULL, 0, 1));
    CHECK(!kui_cdda_stream_clock_update(NULL, 0));
    CHECK(!kui_cdda_stream_clock_init(&clock, 0, 0));
    CHECK(!kui_cdda_stream_clock_update(&clock, 0));
    CHECK(kui_cdda_stream_clock_init(&clock, start, 12500000u));
    before = clock;
    CHECK(!kui_cdda_stream_clock_init(&clock, 0, 0));
    CHECK(same_clock(&clock, &before));
    CHECK(kui_cdda_stream_clock_update(&clock, start));
    CHECK(clock.seconds == 0 && clock.subticks == 0 && clock.wraps == 0);

    /* Fifteen minutes: cumulative ticks exceed 32 bits, with several wraps. */
    for (i = 0; i < 450; ++i) {
        elapsed += 25000000u;
        absolute = (uint64_t)start + elapsed;
        CHECK(kui_cdda_stream_clock_update(&clock, (uint32_t)absolute));
        CHECK(clock.seconds == elapsed / clock.hz);
        CHECK(clock.subticks == elapsed % clock.hz);
        CHECK(clock.wraps == absolute / ((uint64_t)UINT32_MAX + 1));
    }
    CHECK(clock.seconds == 900 && clock.subticks == 0 && clock.wraps == 3);
    for (i = 0; i < 100000; ++i) {
        uint32_t delta = random_u32() % 1000000u;
        elapsed += delta;
        absolute = (uint64_t)start + elapsed;
        CHECK(kui_cdda_stream_clock_update(&clock, (uint32_t)absolute));
        CHECK(clock.seconds == elapsed / clock.hz);
        CHECK(clock.subticks == elapsed % clock.hz);
        CHECK(clock.wraps == absolute / ((uint64_t)UINT32_MAX + 1));
    }

    /* A frequency larger than half UINT32_MAX exposes remainder-add overflow. */
    CHECK(kui_cdda_stream_clock_init(&clock, 0, UINT32_MAX));
    CHECK(kui_cdda_stream_clock_update(&clock, UINT32_MAX - 1));
    CHECK(clock.seconds == 0 && clock.subticks == UINT32_MAX - 1);
    CHECK(kui_cdda_stream_clock_update(&clock, UINT32_MAX - 3));
    CHECK(clock.seconds == 1 && clock.subticks == UINT32_MAX - 2 && clock.wraps == 1);
    CHECK(kui_cdda_stream_clock_update(&clock, UINT32_MAX - 1));
    CHECK(clock.seconds == 2 && clock.subticks == 0);

    CHECK(kui_cdda_stream_clock_init(&clock, 0, 1));
    CHECK(kui_cdda_stream_clock_update(&clock, UINT32_MAX));
    CHECK(clock.seconds == UINT32_MAX && clock.subticks == 0);
    before = clock;
    CHECK(!kui_cdda_stream_clock_update(&clock, 0));
    CHECK(same_clock(&clock, &before));

    CHECK(kui_cdda_stream_clock_init(&clock, UINT32_MAX, 10));
    clock.wraps = UINT32_MAX;
    before = clock;
    CHECK(!kui_cdda_stream_clock_update(&clock, 0));
    CHECK(same_clock(&clock, &before));
    clock.wraps = 0;
    clock.seconds = UINT32_MAX;
    clock.subticks = 9;
    before = clock;
    CHECK(!kui_cdda_stream_clock_update(&clock, 0));
    CHECK(same_clock(&clock, &before));
    clock.subticks = clock.hz;
    before = clock;
    CHECK(!kui_cdda_stream_clock_update(&clock, 0));
    CHECK(same_clock(&clock, &before));
}

int main(void)
{
    test_cursor();
    test_clock();
    printf("CDDA stream cursor/clock checks passed: %u\n", checks);
    return 0;
}
