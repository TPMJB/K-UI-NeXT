/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_PHASE_CLOCK_H
#define KUI_SMASH_PHASE_CLOCK_H
#include <stdint.h>
#define KUI_SMASH_PHASE_CYCLES 1u
#define KUI_SMASH_PHASE_NOMINAL_MS 2u
#define KUI_SMASH_PHASE_RTC 4u
#define KUI_SMASH_PHASE_BAD_CPU_CONFIG 0x100u
#define KUI_SMASH_PHASE_TORN_COUNTER 0x200u
#define KUI_SMASH_PHASE_STOPPED_COUNTER 0x400u
#define KUI_SMASH_PHASE_BUSY_RTC 0x800u
#define KUI_SMASH_PHASE_TORN_RTC 0x1000u
#define KUI_SMASH_PHASE_BAD_DELTA 0x2000u
#define KUI_SMASH_PHASE_CLOCK_DISAGREEMENT 0x4000u
#define KUI_SMASH_PHASE_COUNTER_MASK UINT64_C(0x0000ffffffffffff)
struct kui_smash_phase_stamp {
    uint32_t flags,pmcr,frqcr,cycles_hi,cycles_lo,rtc_seconds,g2_state,reserved;
};
struct kui_smash_phase_delta {
    uint64_t cycles;
    uint32_t flags,cpu_ms,rtc_seconds,reserved;
};
_Static_assert(sizeof(struct kui_smash_phase_stamp)==32u,"Phase clock sample ABI");
_Static_assert(sizeof(struct kui_smash_phase_delta)==24u,"Phase clock delta ABI");
/* Pure observation: no timer/performance-counter/G2 register is written.
 * PRFC0 must already run in elapsed CPU-cycle mode(c023); its split48-bit
 * value must be coherent and advancing. Nominal ms additionally requires
 * unchanged reviewed FRQCR e0a (200MHz nominal, not a calibrated wall clock).
 * RTC reads require IMASK15 plus idle G2 channels and all relevant FIFOs. */
void kui_smash_phase_clock_read(struct kui_smash_phase_stamp *);
/* Frequent command-boundary observation: only PMCR0, FRQCR and PRFC0 high/low
 * are read. No SR, G2, FIFO or RTC access, no register writes, no clock setup.
 * The same bounded coherent/advancing/mode checks apply. RTC/G2 fields stay
 * zero and their validity/refusal flags are absent. */
void kui_smash_phase_clock_read_fast(struct kui_smash_phase_stamp *);
/* Missing/changed clock modes remain unavailable; callers MUST inspect
 * flags rather than displaying the zero-initialized numeric fields as time.
 * RTC seconds are modulo32-bit; CPU cycles are modulo48-bit. */
void kui_smash_phase_clock_delta(const struct kui_smash_phase_stamp *,
    const struct kui_smash_phase_stamp *,struct kui_smash_phase_delta *);
#endif
