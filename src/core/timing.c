/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/timing.h"
#include <string.h>

void kui_timing_start(struct kui_timing *t, uint64_t (*clock_us)(void *), void *ctx) {
    memset(t,0,sizeof(*t));t->clock_us=clock_us;t->ctx=ctx;
    if(clock_us) {t->mark=clock_us(ctx);t->active=true;}
}
void kui_timing_phase(struct kui_timing *t,enum kui_timing_phase phase) {
    if(!t->active || phase>=KUI_TIME_PHASES || phase==t->phase) return;
    uint64_t now=t->clock_us(t->ctx);
    t->elapsed[t->phase]+=now-t->mark;t->mark=now;t->phase=phase;
}
uint64_t kui_timing_begin(struct kui_timing *t) {
    return t->active?t->clock_us(t->ctx):0;
}
void kui_timing_end(struct kui_timing *t,enum kui_timing_bucket bucket,uint64_t start,uint64_t bytes) {
    if(!t->active || bucket>=KUI_TIME_BUCKETS) return;
    struct kui_timing_sample *s=&t->samples[t->phase][bucket];
    s->us+=t->clock_us(t->ctx)-start;s->bytes+=bytes;++s->calls;
}
void kui_timing_finish(struct kui_timing *t) {
    if(!t->active) return;
    t->elapsed[t->phase]+=t->clock_us(t->ctx)-t->mark;t->active=false;
}
