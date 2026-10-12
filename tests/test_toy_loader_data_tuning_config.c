/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent matrix expectations applied to the actual public headers. */
#include "kui/toy_loader_data_tuning.h"
#include "kui/toy_loader_ad_followup.h"
#include "kui/toy_loader_schedule.h"
#include "kui/toy_loader_schedule_report.h"
#include "toy_pilot_sci.h"
_Static_assert(KUI_TOY_PILOT_DATA_TUNING_PROFILE==EXPECT_TUNING,
    "tuning profile");
_Static_assert(KUI_TOY_AD_BUDGET_REPORT_PROFILE==EXPECT_REPORT,
    "SCHED profile identification");
_Static_assert(KUI_TOY_SCI_SERVICE_BLOCKS==EXPECT_BLOCKS,
    "DATA block allowance");
_Static_assert(KUI_TOY_SCI_SERVICE_TICKS==EXPECT_TICKS,
    "DATA tick allowance");
_Static_assert(KUI_TOY_SCI_SERVICE_STEPS==EXPECT_STEPS,
    "finite stream step allowance");
_Static_assert(KUI_TOY_AD_REFILL_QUANTA==EXPECT_AUDIO_QUANTA,
    "retained audio quanta");
_Static_assert(KUI_TOY_AD_REFILL_TICKS==EXPECT_AUDIO_TICKS,
    "retained audio ticks");
_Static_assert(sizeof(struct kui_toy_pilot_sci_stats)==64u,
    "engine telemetry ABI");
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
_Static_assert(sizeof(struct kui_toy_pilot_sci_budget_stats)==48u &&
    KUI_TOY_SCHEDULE_REPORT_WORDS==192u && KUI_TOY_SCHEDULE_RECORD_WORDS==18u,
    "retained observed DATA report geometry");
#endif
#if KUI_TOY_PILOT_DATA_TUNING_PROFILE
_Static_assert(KUI_TOY_TUNING_REPORT_PROFILE==EXPECT_REPORT &&
    KUI_TOY_TUNING_BLOCKS==EXPECT_BLOCKS &&
    KUI_TOY_TUNING_TICKS==EXPECT_TICKS &&
    KUI_TOY_TUNING_STEPS==EXPECT_STEPS,
    "direct tuning aliases agree with SCI caps");
#else
#if defined(KUI_TOY_TUNING_REPORT_PROFILE) || defined(KUI_TOY_TUNING_BLOCKS) || \
    defined(KUI_TOY_TUNING_TICKS) || defined(KUI_TOY_TUNING_STEPS)
#error Disabled tuning must not define active tuning cap aliases
#endif
#endif
int main(void) {return 0;}
