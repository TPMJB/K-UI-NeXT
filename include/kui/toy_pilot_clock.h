/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_CLOCK_H
#define KUI_TOY_PILOT_CLOCK_H
#include <stdbool.h>
#include <stdint.h>

/* Existing Toy timer profile: read-only admission, never timer setup. */
static inline bool kui_toy_pilot_clock_profile(uint32_t frqcr, uint32_t tcor,
    uint32_t tcr, uint32_t tstr) {
    return (((frqcr & 0x0fffu) ^ 0x0e0au) | ~tcor |
        ((tcr & 0x0027u) ^ 0x0002u) | ((tstr & 1u) ^ 1u)) == 0u;
}
#endif
