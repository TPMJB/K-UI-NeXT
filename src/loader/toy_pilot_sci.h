/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_SCI_H
#define KUI_TOY_PILOT_SCI_H
#include "kui/retail_gd.h"
#include "sd_reader.h"
#ifndef KUI_TOY_PILOT_ASYNC_CDDA
#define KUI_TOY_PILOT_ASYNC_CDDA 0
#endif
#ifndef KUI_TOY_PILOT_DATA_SCHEDULE_MODE
#define KUI_TOY_PILOT_DATA_SCHEDULE_MODE 0
#endif
#ifndef KUI_TOY_PILOT_DATA_BUDGET_PROFILE
#define KUI_TOY_PILOT_DATA_BUDGET_PROFILE 0
#endif
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
#include "kui/toy_loader_schedule.h"
#endif
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE < 0 || KUI_TOY_PILOT_DATA_BUDGET_PROFILE > 4
#error Unknown startup DATA budget profile
#endif
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE && \
    (KUI_TOY_PILOT_DATA_SCHEDULE_MODE != 2 || KUI_TOY_PILOT_SHARED_SCI || \
     KUI_TOY_PILOT_ASYNC_CDDA || !KUI_TOY_PILOT_PRIVATE_P2)
#error DATA budgets require the private startup DATA engine and retained synchronous audio
#endif
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 2 && \
    (KUI_TOY_PILOT_SHARED_SCI || KUI_TOY_PILOT_ASYNC_CDDA || !KUI_TOY_PILOT_PRIVATE_P2)
#error Startup DATA scheduling requires private P2 and the unchanged synchronous audio profile
#endif

/* This engine is linked only into the exact-title high worker. Calls run
 * with CPU interrupts masked; its SCI handler has a separate guarded stack.
 * A data handle remains owned until the normal CHECK acknowledges it. The
 * physical card may instead be yielded at a checked 512-byte boundary. */
enum kui_toy_pilot_sci_result {
    KUI_TOY_SCI_OK = 0, KUI_TOY_SCI_PENDING = 1, KUI_TOY_SCI_FAULT = -1
};
/* Foreground CHECK/EXEC admission, checked between finite stream steps. */
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE == 2 || KUI_TOY_PILOT_DATA_BUDGET_PROFILE == 4
#define KUI_TOY_SCI_SERVICE_BLOCKS 12u
#else
#define KUI_TOY_SCI_SERVICE_BLOCKS 4u
#endif
#define KUI_TOY_SCI_SERVICE_STEPS 1024u
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE == 3 || KUI_TOY_PILOT_DATA_BUDGET_PROFILE == 4
#define KUI_TOY_SCI_SERVICE_TICKS 6000u
#else
#define KUI_TOY_SCI_SERVICE_TICKS 1500u
#endif
struct kui_toy_pilot_sci_stats {
    /* Verified physical blocks include DATA and asynchronous RAW work. */
    uint32_t calls, irq_calls, call_blocks, irq_blocks;
    uint32_t audio_claims, audio_pending, audio_releases, data_resumes;
    uint32_t errors, retries, token_yields, polled_blocks;
    uint32_t work_ticks_max, irq_ticks_max, active_token, card_owned;
};
_Static_assert(sizeof(struct kui_toy_pilot_sci_stats)==64u,"Shared SCI telemetry");
void kui_toy_pilot_sci_init(const struct kui_retail_manifest *,
    const struct kui_loader_sd *, enum kui_loader_sd_result (*acquire)(void),
    void (*release)(void));
/* Detect a newly validated pending GD16/17 token and advance at most one
 * checked card block. Never waits for DMA. Updates GD progress itself. */
int kui_toy_pilot_sci_pump(struct kui_retail_gd *);
/* Foreground CHECK/EXEC only: make up to four verified card blocks of
 * progress since the preceding service, counting intervening IRQ/pump work.
 * Finite step/time admission and a single token-search slice bound this
 * entry; one stream step may exceed the time allowance. No sound work or
 * generic DMA wait is called. A pending audio claim gets one nonblocking
 * DATA boundary pump; a held synchronous raw lease gets no transport access.
 * With asynchronous CDDA this also advances RAW without a pending DATA
 * handle. Continuing IRQs or frequent CHECK/EXEC, beyond worker visits,
 * must supply the remaining physical-block progress. */
int kui_toy_pilot_sci_service(struct kui_retail_gd *);
/* Invalidate the token before fencing/stopping DMA. No completion is forged. */
void kui_toy_pilot_sci_cancel(struct kui_retail_gd *);
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 2
struct kui_toy_pilot_sci_startup_stats {
    uint32_t enabled,retired,quiesces,quiesce_failed;
};
/* Startup DATA alone owns this receiver. Quiesce revokes its cursor before
 * fencing DMA and restoring the game's vectors/card ownership. A failed
 * quiesce must never be followed by publication of an audio PLAY mailbox.
 * Retirement is permanent for this cold boot; rejected PLAY/mailbox calls
 * may quiesce safely without retiring the startup scheduler. */
bool kui_toy_pilot_sci_startup_active(void);
bool kui_toy_pilot_sci_quiesce(void);
void kui_toy_pilot_sci_retire(void);
const struct kui_toy_pilot_sci_startup_stats *kui_toy_pilot_sci_startup_snapshot(void);
#else
/* 0: the shared bus was yielded and raw audio may claim it; 1: wait for
 * an arriving data block; -1: transport failed. A failed/pending claim must
 * not be followed by a raw callback. Release exactly one successful claim.
 * ASYNC_CDDA builds reject this legacy admission with FAULT. */
int kui_toy_pilot_sci_audio_acquire(void);
void kui_toy_pilot_sci_audio_release(void);
#if KUI_TOY_PILOT_ASYNC_CDDA
/* Revoke the RAW tuple before fencing/stopping a RAW-owned receiver. Leaves
 * the logical DATA token and a DATA-owned receiver intact. */
#else
/* Revoke an ungranted audio request after its mailbox/driver epoch changes.
 * Does not cancel data, touch an acquired raw lease or access hardware. */
#endif
void kui_toy_pilot_sci_audio_cancel(void);
#endif
#if KUI_TOY_PILOT_ASYNC_CDDA
/* Fixed 2352-byte raw audio request, called with exact saved SR masked.
 * (lba,generation,output) identifies a request; generation must be nonzero
 * and output is the stable private worker buffer. A new tuple revokes the
 * preceding one before fencing its DMA. PENDING never writes output: IRQ,
 * GD service and this one-step poll fill only engine-owned staging. OK
 * publishes the fully verified sector once; repeated matching calls return
 * OK without copying again. FAULT remains attached to the matching tuple.
 * Completion releases the physical bus before the worker consumes staging.
 * No sound work, generic payload DMA wait or low raw callback is invoked. */
int kui_toy_pilot_sci_audio_read(uint32_t lba,uint32_t generation,void *output);
#endif
const struct kui_toy_pilot_sci_stats *kui_toy_pilot_sci_snapshot(void);
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
/* Diagnostic foreground admission only. Exactly one exit counter is charged
 * per enabled service call. Blocks/bytes count work within that call, not
 * intervening IRQ or one-step work. Mutable counters saturate at UINT32_MAX.
 * The time allowance admits finite steps; the final step may cross it. */
struct kui_toy_pilot_sci_budget_stats {
    uint32_t profile,block_limit,tick_limit,service_calls;
    uint32_t block_exits,time_exits,token_exits,step_exits;
    uint32_t finished_or_inactive_exits,service_verified_blocks,zero_credit_calls,credited_bytes;
};
_Static_assert(sizeof(struct kui_toy_pilot_sci_budget_stats)==48u,"Startup DATA budget telemetry");
const struct kui_toy_pilot_sci_budget_stats *kui_toy_pilot_sci_budget_snapshot(void);
#endif
/* Returns zero for our SCI event, nonzero for normal game forwarding. */
uint32_t kui_toy_pilot_sci_irq(void);

#ifdef KUI_TOY_PILOT_SCI_TEST
uint32_t kui_toy_pilot_sci_test_vbr(void);
void kui_toy_pilot_sci_test_set_vbr(uint32_t);
uint16_t kui_toy_pilot_sci_test_read16(uint32_t);
void kui_toy_pilot_sci_test_write16(uint32_t,uint16_t);
uint32_t kui_toy_pilot_sci_test_ticks(void);
#endif
#endif
