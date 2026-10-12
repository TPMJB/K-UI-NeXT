/* SPDX-License-Identifier: GPL-3.0-only */
/* Compile the actual public admission headers against independently stated
 * labels/caps. The worker's private audio constants are inspected by the
 * runner through its production preprocessor path. */
#include "kui/toy_loader_schedule.h"
#include "toy_pilot_sci.h"
_Static_assert(KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE==EXPECT_FOLLOWUP,
    "follow-up compile profile");
_Static_assert(KUI_TOY_AD_BUDGET_REPORT_PROFILE==EXPECT_REPORT_PROFILE,
    "SCHED experiment label");
_Static_assert(KUI_TOY_SCI_SERVICE_BLOCKS==EXPECT_BLOCKS,
    "DATA foreground block allowance");
_Static_assert(KUI_TOY_SCI_SERVICE_TICKS==EXPECT_TICKS,
    "DATA foreground tick allowance");
_Static_assert(KUI_TOY_AD_REFILL_QUANTA==EXPECT_AUDIO_QUANTA,
    "audio refill quanta allowance");
_Static_assert(KUI_TOY_AD_REFILL_TICKS==EXPECT_AUDIO_TICKS,
    "audio refill tick allowance");
_Static_assert(KUI_TOY_SCI_SERVICE_STEPS==1024u,
    "finite stream admission unchanged");
_Static_assert(sizeof(struct kui_toy_pilot_sci_stats)==64u,
    "engine report retained");
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
_Static_assert(sizeof(struct kui_toy_pilot_sci_budget_stats)==48u,
    "budget report retained");
#endif
int main(void) {return 0;}
