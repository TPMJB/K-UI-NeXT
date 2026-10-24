/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_READER_CAPTURE_H
#define KUI_NATIVE_READER_CAPTURE_H
#include <stdint.h>
#include <string.h>
#define KUI_READER_CAPTURE_TOP 4u
/* Observations only; no service, storage or MMIO authority. Counters are
 * modulo32, matching the existing reader counters. Timing requires valid
 * clock samples at both endpoints and an interval below half the counter. */
struct kui_reader_capture_sample {
    uint32_t tick,valid,call_max,irq_max,irq_blocks,call_blocks,polled;
    uint32_t crc,token_errors,foreign,repaired,overruns,deferred;
    uint32_t function,command,token,block,done,pending,pr;
};
struct kui_reader_capture_event {
    uint32_t gap,call_max,irq_max,irq_blocks,call_blocks,polled;
    uint32_t crc,token_errors,foreign,repaired,overruns,deferred;
    uint32_t function,command,token,block,done,pending,pr,previous_token;
};
struct kui_reader_capture {
    uint32_t samples,bad,have_previous,gap_max,call_max,irq_max,top_count;
    struct kui_reader_capture_sample first,previous;
    struct kui_reader_capture_event top[KUI_READER_CAPTURE_TOP];
};
static inline void kui_reader_capture_reset(struct kui_reader_capture *c) {
    memset(c,0,sizeof(*c));
}
static inline void kui_reader_capture_add(struct kui_reader_capture *c,
    const struct kui_reader_capture_sample *s) {
    ++c->samples;
    if(!c->have_previous) {c->first=*s;c->previous=*s;c->have_previous=1u;return;}
    const struct kui_reader_capture_sample *p=&c->previous;
    uint32_t gap=p->tick-s->tick;
    if(!p->valid || !s->valid || gap>UINT32_C(0x7fffffff) ||
       s->call_max>UINT32_C(0x7fffffff) || s->irq_max>UINT32_C(0x7fffffff)) {
        ++c->bad;c->previous=*s;return;
    }
    if(gap>c->gap_max)c->gap_max=gap;
    if(s->call_max>c->call_max)c->call_max=s->call_max;
    if(s->irq_max>c->irq_max)c->irq_max=s->irq_max;
    unsigned at=0u;
    while(at<c->top_count && c->top[at].gap>=gap)++at;
    if(at<KUI_READER_CAPTURE_TOP) {
        if(c->top_count<KUI_READER_CAPTURE_TOP)++c->top_count;
        for(unsigned i=c->top_count-1u;i>at;--i)c->top[i]=c->top[i-1u];
        c->top[at]=(struct kui_reader_capture_event){
            .gap=gap,.call_max=s->call_max,.irq_max=s->irq_max,
            .irq_blocks=s->irq_blocks-p->irq_blocks,.call_blocks=s->call_blocks-p->call_blocks,
            .polled=s->polled-p->polled,.crc=s->crc-p->crc,
            .token_errors=s->token_errors-p->token_errors,.foreign=s->foreign-p->foreign,
            .repaired=s->repaired-p->repaired,.overruns=s->overruns-p->overruns,
            .deferred=s->deferred-p->deferred,.function=s->function,.command=s->command,
            .token=s->token,.block=s->block,.done=s->done,.pending=s->pending,
            .pr=s->pr,.previous_token=p->token};
    }
    c->previous=*s;
}
#endif
