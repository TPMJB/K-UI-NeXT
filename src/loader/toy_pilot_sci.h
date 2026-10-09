/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_SCI_H
#define KUI_TOY_PILOT_SCI_H
#include "kui/retail_gd.h"
#include "sd_reader.h"
#ifndef KUI_TOY_PILOT_ASYNC_CDDA
#define KUI_TOY_PILOT_ASYNC_CDDA 0
#endif

/* This engine is linked only into the exact-title high worker. Calls run
 * with CPU interrupts masked; its SCI handler has a separate guarded stack.
 * A data handle remains owned until the normal CHECK acknowledges it. The
 * physical card may instead be yielded at a checked 512-byte boundary. */
enum kui_toy_pilot_sci_result {
    KUI_TOY_SCI_OK = 0, KUI_TOY_SCI_PENDING = 1, KUI_TOY_SCI_FAULT = -1
};
/* Foreground CHECK/EXEC admission, checked between finite stream steps. */
#define KUI_TOY_SCI_SERVICE_BLOCKS 4u
#define KUI_TOY_SCI_SERVICE_STEPS 1024u
#define KUI_TOY_SCI_SERVICE_TICKS 1500u
/* Four verified RAW sectors fit inside the unchanged 64-KiB worker lease. */
#define KUI_TOY_SCI_AUDIO_QUEUE 4u
#define KUI_TOY_SCI_AUDIO_RESERVE_LOW 4096u
#define KUI_TOY_SCI_AUDIO_RESERVE_HIGH 8192u
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
#if KUI_TOY_PILOT_ASYNC_CDDA
/* Admit one mapped audio track interval [lba,end), called with saved SR
 * masked. The immutable manifest must identify audio (control=0), RAW
 * sectors, and end <= that track's end. Generation must be nonzero. Queue
 * production is independent of read/output and never crosses this range.
 * A same-generation plan at the current unconsumed LBA preserves queued
 * sectors; seek, range or generation change revokes before fencing RAW.
 * reserve_frames is a conservative playable PCM reserve. Its timestamped
 * value decays while worker visits are absent; LOW/HIGH hysteresis gives
 * DATA priority while reserve is healthy, RAW priority as it runs low.
 * Invalid plans fail closed, revoking old RAW authority. DATA is independent. */
int kui_toy_pilot_sci_audio_plan(uint32_t lba,uint32_t end,uint32_t generation,
    uint32_t reserve_frames);
/* Fixed 2352-byte RAW request. (lba,generation,output) identifies publication;
 * output must remain the same private worker buffer for a sequential plan.
 * IRQ/GD service populate only the four private verified queue slots.
 * PENDING never writes output. OK consumes the ready head once; repeated
 * matching calls return OK without copying again. The next sequential LBA
 * retains the queued tail. A mismatching tuple under an explicit plan
 * revokes before fencing RAW and returns FAULT; replace its plan first.
 * No plan retains the legacy one-sector request behavior.
 * Completion/full queue releases the physical bus. No sound SDK, generic
 * payload DMA wait or low RAW callback runs in the transport producer. */
int kui_toy_pilot_sci_audio_read(uint32_t lba,uint32_t generation,void *output);
#endif
const struct kui_toy_pilot_sci_stats *kui_toy_pilot_sci_snapshot(void);
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
