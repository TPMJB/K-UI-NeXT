/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_PROFILE_H
#define KUI_VMU_NATIVE_PROFILE_H
#include <stdbool.h>
#include <stdint.h>

#define KUI_VMU_NATIVE_PROFILE_HZ 781250u
#define KUI_VMU_NATIVE_PROFILE_MAX_INTERVAL UINT32_C(0x7fffffff)
enum kui_vmu_native_profile_kind {
    KUI_VMU_NATIVE_PROFILE_SUBMIT,
    KUI_VMU_NATIVE_PROFILE_COMPLETE,
    KUI_VMU_NATIVE_PROFILE_FREE,
    KUI_VMU_NATIVE_PROFILE_STORAGE,
    KUI_VMU_NATIVE_PROFILE_KINDS
};
enum kui_vmu_native_profile_flags {
    KUI_VMU_NATIVE_PROFILE_CALLS_SATURATED=1u,
    KUI_VMU_NATIVE_PROFILE_VALID_SATURATED=2u,
    KUI_VMU_NATIVE_PROFILE_INVALID_SATURATED=4u,
    KUI_VMU_NATIVE_PROFILE_TICKS_SATURATED=8u
};
/* Read-only samples of FRQCR, TCOR0, TCR0, TSTR and TCNT0. This module owns
 * no timer and performs no MMIO. The caller samples each endpoint under its
 * existing serialized scope and must not reconfigure the game clock. */
struct kui_vmu_native_profile_sample {
    uint32_t frqcr,tcor,tcr,tstr,counter;
};
struct kui_vmu_native_profile_span {
    struct kui_vmu_native_profile_sample begin;
    uint32_t valid;
};
struct kui_vmu_native_profile_metric {
    uint32_t calls,valid,invalid,ticks_total,ticks_max;
};
struct kui_vmu_native_profile {
    struct kui_vmu_native_profile_metric metric[KUI_VMU_NATIVE_PROFILE_KINDS];
    uint32_t flags;
};
/* Same exact existing owner profile as toy_pilot_clock.h: running TMU0,
 * full32-bit reload, PCLK/64, admitted 50MHz peripheral clock. A valid sample
 * does not prove that an unobserved stop/reset never happened between reads.
 * In the intended private C hook no game caller runs between endpoints. */
bool kui_vmu_native_profile_sample_valid(const struct kui_vmu_native_profile_sample *);
void kui_vmu_native_profile_begin(struct kui_vmu_native_profile_span *,
    const struct kui_vmu_native_profile_sample *);
/* Records one call. Invalid/changed clock profiles and implausibly long
 * intervals increment invalid instead of adding a guessed duration. A
 * down-counter wrap is handled modulo32bits. All cumulative fields saturate;
 * ticks_max remains the largest valid observed interval. The caller may nest
 * separate spans (e.g. a storage loan inside completion); these metrics then
 * overlap in time and must not be added as independent CPU usage. */
bool kui_vmu_native_profile_end(struct kui_vmu_native_profile *,
    enum kui_vmu_native_profile_kind,const struct kui_vmu_native_profile_span *,
    const struct kui_vmu_native_profile_sample *);
#endif
