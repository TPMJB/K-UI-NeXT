/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent experiment matrix checked through production public headers. */
#include "kui/toy_loader_irq_progress.h"
#include "kui/toy_loader_ad_followup.h"
#include "kui/toy_loader_schedule.h"
#include "kui/toy_loader_schedule_report.h"
#include "toy_pilot_sci.h"
_Static_assert(KUI_TOY_PILOT_STARTUP_IRQ_PROFILE == EXPECT_IRQ,
    "startup IRQ selector");
_Static_assert(KUI_TOY_STARTUP_SCI_PRIORITY == EXPECT_PRIORITY,
    "SCI interrupt priority");
_Static_assert(KUI_TOY_STARTUP_SCI_NIBBLE == EXPECT_NIBBLE &&
    KUI_TOY_STARTUP_SCI_LEVEL == EXPECT_NIBBLE,
    "C and assembly SCI fields agree");
_Static_assert(KUI_TOY_AD_BUDGET_REPORT_PROFILE == EXPECT_REPORT,
    "SCHED report identification");
_Static_assert(KUI_TOY_SCI_SERVICE_BLOCKS == EXPECT_BLOCKS &&
    KUI_TOY_SCI_SERVICE_TICKS == EXPECT_TICKS &&
    KUI_TOY_SCI_SERVICE_STEPS == EXPECT_STEPS,
    "foreground service admission");
_Static_assert(KUI_TOY_AD_REFILL_QUANTA == EXPECT_AUDIO_QUANTA &&
    KUI_TOY_AD_REFILL_TICKS == EXPECT_AUDIO_TICKS,
    "retained audio admission");
_Static_assert(sizeof(struct kui_toy_pilot_sci_stats) == 64u,
    "retained SCI stats ABI");
#if KUI_TOY_PILOT_STARTUP_IRQ_PROFILE
_Static_assert(KUI_TOY_IRQ_REPORT_PROFILE == EXPECT_REPORT,
    "startup IRQ report alias");
#else
#ifdef KUI_TOY_IRQ_REPORT_PROFILE
#error Disabled IRQ experiments must not define an active report alias
#endif
#endif
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
_Static_assert(sizeof(struct kui_toy_pilot_sci_budget_stats) == 48u &&
    KUI_TOY_SCHEDULE_REPORT_WORDS == 192u &&
    KUI_TOY_SCHEDULE_RECORD_WORDS == 18u,
    "retained budget and SCHED wire ABI");
#endif
int main(void) { return 0; }
