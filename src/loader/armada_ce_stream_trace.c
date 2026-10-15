/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_stream_trace.h"
#include <stddef.h>
static struct {
    struct kui_armada_ce_stream_trace_snapshot rows;
    uint32_t last_sample,last_visit,source_begin,visits,budget_yields;
    uint32_t have_sample,have_visit,source_active;
} trace;
static void record(uint32_t rows[KUI_ARMADA_CE_STREAM_TRACE_ROWS][5],
    uint32_t total,uint32_t code,uint32_t now,uint32_t source,
    uint32_t ready,uint32_t auxiliary) {
    uint32_t *row=rows[total%KUI_ARMADA_CE_STREAM_TRACE_ROWS];
    row[0]=code;row[1]=now;row[2]=source;row[3]=ready;row[4]=auxiliary;
}
void kui_armada_ce_stream_trace_event(uint32_t code,uint32_t now,
    uint32_t source,uint32_t ready,uint32_t auxiliary) {
    record(trace.rows.events,trace.rows.counts[0],code,now,source,ready,auxiliary);
    ++trace.rows.counts[0];
}
void kui_armada_ce_stream_trace_budget_yield(void) {
    if(trace.budget_yields<15u) ++trace.budget_yields;
}
void kui_armada_ce_stream_trace_sample(uint32_t now,uint32_t source,
    uint32_t ready,uint32_t pending) {
    if(trace.have_visit && now-trace.last_visit>trace.rows.counts[2])
        trace.rows.counts[2]=now-trace.last_visit;
    trace.last_visit=now;trace.have_visit=1u;++trace.visits;
    if(trace.have_sample && now-trace.last_sample<1000u) return;
    /* AUX retains worker cadence and saturated pump-budget yields since
     * the previous recorded sample, including visits below the one-second
     * recording interval. */
    uint32_t visits=trace.visits>0xffffu?0xffffu:trace.visits;
    record(trace.rows.samples,trace.rows.counts[1],KUI_ARMADA_CE_STREAM_TRACE_SAMPLE,
        now,source,ready,(pending&0xff0fu)|(trace.budget_yields<<4)|(visits<<16));
    trace.visits=trace.budget_yields=0;
    ++trace.rows.counts[1];trace.last_sample=now;trace.have_sample=1u;
}
void kui_armada_ce_stream_trace_source_begin(uint32_t now) {
    trace.source_begin=now;trace.source_active=1u;
}
void kui_armada_ce_stream_trace_source_end(uint32_t now) {
    if(!trace.source_active) return;
    uint32_t elapsed=now-trace.source_begin;
    if(elapsed>trace.rows.counts[3]) trace.rows.counts[3]=elapsed;
    ++trace.rows.counts[4];trace.source_active=0u;
}
static void copy_rows(uint32_t to[KUI_ARMADA_CE_STREAM_TRACE_ROWS][5],
    uint32_t from[KUI_ARMADA_CE_STREAM_TRACE_ROWS][5],uint32_t total) {
    uint32_t count=total<KUI_ARMADA_CE_STREAM_TRACE_ROWS?total:KUI_ARMADA_CE_STREAM_TRACE_ROWS;
    uint32_t first=total-count;
    for(uint32_t row=0;row<KUI_ARMADA_CE_STREAM_TRACE_ROWS;row++)
        for(uint32_t word=0;word<5u;word++)
            to[row][word]=row<count?from[(first+row)%KUI_ARMADA_CE_STREAM_TRACE_ROWS][word]:0u;
}
void kui_armada_ce_stream_trace_snapshot(struct kui_armada_ce_stream_trace_snapshot *out) {
    if(!out) return;
    copy_rows(out->events,trace.rows.events,trace.rows.counts[0]);
    copy_rows(out->samples,trace.rows.samples,trace.rows.counts[1]);
    for(uint32_t i=0;i<5u;i++) out->counts[i]=trace.rows.counts[i];
}
