/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_timing.h"

bool kui_cdda_timing_measure(const struct kui_cdda_timing_snapshot *start,
                             const struct kui_cdda_timing_snapshot *end,
                             uint32_t max_read_ticks,
                             uint32_t max_elapsed_ticks,
                             struct kui_cdda_timing_result *result)
{
    if (!start || !end || !result || !max_read_ticks ||
        !max_elapsed_ticks || max_elapsed_ticks >= UINT32_C(0x80000000) ||
        max_read_ticks > max_elapsed_ticks ||
        end->played_frames <= start->played_frames)
        return false;

    const uint32_t first_width = start->after_tick - start->before_tick;
    const uint32_t last_width = end->after_tick - end->before_tick;
    const uint32_t upper = end->after_tick - start->before_tick;
    if (first_width > max_read_ticks || last_width > max_read_ticks ||
        !upper || upper > max_elapsed_ticks || first_width >= upper ||
        last_width >= upper - first_width)
        return false;

    /* Subtract rather than adding the windows, so the bound arithmetic cannot
     * overflow even for unusually large valid caller-supplied limits. */
    *result = (struct kui_cdda_timing_result) {
        end->played_frames - start->played_frames,
        upper - first_width - last_width,
        upper, first_width, last_width
    };
    return true;
}
