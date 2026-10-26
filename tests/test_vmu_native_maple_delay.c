/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_profile.h"
#include <assert.h>
#include <stdio.h>
#define NATIVE_MAPLE_DELAY_HOST_TEST 1
#include "vmu_native_maple_delay.inc"

static unsigned checks;
#define CHECK(value) do {++checks;assert(value);} while(0)
struct clock_model {
    struct kui_vmu_native_profile_sample begin,end;
    uint32_t current,step;
    unsigned calls,profiles,counters,fail_at;
    bool reverse,reset_end;
};
static struct clock_model clock_model(uint32_t counter,uint32_t step) {
    struct kui_vmu_native_profile_sample sample={0xe0au,UINT32_MAX,2u,1u,counter};
    return (struct clock_model){.begin=sample,.end=sample,.current=counter,.step=step};
}
static bool read_clock(void *context,bool profile,struct kui_vmu_native_profile_sample *sample) {
    struct clock_model *clock=context;
    ++clock->calls;
    if(clock->fail_at==clock->calls) return false;
    if(profile) {
        *sample=clock->profiles?clock->end:clock->begin;
        if(clock->profiles && clock->reset_end) clock->current=clock->begin.counter+1u;
        ++clock->profiles;
    } else ++clock->counters;
    if(clock->calls>1u) clock->current+=clock->reverse?clock->step:0u-clock->step;
    sample->counter=clock->current;
    return true;
}
static enum native_maple_delay_status run(struct clock_model *clock,uint32_t ticks,
    struct native_maple_delay_result *result) {
    return native_maple_delay_wait(ticks,read_clock,clock,result);
}
static void advancing(void) {
    const uint32_t targets[]={59u,76u};
    const uint32_t starts[]={0u,1u,58u,75u,UINT32_MAX,UINT32_C(0x80000000)};
    const uint32_t steps[]={1u,3u,17u,100u};
    for(unsigned a=0u;a<2u;a++) for(unsigned b=0u;b<6u;b++) for(unsigned c=0u;c<4u;c++) {
        struct clock_model clock=clock_model(starts[b],steps[c]);
        struct native_maple_delay_result result;
        CHECK(run(&clock,targets[a],&result)==NATIVE_MAPLE_DELAY_OK);
        CHECK(clock.profiles==2u);
        CHECK(clock.counters==(targets[a]+steps[c]-1u)/steps[c]);
        CHECK(result.polls==clock.counters);
        CHECK(result.elapsed>=targets[a]);
        CHECK(result.elapsed==(clock.counters+1u)*steps[c]);
        CHECK(clock.calls==clock.counters+2u);
    }
}
static void failed_reads(void) {
    const unsigned at[]={1u,2u,61u};
    const enum native_maple_delay_status expected[]={NATIVE_MAPLE_DELAY_BEGIN_READ,
        NATIVE_MAPLE_DELAY_COUNTER_READ,NATIVE_MAPLE_DELAY_END_READ};
    for(unsigned i=0u;i<3u;i++) {
        struct clock_model clock=clock_model(1000u,1u);clock.fail_at=at[i];
        struct native_maple_delay_result result;
        CHECK(run(&clock,59u,&result)==expected[i]);
        CHECK(clock.calls==at[i]);
    }
}
static void clock_refusals(void) {
    struct native_maple_delay_result result;
    struct clock_model clock=clock_model(1000u,0u);
    CHECK(run(&clock,59u,&result)==NATIVE_MAPLE_DELAY_FROZEN);
    CHECK(result.polls==4096u && clock.counters==4096u && clock.profiles==1u);
    CHECK(result.elapsed==0u);
    clock=clock_model(1000u,1u);clock.reverse=true;
    CHECK(run(&clock,76u,&result)==NATIVE_MAPLE_DELAY_COUNTER_RESET);
    CHECK(clock.counters==1u);
    clock=clock_model(1000u,1u);clock.reset_end=true;
    CHECK(run(&clock,59u,&result)==NATIVE_MAPLE_DELAY_COUNTER_RESET);
    clock=clock_model(1000u,1u);clock.end.counter=0u; /* Runtime counter remains independent. */
    clock.end.tstr=0u;
    CHECK(run(&clock,59u,&result)==NATIVE_MAPLE_DELAY_END_PROFILE);
    CHECK(clock.profiles==2u && result.polls==59u);
    for(unsigned i=0u;i<4u;i++) {
        clock=clock_model(1000u,1u);
        switch(i) {
        case 0u:clock.begin.frqcr^=2u;break;
        case 1u:clock.begin.tcor^=1u;break;
        case 2u:clock.begin.tcr^=1u;break;
        default:clock.begin.tstr=0u;break;
        }
        CHECK(run(&clock,59u,&result)==NATIVE_MAPLE_DELAY_BEGIN_PROFILE);
        CHECK(clock.calls==1u && clock.counters==0u);
        clock=clock_model(1000u,1u);
        switch(i) {
        case 0u:clock.end.frqcr^=2u;break;
        case 1u:clock.end.tcor^=1u;break;
        case 2u:clock.end.tcr^=1u;break;
        default:clock.end.tstr=0u;break;
        }
        CHECK(run(&clock,76u,&result)==NATIVE_MAPLE_DELAY_END_PROFILE);
        CHECK(clock.profiles==2u && clock.counters==76u);
    }
    /* Every admitted rate/reload/run identity bit is checked independently,
     * both before waiting and at the endpoint. */
    const uint32_t masks[]={0xfffu,UINT32_MAX,0x27u,1u};
    for(unsigned field=0u;field<4u;field++) for(unsigned bit=0u;bit<32u;bit++) {
        uint32_t mask=UINT32_C(1)<<bit;
        if(!(masks[field]&mask)) continue;
        for(unsigned endpoint=0u;endpoint<2u;endpoint++) {
            clock=clock_model(1000u,1u);
            struct kui_vmu_native_profile_sample *sample=endpoint?&clock.end:&clock.begin;
            if(field==0u) sample->frqcr^=mask;
            else if(field==1u) sample->tcor^=mask;
            else if(field==2u) sample->tcr^=mask;
            else sample->tstr^=mask;
            CHECK(run(&clock,59u,&result)==(endpoint?NATIVE_MAPLE_DELAY_END_PROFILE:
                NATIVE_MAPLE_DELAY_BEGIN_PROFILE));
            CHECK(clock.profiles==(endpoint?2u:1u));
        }
    }
    /* Other channels and the independently changing underflow flag do not
     * change TMU0's admitted rate, reload or run state. */
    clock=clock_model(1000u,1u);clock.end.tstr=7u;clock.end.tcr=0x102u;
    CHECK(run(&clock,59u,&result)==NATIVE_MAPLE_DELAY_OK);
    const uint32_t invalid[]={0u,1u,58u,60u,75u,77u,UINT32_MAX};
    for(unsigned i=0u;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        clock=clock_model(1000u,1u);
        CHECK(run(&clock,invalid[i],&result)==NATIVE_MAPLE_DELAY_BAD_TARGET);
        CHECK(clock.calls==0u && result.elapsed==0u && result.polls==0u);
    }
}
int main(void) {
    advancing();failed_reads();clock_refusals();
    printf("Maple delay: %u checks PASS\n",checks);
    return 0;
}
