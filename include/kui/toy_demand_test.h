/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_DEMAND_TEST_H
#define KUI_TOY_DEMAND_TEST_H
#include <stdint.h>
#include "kui/fmv_test.h"

/* Isolated pre-PLAY DATA comparisons. Zero retains the existing reader and
 * FMV report. These selectors do not enlarge any transport allowance. */
#ifndef KUI_TOY_DEMAND_TEST_PROFILE
#define KUI_TOY_DEMAND_TEST_PROFILE 0
#endif
#if KUI_TOY_DEMAND_TEST_PROFILE < 0 || KUI_TOY_DEMAND_TEST_PROFILE > 3
#error Toy demand test profile must be 0, 1, 2 or 3
#endif
#if KUI_TOY_DEMAND_TEST_PROFILE && (!KUI_FMV_TEST || KUI_TOY_FMV_IDLE_REARM || \
    KUI_TOY_PILOT_DATA_PIPELINE_PROFILE != 2 || \
    KUI_TOY_PILOT_DATA_SCHEDULE_MODE != 2)
#error Toy demand tests require the isolated AP FMV build without idle rearm
#endif

#define KUI_TOY_DEMAND_READY_RETURN (KUI_TOY_DEMAND_TEST_PROFILE == 2)
#define KUI_TOY_DEMAND_SKIP_FINGERPRINT (KUI_TOY_DEMAND_TEST_PROFILE == 3)

#if KUI_TOY_DEMAND_TEST_PROFILE
/* Native engine entries are already masked. Original caller SR comes from
 * the existing DATA probe, never from reading the engine's current SR. */
struct kui_toy_demand_test_stats {
    uint32_t service_calls, pump_steps, wait_polls, wait_returns;
    uint32_t retry_steps, credit_visits, zero_credit_visits, max_steps;
};
_Static_assert(sizeof(struct kui_toy_demand_test_stats)==32u,"Toy demand test ABI");
const struct kui_toy_demand_test_stats *kui_toy_demand_test_snapshot(void);
uint32_t kui_toy_demand_test_fingerprint_skips(void);
#endif
#endif
