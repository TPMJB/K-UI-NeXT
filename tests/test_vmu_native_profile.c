/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(test) do {++checks;assert(test);} while(0)
static struct kui_vmu_native_profile_sample clock_at(uint32_t counter) {
    return (struct kui_vmu_native_profile_sample){0xe0au,UINT32_MAX,2u,1u,counter};
}
static void admission(void) {
    struct kui_vmu_native_profile_sample good=clock_at(0u);
    CHECK(kui_vmu_native_profile_sample_valid(&good));
    CHECK(!kui_vmu_native_profile_sample_valid(NULL));
    for(unsigned field=0;field<5u;field++) for(unsigned bit=0;bit<32u;bit++) {
        struct kui_vmu_native_profile_sample bad=good;
        uint32_t words[5];memcpy(words,&bad,sizeof(words));words[field]^=UINT32_C(1)<<bit;
        memcpy(&bad,words,sizeof(bad));
        /* Independently specified permitted changes: FRQCR reserved high
         * bits, unrelated TCR flags, other channels' run bits, and counter. */
        bool permitted=field==4u || (field==0u && bit>=12u) ||
            (field==2u && bit!=0u && bit!=1u && bit!=2u && bit!=5u) ||
            (field==3u && bit!=0u);
        CHECK(kui_vmu_native_profile_sample_valid(&bad)==permitted);
    }
}
static void durations(void) {
    for(unsigned kind=0;kind<KUI_VMU_NATIVE_PROFILE_KINDS;kind++) {
        struct kui_vmu_native_profile p={0};struct kui_vmu_native_profile_span span;
        struct kui_vmu_native_profile_sample first=clock_at(1000u),last=clock_at(250u);
        kui_vmu_native_profile_begin(&span,&first);
        CHECK(kui_vmu_native_profile_end(&p,(enum kui_vmu_native_profile_kind)kind,&span,&last));
        CHECK(p.metric[kind].calls==1u && p.metric[kind].valid==1u && !p.metric[kind].invalid);
        CHECK(p.metric[kind].ticks_total==750u && p.metric[kind].ticks_max==750u && !p.flags);
        first.counter=3u;last.counter=UINT32_MAX-8u;
        kui_vmu_native_profile_begin(&span,&first);
        CHECK(kui_vmu_native_profile_end(&p,(enum kui_vmu_native_profile_kind)kind,&span,&last));
        CHECK(p.metric[kind].ticks_total==762u && p.metric[kind].ticks_max==750u);
        /* Zero duration is a valid sub-tick measurement, not a guessed tick. */
        first.counter=last.counter;kui_vmu_native_profile_begin(&span,&first);
        CHECK(kui_vmu_native_profile_end(&p,(enum kui_vmu_native_profile_kind)kind,&span,&last));
        CHECK(p.metric[kind].ticks_total==762u && p.metric[kind].valid==3u);
        first.counter=UINT32_MAX;last.counter=UINT32_MAX-KUI_VMU_NATIVE_PROFILE_MAX_INTERVAL;
        kui_vmu_native_profile_begin(&span,&first);
        CHECK(kui_vmu_native_profile_end(&p,(enum kui_vmu_native_profile_kind)kind,&span,&last));
        CHECK(p.metric[kind].ticks_max==KUI_VMU_NATIVE_PROFILE_MAX_INTERVAL);
        --last.counter;
        CHECK(!kui_vmu_native_profile_end(&p,(enum kui_vmu_native_profile_kind)kind,&span,&last));
        CHECK(p.metric[kind].calls==5u && p.metric[kind].valid==4u && p.metric[kind].invalid==1u);
        for(unsigned other=0;other<KUI_VMU_NATIVE_PROFILE_KINDS;other++)
            if(other!=kind) CHECK(!p.metric[other].calls);
    }
}
static void rejected_clocks(void) {
    struct kui_vmu_native_profile_sample good=clock_at(10000u),last=clock_at(9000u);
    for(unsigned endpoint=0;endpoint<2u;endpoint++)
        for(unsigned field=0;field<4u;field++) for(unsigned bit=0;bit<32u;bit++) {
            struct kui_vmu_native_profile p={0};struct kui_vmu_native_profile_span span;
            struct kui_vmu_native_profile_sample first=good,end=last;
            struct kui_vmu_native_profile_sample *which=endpoint?&end:&first;
            uint32_t words[5];memcpy(words,which,sizeof(words));words[field]^=UINT32_C(1)<<bit;
            memcpy(which,words,sizeof(*which));
            bool accepted=kui_vmu_native_profile_sample_valid(&first) &&
                kui_vmu_native_profile_sample_valid(&end);
            kui_vmu_native_profile_begin(&span,&first);
            CHECK(kui_vmu_native_profile_end(&p,KUI_VMU_NATIVE_PROFILE_COMPLETE,&span,&end)==accepted);
            CHECK(p.metric[1].calls==1u && p.metric[1].valid==(accepted?1u:0u) &&
                  p.metric[1].invalid==(accepted?0u:1u));
            CHECK(p.metric[1].ticks_total==(accepted?1000u:0u));
        }
    struct kui_vmu_native_profile p={0};struct kui_vmu_native_profile_span span;
    kui_vmu_native_profile_begin(NULL,&good);
    kui_vmu_native_profile_begin(&span,NULL);CHECK(!span.valid);
    CHECK(!kui_vmu_native_profile_end(&p,KUI_VMU_NATIVE_PROFILE_FREE,&span,&last));
    kui_vmu_native_profile_begin(&span,&good);span.valid=2u;
    CHECK(!kui_vmu_native_profile_end(&p,KUI_VMU_NATIVE_PROFILE_FREE,&span,&last));
    CHECK(!kui_vmu_native_profile_end(&p,KUI_VMU_NATIVE_PROFILE_FREE,NULL,&last));
    CHECK(!kui_vmu_native_profile_end(&p,KUI_VMU_NATIVE_PROFILE_FREE,&span,NULL));
    CHECK(p.metric[2].calls==4u && p.metric[2].invalid==4u && !p.metric[2].ticks_total);
    struct kui_vmu_native_profile before=p;
    CHECK(!kui_vmu_native_profile_end(&p,(enum kui_vmu_native_profile_kind)-1,&span,&last));
    CHECK(!kui_vmu_native_profile_end(&p,KUI_VMU_NATIVE_PROFILE_KINDS,&span,&last));
    CHECK(!memcmp(&p,&before,sizeof(p)));
    CHECK(!kui_vmu_native_profile_end(NULL,KUI_VMU_NATIVE_PROFILE_SUBMIT,&span,&last));
}
static void saturation(void) {
    struct kui_vmu_native_profile_sample first=clock_at(50u),last=clock_at(40u);
    struct kui_vmu_native_profile_span span;kui_vmu_native_profile_begin(&span,&first);
    struct kui_vmu_native_profile p={0};
    struct kui_vmu_native_profile_metric *m=&p.metric[KUI_VMU_NATIVE_PROFILE_STORAGE];
    m->calls=UINT32_MAX;m->valid=UINT32_MAX;m->ticks_total=UINT32_MAX-5u;
    CHECK(kui_vmu_native_profile_end(&p,KUI_VMU_NATIVE_PROFILE_STORAGE,&span,&last));
    CHECK(m->calls==UINT32_MAX && m->valid==UINT32_MAX && m->ticks_total==UINT32_MAX && m->ticks_max==10u);
    CHECK(p.flags==(KUI_VMU_NATIVE_PROFILE_CALLS_SATURATED|KUI_VMU_NATIVE_PROFILE_VALID_SATURATED|
                    KUI_VMU_NATIVE_PROFILE_TICKS_SATURATED));
    m->invalid=UINT32_MAX;last.tstr=0u;
    CHECK(!kui_vmu_native_profile_end(&p,KUI_VMU_NATIVE_PROFILE_STORAGE,&span,&last));
    CHECK(m->invalid==UINT32_MAX && p.flags==15u);
    /* Reaching the maximum exactly is representable and is not overflow. */
    p=(struct kui_vmu_native_profile){0};m=&p.metric[KUI_VMU_NATIVE_PROFILE_STORAGE];
    m->calls=UINT32_MAX-1u;m->valid=UINT32_MAX-1u;m->ticks_total=UINT32_MAX-10u;
    last=clock_at(40u);CHECK(kui_vmu_native_profile_end(&p,KUI_VMU_NATIVE_PROFILE_STORAGE,&span,&last));
    CHECK(m->ticks_total==UINT32_MAX && m->calls==UINT32_MAX && m->valid==UINT32_MAX && !p.flags);
}
int main(void) {
    admission();durations();rejected_clocks();saturation();
    printf("Native read-only profile: %u checks; admitted owner clock, wraps, invalid spans and saturation\n",checks);
    return 0;
}
