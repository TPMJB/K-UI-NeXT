/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_profile.h"
#include "kui/toy_pilot_clock.h"

bool kui_vmu_native_profile_sample_valid(const struct kui_vmu_native_profile_sample *s) {
    return s && kui_toy_pilot_clock_profile(s->frqcr,s->tcor,s->tcr,s->tstr);
}
void kui_vmu_native_profile_begin(struct kui_vmu_native_profile_span *span,
    const struct kui_vmu_native_profile_sample *sample) {
    if(!span) return;
    *span=(struct kui_vmu_native_profile_span){0};
    if(sample) {
        span->begin=*sample;
        span->valid=kui_vmu_native_profile_sample_valid(sample)?1u:0u;
    }
}
static void add(struct kui_vmu_native_profile *p,uint32_t *value,
    uint32_t amount,uint32_t flag) {
    if(amount>UINT32_MAX-*value) {*value=UINT32_MAX;p->flags|=flag;}
    else *value+=amount;
}
bool kui_vmu_native_profile_end(struct kui_vmu_native_profile *p,
    enum kui_vmu_native_profile_kind kind,const struct kui_vmu_native_profile_span *span,
    const struct kui_vmu_native_profile_sample *sample) {
    if(!p || (unsigned)kind>=KUI_VMU_NATIVE_PROFILE_KINDS) return false;
    struct kui_vmu_native_profile_metric *m=&p->metric[kind];
    add(p,&m->calls,1u,KUI_VMU_NATIVE_PROFILE_CALLS_SATURATED);
    uint32_t elapsed=span && sample?span->begin.counter-sample->counter:0u;
    /* Every admitted clock control bit is fixed by the exact profile. Other
     * channels' TSTR bits and TMU0's underflow flag may change independently;
     * they are not part of this channel's rate/run/reload identity. */
    if(!span || span->valid!=1u || !kui_vmu_native_profile_sample_valid(&span->begin) ||
       !kui_vmu_native_profile_sample_valid(sample) ||
       elapsed>KUI_VMU_NATIVE_PROFILE_MAX_INTERVAL) {
        add(p,&m->invalid,1u,KUI_VMU_NATIVE_PROFILE_INVALID_SATURATED);
        return false;
    }
    add(p,&m->valid,1u,KUI_VMU_NATIVE_PROFILE_VALID_SATURATED);
    add(p,&m->ticks_total,elapsed,KUI_VMU_NATIVE_PROFILE_TICKS_SATURATED);
    if(elapsed>m->ticks_max) m->ticks_max=elapsed;
    return true;
}
