/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_phase_trace.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned samples;
void kui_smash_phase_clock_read(struct kui_smash_phase_stamp *stamp) {
    memset(stamp,0,sizeof(*stamp));stamp->flags=KUI_SMASH_PHASE_STOPPED_COUNTER;
    stamp->pmcr=0xc023u;stamp->frqcr=0xe0au;++samples;
}
static void refused_unchanged(struct kui_smash_phase_trace *trace,uint32_t phase) {
    struct kui_smash_phase_trace before=*trace;unsigned prior=samples;
    assert(!kui_smash_phase_trace_mark(trace,phase));
    assert(!memcmp(trace,&before,sizeof(*trace)) && prior==samples);
}
int main(void) {
    struct kui_smash_phase_trace trace={0};
    assert(!kui_smash_phase_trace_valid(&trace));
    assert(kui_smash_phase_trace_mark(&trace,1u) && samples==1u);
    assert(kui_smash_phase_trace_valid(&trace) && trace.count==1u && !trace.overflow);
    assert(trace.entries[0].phase==1u && trace.entries[0].stamp.flags==KUI_SMASH_PHASE_STOPPED_COUNTER);
    refused_unchanged(&trace,1u);refused_unchanged(&trace,0u);
    for(unsigned phase=6u;phase<=9u;phase++)refused_unchanged(&trace,phase);
    refused_unchanged(&trace,29u);
    kui_smash_phase_trace_init(&trace);assert(kui_smash_phase_trace_blank(&trace));
    for(unsigned phase=1u;phase<=28u;phase++)if(phase<=5u || phase>=10u)
        assert(kui_smash_phase_trace_mark(&trace,phase));
    assert(trace.count==24u && kui_smash_phase_trace_valid(&trace));
    refused_unchanged(&trace,28u);refused_unchanged(&trace,29u);
    struct kui_smash_phase_trace clean=trace;
    trace.reserved[1]=1u;assert(!kui_smash_phase_trace_valid(&trace));refused_unchanged(&trace,25u);
    trace=clean;trace.entries[4].reserved=1u;assert(!kui_smash_phase_trace_valid(&trace));
    trace=clean;trace.entries[4].stamp.reserved=1u;assert(!kui_smash_phase_trace_valid(&trace));
    trace=clean;trace.entries[4].phase=6u;assert(!kui_smash_phase_trace_valid(&trace));
    trace=clean;trace.entries[2].phase=trace.entries[1].phase;assert(!kui_smash_phase_trace_valid(&trace));
    trace=clean;trace.entries[23].phase=27u;assert(!kui_smash_phase_trace_valid(&trace));
    trace=clean;trace.count=25u;assert(!kui_smash_phase_trace_valid(&trace));refused_unchanged(&trace,25u);
    trace=clean;trace.overflow=2u;assert(!kui_smash_phase_trace_valid(&trace));
    memset(&trace,0,sizeof(trace));trace.reserved[0]=1u;refused_unchanged(&trace,1u);
    puts("PASS Smash phase trace:24 ordered checkpoints, unavailable clocks retained, malformed records/tails/IDs refused without mutation");
    return 0;
}
