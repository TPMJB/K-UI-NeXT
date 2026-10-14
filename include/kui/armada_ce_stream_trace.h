/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_STREAM_TRACE_H
#define KUI_ARMADA_CE_STREAM_TRACE_H
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
     * Sample flags: bit0 pending read, bit1 native running, bits8+ state. */
    /* Startup codes5/6/7 have MS0 until a native clock exists; AUX is the
     * discovery/binding/native-start status (zero means successful admission). */
    uint32_t events[KUI_ARMADA_CE_STREAM_TRACE_ROWS][5];
    uint32_t samples[KUI_ARMADA_CE_STREAM_TRACE_ROWS][5];
    /* Totals, longest worker interval, longest source job, completed jobs. */
    uint32_t counts[5];
};
void kui_armada_ce_stream_trace_event(uint32_t code,uint32_t now,
    uint32_t source,uint32_t ready,uint32_t auxiliary);
void kui_armada_ce_stream_trace_sample(uint32_t now,uint32_t source,
    uint32_t ready,uint32_t pending);
void kui_armada_ce_stream_trace_source_begin(uint32_t now);
void kui_armada_ce_stream_trace_source_end(uint32_t now);
/* Terminal local copies only; no native calls, guest reads, clocks or I/O. */
void kui_armada_ce_stream_trace_snapshot(struct kui_armada_ce_stream_trace_snapshot *);
#endif
