/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_STREAM_TRACE_H
#define KUI_ARMADA_CE_STREAM_TRACE_H
#include <stdbool.h>
#include <stdint.h>
#define KUI_ARMADA_CE_STREAM_TRACE_ROWS 6u
enum kui_armada_ce_stream_trace_code {
    KUI_ARMADA_CE_STREAM_TRACE_START=1,
    KUI_ARMADA_CE_STREAM_TRACE_UNDERRUN=2,
    KUI_ARMADA_CE_STREAM_TRACE_FAULT=3,
    KUI_ARMADA_CE_STREAM_TRACE_SAMPLE=4,
    KUI_ARMADA_CE_STREAM_TRACE_DISCOVERY=5,
    KUI_ARMADA_CE_STREAM_TRACE_OWNER_BIND=6,
    KUI_ARMADA_CE_STREAM_TRACE_WORKER=7
};
struct kui_armada_ce_stream_trace_snapshot {
    /* Chronological retained rows: code, native elapsed ms, fetched bytes,
     * fresh ring bytes, auxiliary (elapsed/reason/sample flags).
     * Sample flags: bit0 pending read, bit1 native running, bit2 urgent
     * audio arbitration, bits4-7 worker pump-budget yields (saturated at15),
     * bits8-15 state, bits16-31 worker visits since the
     * previous sample (use the MS difference for visits per second). */
    /* Startup codes5/6/7 have MS0 until a native clock exists; AUX is the
     * discovery/binding/native-start status (zero means successful admission). */
    /* START AUX: low16 bits are applied control/recovery-to-Play delay in ms
     * (saturated65535); high16 bits count cached64-byte blocks in this prime. */
    uint32_t events[KUI_ARMADA_CE_STREAM_TRACE_ROWS][5];
    uint32_t samples[KUI_ARMADA_CE_STREAM_TRACE_ROWS][5];
    /* Totals, longest worker interval, longest source job, completed jobs. */
    uint32_t counts[5];
};
/* Kept separate from the original terminal history ABI. Rows freeze at the
 * latest real underrun; later recovery and unrelated maxima cannot alter it.
 * 0: event ms, fresh bytes, cursor advance, staged bytes, physical-read age.
 * 1: worker-entry gap, requested sleep, entry-to-observation ms, source age,
 *    ring-publication age. Ages use only the existing worker clock samples.
 * 2: transport status, owner, checked bytes, total bytes, retries.
 * 3: DATA blocks, AUDIO blocks, preemptions, budget yields, copy retries;
 *    these are deltas from the immediately preceding 100ms history sample.
 * 4..6: chronological pre-underrun 100ms samples: ms/source/ready/staged/flags.
 * Flags: bits0/1/2 = active read/native running/urgent; bits8..15 model state,
 * bits16..17 transport owner, bits20..23 transport status.
 * UINT32_MAX marks unavailable ages/status/counters, never a zero-duration
 * measurement. With no active matching read, row2's other fields are zero
 * and row3's first three counters are unavailable.
 */
struct kui_armada_ce_stream_underrun_snapshot { uint32_t rows[7][5]; };
struct kui_armada_ce_stream_trace_supply {
    uint32_t status,owner,bytes,total,retries,data_blocks,audio_blocks,preemptions;
};
/* Worker-only bookkeeping. No native call, transport access or I/O occurs. */
void kui_armada_ce_stream_trace_worker_begin(uint32_t now);
void kui_armada_ce_stream_trace_sleep(uint32_t milliseconds);
void kui_armada_ce_stream_trace_source_progress(uint32_t now);
void kui_armada_ce_stream_trace_ring_progress(uint32_t now);
void kui_armada_ce_stream_trace_copy_retry(void);
void kui_armada_ce_stream_trace_observe(uint32_t now,uint32_t source,
    uint32_t ready,uint32_t delta,uint32_t staged,uint32_t flags,
    const struct kui_armada_ce_stream_trace_supply *,bool underrun);
void kui_armada_ce_stream_underrun_snapshot(struct kui_armada_ce_stream_underrun_snapshot *);
void kui_armada_ce_stream_trace_event(uint32_t code,uint32_t now,
    uint32_t source,uint32_t ready,uint32_t auxiliary);
void kui_armada_ce_stream_trace_sample(uint32_t now,uint32_t source,
    uint32_t ready,uint32_t pending);
/* A bounded worker read visit exhausted its pump budget while pending
 * AUDIO work could still continue without DMA. Pure integer state only. */
void kui_armada_ce_stream_trace_budget_yield(void);
void kui_armada_ce_stream_trace_source_begin(uint32_t now);
void kui_armada_ce_stream_trace_source_end(uint32_t now);
/* Terminal local copies only; no native calls, guest reads, clocks or I/O. */
void kui_armada_ce_stream_trace_snapshot(struct kui_armada_ce_stream_trace_snapshot *);
#endif
