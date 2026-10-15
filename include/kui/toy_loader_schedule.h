/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_LOADER_SCHEDULE_H
#define KUI_TOY_LOADER_SCHEDULE_H
#include <stdint.h>

/* These controls belong only to separate diagnostic profiles. A zero mode
 * leaves the retained two-sector reader's code and data unchanged. */
#ifndef KUI_TOY_PILOT_DATA_SCHEDULE_MODE
#define KUI_TOY_PILOT_DATA_SCHEDULE_MODE 0
#endif
#ifndef KUI_TOY_PILOT_DATA_BUDGET_PROFILE
#define KUI_TOY_PILOT_DATA_BUDGET_PROFILE 0
#endif
#include "kui/toy_loader_ad_followup.h"
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE < 0 || KUI_TOY_PILOT_DATA_BUDGET_PROFILE > 4
#error Toy loader DATA budget profile must be 0, 1, 2, 3 or 4
#endif
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE && \
    (KUI_TOY_PILOT_DATA_SCHEDULE_MODE != 2 || !KUI_TOY_PILOT_LOADER_TRACE || \
     !KUI_TOY_PILOT_DATA_PROBE)
#error Toy loader DATA budgets require the observed startup-only DATA engine
#endif
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE < 0 || KUI_TOY_PILOT_DATA_SCHEDULE_MODE > 3
#error Toy loader DATA schedule mode must be 0, 1, 2 or 3
#endif
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE && \
    (!KUI_TOY_PILOT_PRIVATE_P2 || KUI_TOY_PILOT_NATIVE_CACHE || \
     KUI_TOY_PILOT_SHARED_SCI || KUI_TOY_PILOT_ASYNC_CDDA || \
     KUI_TOY_PILOT_SYNTHETIC_SOURCE || KUI_TOY_PILOT_GD_FIXED_STEP != 2 || \
     KUI_SCI_DMA_REUSE_TDRE || KUI_TOY_PILOT_DATA_PAYLOAD_MODE)
#error Toy loader DATA schedule requires retained private P2 two-sector real audio
#endif

/* Accepted means both the GD core and the audio mailbox accepted PLAY.
 * It is a one-way cold-boot phase boundary: GD INIT/RESET and rejected PLAY
 * must not reactivate startup scheduling after storage belongs to audio. */
static inline uint32_t kui_toy_loader_schedule_step(uint32_t accepted_play) {
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 1
    return accepted_play ? 2u : 1u;
#else
    (void)accepted_play;
    return 2u;
#endif
}
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE
/* Called by the serialized high GD adapter through the exact linked low
 * entry, never through a guest address. It writes only private P2 state. */
void kui_retail_toy_data_schedule_play(void);

/* The high adapter includes generated linked addresses before this header.
 * Low code sees only the setter declaration above. Host protocol fixtures
 * replace the numeric call with the same low phase-latch operation. */
#if defined(KUI_TOY_LOADER_SCHEDULE_HOST_TEST)
void kui_toy_loader_schedule_host_play(void);
static inline void kui_toy_loader_schedule_accepted_play(void) {
    kui_toy_loader_schedule_host_play();
}
#elif defined(KUI_TOY_PILOT_LOW_DATA_SCHEDULE_PLAY)
static inline void kui_toy_loader_schedule_accepted_play(void) {
    ((void (*)(void))(uintptr_t)KUI_TOY_PILOT_LOW_DATA_SCHEDULE_PLAY)();
}
#endif
#endif
#endif
