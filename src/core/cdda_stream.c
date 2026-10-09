/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_stream.h"

bool kui_cdda_stream_init(struct kui_cdda_stream *stream, uint32_t first_frame,
                         uint32_t end_frame, bool repeat)
{
    if (!stream || first_frame >= end_frame)
        return false;
    stream->first_frame = first_frame;
    stream->end_frame = end_frame;
    stream->repeat = repeat;
    stream->initialized = true;
    return true;
}

bool kui_cdda_stream_cursor(uint32_t first_frame, uint32_t end_frame,
                            bool repeat, uint32_t played,
                            uint32_t *frame, uint32_t *loops)
{
    uint32_t span, offset, completed;

    if (!frame || !loops || frame == loops || first_frame >= end_frame)
        return false;
    span = end_frame - first_frame;
    if (repeat) {
        offset = played % span;
        completed = played / span;
    } else {
        if (played > span)
            return false;
        offset = played;
        completed = 0;
    }
    /* offset <= span, and first_frame + span == end_frame, so the only
     * addition is proven representable even at the UINT32_MAX boundary. */
    *frame = first_frame + offset;
    *loops = completed;
    return true;
}

bool kui_cdda_stream_position(const struct kui_cdda_stream *stream,
                              uint32_t played, uint32_t *frame, uint32_t *loops)
{
    if (!stream || !stream->initialized)
        return false;
    return kui_cdda_stream_cursor(stream->first_frame, stream->end_frame,
                                   stream->repeat, played, frame, loops);
}

bool kui_cdda_stream_clock_init(struct kui_cdda_stream_clock *clock,
                               uint32_t now, uint32_t hz)
{
    if (!clock || !hz)
        return false;
    clock->last_tick = now;
    clock->seconds = 0;
    clock->subticks = 0;
    clock->wraps = 0;
    clock->hz = hz;
    clock->initialized = true;
    return true;
}

bool kui_cdda_stream_clock_update(struct kui_cdda_stream_clock *clock,
                                 uint32_t now)
{
    uint32_t delta, seconds, remainder, subticks, wraps;

    if (!clock || !clock->initialized || !clock->hz ||
        clock->subticks >= clock->hz)
        return false;

    delta = now - clock->last_tick;
    seconds = delta / clock->hz;
    remainder = delta % clock->hz;
    subticks = clock->subticks;
    wraps = clock->wraps;

    /* Adding two remainders directly could overflow for a large hz. */
    if (remainder >= clock->hz - subticks) {
        remainder -= clock->hz - subticks;
        if (seconds == UINT32_MAX)
            return false;
        ++seconds;
        subticks = remainder;
    } else {
        subticks += remainder;
    }
    if (seconds > UINT32_MAX - clock->seconds)
        return false;
    seconds += clock->seconds;

    if (now < clock->last_tick) {
        if (wraps == UINT32_MAX)
            return false;
        ++wraps;
    }

    clock->last_tick = now;
    clock->seconds = seconds;
    clock->subticks = subticks;
    clock->wraps = wraps;
    return true;
}
