/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_LOADER_DATA_TUNING_H
#define KUI_TOY_LOADER_DATA_TUNING_H

/* Separate, explicit follow-ups over AF. Zero preserves every old profile. */
#ifndef KUI_TOY_PILOT_DATA_TUNING_PROFILE
#define KUI_TOY_PILOT_DATA_TUNING_PROFILE 0
#endif
#ifndef KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE
#define KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE 0
#endif
#ifndef KUI_TOY_PILOT_DATA_BUDGET_PROFILE
#define KUI_TOY_PILOT_DATA_BUDGET_PROFILE 0
#endif
#ifndef KUI_TOY_PILOT_DATA_SCHEDULE_MODE
#define KUI_TOY_PILOT_DATA_SCHEDULE_MODE 0
#endif
#if KUI_TOY_PILOT_DATA_TUNING_PROFILE < 0 || KUI_TOY_PILOT_DATA_TUNING_PROFILE > 4
#error Toy DATA tuning profile must be 0, 1, 2, 3 or 4
#endif
#if KUI_TOY_PILOT_DATA_TUNING_PROFILE && \
    (KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE != 2 || \
     KUI_TOY_PILOT_DATA_BUDGET_PROFILE != 4 || \
     KUI_TOY_PILOT_DATA_SCHEDULE_MODE != 2 || \
     !KUI_TOY_PILOT_PRIVATE_P2 || KUI_TOY_PILOT_NATIVE_CACHE || \
     KUI_TOY_PILOT_SHARED_SCI || KUI_TOY_PILOT_ASYNC_CDDA || \
     KUI_TOY_PILOT_SYNTHETIC_SOURCE || !KUI_TOY_PILOT_LOADER_TRACE || \
     !KUI_TOY_PILOT_DATA_PROBE || KUI_TOY_PILOT_DATA_PAYLOAD_MODE || \
     KUI_TOY_PILOT_GD_FIXED_STEP != 2 || KUI_SCI_DMA_REUSE_TDRE)
#error Toy DATA tuning requires the complete admitted AF configuration
#endif

/* AI: lift AF's observed step constraint. AJ: shorten AF's time admission.
 * AK: increase AI's block allowance. AL: increase AK's time admission.
 * The independent timer and finite step guard both remain active. */
#if KUI_TOY_PILOT_DATA_TUNING_PROFILE
#define KUI_TOY_TUNING_REPORT_PROFILE (8u + KUI_TOY_PILOT_DATA_TUNING_PROFILE)
#if KUI_TOY_PILOT_DATA_TUNING_PROFILE >= 3
#define KUI_TOY_TUNING_BLOCKS 32u
#else
#define KUI_TOY_TUNING_BLOCKS 24u
#endif
#if KUI_TOY_PILOT_DATA_TUNING_PROFILE == 2
#define KUI_TOY_TUNING_TICKS 9000u
#elif KUI_TOY_PILOT_DATA_TUNING_PROFILE == 4
#define KUI_TOY_TUNING_TICKS 18000u
#else
#define KUI_TOY_TUNING_TICKS 12000u
#endif
#if KUI_TOY_PILOT_DATA_TUNING_PROFILE == 2
#define KUI_TOY_TUNING_STEPS 1024u
#else
#define KUI_TOY_TUNING_STEPS 2048u
#endif
#endif

#endif
