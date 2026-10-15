/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_stream_trace.h"
#include <stddef.h>
static struct {
    struct kui_armada_ce_stream_trace_snapshot rows;
    uint32_t last_sample,last_visit,source_begin,visits,budget_yields;
    uint32_t have_sample,have_visit,source_active;
} trace;
static struct {
    struct kui_armada_ce_stream_underrun_snapshot frozen;
    uint32_t history[3][5],history_count,history_ms;
    uint32_t entry_ms,entry_gap,sleep,source_ms,ring_ms;
    uint32_t have_entry,have_source,have_ring;
    uint32_t yields,copy_retries,baseline[5],have_baseline;
} supply_trace;

void kui_armada_ce_stream_trace_worker_begin(uint32_t now) {
    supply_trace.entry_gap=supply_trace.have_entry?now-supply_trace.entry_ms:UINT32_MAX;
    supply_trace.entry_ms=now;supply_trace.have_entry=1u;
}
void kui_armada_ce_stream_trace_sleep(uint32_t milliseconds) { supply_trace.sleep=milliseconds; }
void kui_armada_ce_stream_trace_source_progress(uint32_t now) {
    supply_trace.source_ms=now;supply_trace.have_source=1u;
}
void kui_armada_ce_stream_trace_ring_progress(uint32_t now) {
    supply_trace.ring_ms=now;supply_trace.have_ring=1u;
}
void kui_armada_ce_stream_trace_copy_retry(void) { ++supply_trace.copy_retries; }
void kui_armada_ce_stream_trace_observe(uint32_t now,uint32_t source,
    uint32_t ready,uint32_t delta,uint32_t staged,uint32_t flags,
    const struct kui_armada_ce_stream_trace_supply *s,bool underrun) {
    bool active=s && (flags&1u);
    if(!active) flags&=~1u;
    if(active) flags|=(s->owner&3u)<<16|(s->status&15u)<<20;
    uint32_t counters[5]={active?s->data_blocks:0u,active?s->audio_blocks:0u,
        active?s->preemptions:0u,supply_trace.yields,supply_trace.copy_retries};
    if(underrun) {
        uint32_t (*out)[5]=supply_trace.frozen.rows;
        out[0][0]=now;out[0][1]=ready;out[0][2]=delta;out[0][3]=staged;
        out[0][4]=active && trace.source_active?now-trace.source_begin:UINT32_MAX;
        out[1][0]=supply_trace.have_entry?supply_trace.entry_gap:UINT32_MAX;
        out[1][1]=supply_trace.have_entry?supply_trace.sleep:UINT32_MAX;
        out[1][2]=supply_trace.have_entry?now-supply_trace.entry_ms:UINT32_MAX;
        out[1][3]=supply_trace.have_source?now-supply_trace.source_ms:UINT32_MAX;
        out[1][4]=supply_trace.have_ring?now-supply_trace.ring_ms:UINT32_MAX;
        out[2][0]=active?s->status:UINT32_MAX;out[2][1]=active?s->owner:0u;
        out[2][2]=active?s->bytes:0u;out[2][3]=active?s->total:0u;out[2][4]=active?s->retries:0u;
        for(unsigned i=0;i<5u;i++)
            out[3][i]=supply_trace.have_baseline && (i>=3u || (active && (supply_trace.have_baseline&2u)))?
                counters[i]-supply_trace.baseline[i]:UINT32_MAX;
        uint32_t count=supply_trace.history_count<3u?supply_trace.history_count:3u;
        uint32_t first=supply_trace.history_count-count;
        for(unsigned row=0;row<3u;row++) for(unsigned word=0;word<5u;word++)
            out[4u+row][word]=row<count?supply_trace.history[(first+row)%3u][word]:0u;
    }
    /* Freeze above before recording this observation: all three retained
     * history rows strictly precede the underrun, even on a 100ms boundary. */
    if(!supply_trace.history_count || now-supply_trace.history_ms>=100u) {
        uint32_t *row=supply_trace.history[supply_trace.history_count%3u];
        row[0]=now;row[1]=source;row[2]=ready;row[3]=staged;row[4]=flags;
        for(unsigned i=0;i<5u;i++) supply_trace.baseline[i]=counters[i];
        supply_trace.have_baseline=1u|(active?2u:0u);
        ++supply_trace.history_count;supply_trace.history_ms=now;
    }
}
void kui_armada_ce_stream_underrun_snapshot(struct kui_armada_ce_stream_underrun_snapshot *out) {
    if(!out) return;
    for(unsigned row=0;row<7u;row++) for(unsigned word=0;word<5u;word++)
        out->rows[row][word]=supply_trace.frozen.rows[row][word];
}
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
    ++supply_trace.yields;
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
