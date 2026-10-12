/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_LOADER_AD_FOLLOWUP_H
#define KUI_TOY_LOADER_AD_FOLLOWUP_H

#ifndef KUI_TOY_PILOT_DATA_BUDGET_PROFILE
#define KUI_TOY_PILOT_DATA_BUDGET_PROFILE 0
#endif
#ifndef KUI_TOY_PILOT_DATA_SCHEDULE_MODE
#define KUI_TOY_PILOT_DATA_SCHEDULE_MODE 0
#endif

/* Separate opt-in factors over the admitted AD startup reader. Zero keeps
 * every existing reader and audio scheduling allowance unchanged. */
#ifndef KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE
#define KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE 0
#endif
#if KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE < 0 || KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE > 4
#error Toy AD follow-up profile must be 0, 1, 2, 3 or 4
#endif
#if KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE && \
    (KUI_TOY_PILOT_DATA_BUDGET_PROFILE != 4 || \
     KUI_TOY_PILOT_DATA_SCHEDULE_MODE != 2 || \
     !KUI_TOY_PILOT_PRIVATE_P2 || KUI_TOY_PILOT_NATIVE_CACHE || \
     KUI_TOY_PILOT_SHARED_SCI || KUI_TOY_PILOT_ASYNC_CDDA || \
     KUI_TOY_PILOT_SYNTHETIC_SOURCE || !KUI_TOY_PILOT_LOADER_TRACE || \
     !KUI_TOY_PILOT_DATA_PROBE || KUI_TOY_PILOT_DATA_PAYLOAD_MODE || \
     KUI_TOY_PILOT_GD_FIXED_STEP != 2 || KUI_SCI_DMA_REUSE_TDRE)
#error Toy AD follow-ups require the complete admitted AD reader configuration
#endif

#include "kui/toy_loader_data_tuning.h"

/* The unchanged twelve-word SCHED budget snapshot identifies these four
 * experiments as profiles 5..8; their build IDs identify the audio factor. */
#if KUI_TOY_PILOT_DATA_TUNING_PROFILE
#define KUI_TOY_AD_BUDGET_REPORT_PROFILE KUI_TOY_TUNING_REPORT_PROFILE
#elif KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE
#define KUI_TOY_AD_BUDGET_REPORT_PROFILE (4u + KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE)
#else
#define KUI_TOY_AD_BUDGET_REPORT_PROFILE KUI_TOY_PILOT_DATA_BUDGET_PROFILE
#endif

/* Audio scheduling factors retain the original sector reader, hardware
 * queue, ownership and ring algorithm. A quantum can include a cached
 * second fragment; these are admission limits, not hard call deadlines. */
#if KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE == 3
#define KUI_TOY_AD_REFILL_QUANTA 2u
#else
#define KUI_TOY_AD_REFILL_QUANTA 4u
#endif
#if KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE == 4
#define KUI_TOY_AD_REFILL_TICKS 6250u
#else
#define KUI_TOY_AD_REFILL_TICKS 12500u
#endif

#endif
