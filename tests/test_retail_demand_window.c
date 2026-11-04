/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_demand_window.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {assert(x);++checks;} while(0)
static struct kui_retail_demand_clock clock_at(uint64_t cycles) {
    return (struct kui_retail_demand_clock){900u,1000u,2u,1u,0xe0au,0xc023u,
        (uint32_t)cycles,(uint32_t)(cycles>>32)};
}
static void record(struct kui_retail_demand_window *w,uint64_t begin,uint64_t end,
    struct kui_retail_demand_window_visit v) {
    struct kui_retail_demand_clock a=clock_at(begin),b=clock_at(end);
    kui_retail_demand_window_record(w,&a,&b,&v);
}
static uint64_t total(uint32_t lo,uint32_t hi) {return (uint64_t)hi<<32|lo;}
static void boundaries(void) {
    struct kui_retail_demand_window w={0};
    struct kui_retail_demand_window_visit v={0u,16u,2u,3u,100u,0x400000f0u,1u,0u};
    record(&w,0,1,v);
    CHECK(kui_retail_demand_window_count(&w)==1u && w.epoch==1u && w.sequence==1u);
    const struct kui_retail_demand_window_bin *b=kui_retail_demand_window_oldest(&w,0u);
    CHECK(b && b->gd==1u && b->irq==0u && b->modes==1u && b->span_cycles==1u);
    CHECK(b->gd_lo==1u && b->masked==1u && b->dma_zero==1u && b->bytes==100u);
    record(&w,10u,400000000u,v);
    b=kui_retail_demand_window_oldest(&w,0u);
    CHECK(w.count==1u && b->gd==2u && b->span_cycles==400000000u);
    v.command=17u;v.kind=1u;v.no_progress=1u;
    record(&w,400000001u,400000002u,v);
    b=kui_retail_demand_window_oldest(&w,1u);
    CHECK(w.count==2u && b->ordinal==1u && b->span_cycles==2u && b->irq==1u);
    CHECK(b->modes==2u && b->zero==1u && b->masked==0u);
    v.command=38u;v.kind=0u;
    record(&w,799999990u,800000010u,v);
    b=kui_retail_demand_window_oldest(&w,2u);
    CHECK(b && b->span_cycles==10u && b->gd_lo==20u && b->modes==4u);
    CHECK(b->flags&KUI_RETAIL_DEMAND_WINDOW_CROSSED_BIN);
    CHECK(kui_retail_demand_window_oldest(&w,3u)==NULL);
}
static void rolling_and_freeze(void) {
    struct kui_retail_demand_window w={0};
    static const uint32_t modes[4]={16u,17u,38u,39u};
    for(unsigned i=0;i<12u;i++) {
        struct kui_retail_demand_window_visit v={i&1u,modes[i&3u],i,i+1u,100u+i,
            i&1u?0x40000000u:0x400000f0u,0u,0u};
        record(&w,(uint64_t)i*400000000u+2u,(uint64_t)i*400000000u+22u,v);
    }
    CHECK(w.count==8u);
    for(unsigned i=0;i<8u;i++) {
        const struct kui_retail_demand_window_bin *b=kui_retail_demand_window_oldest(&w,i);
        unsigned ordinal=i+4u;
        CHECK(b->ordinal==ordinal && b->first_seq==ordinal+1u && b->last_seq==ordinal+1u);
        CHECK(b->gd==(ordinal%2u==0u) && b->irq==(ordinal%2u==1u));
        CHECK(b->modes==(1u<<(ordinal&3u)) && b->bytes==100u+ordinal);
        CHECK(total(b->gd_lo,b->gd_hi)+total(b->irq_lo,b->irq_hi)==20u);
    }
    struct kui_retail_demand_clock now=clock_at(UINT64_C(50000000000));
    kui_retail_demand_window_freeze(&w,&now);
    CHECK(w.frozen && w.count==8u && (w.flags&KUI_RETAIL_DEMAND_WINDOW_REALIGNED));
    for(unsigned i=0;i<8u;i++) {
        const struct kui_retail_demand_window_bin *b=kui_retail_demand_window_oldest(&w,i);
        CHECK(b->span_cycles==400000000u && !b->gd && !b->irq && !b->bytes);
    }
    struct kui_retail_demand_window saved=w;
    struct kui_retail_demand_window_visit v={0};
    record(&w,1u,2u,v);kui_retail_demand_window_freeze(&w,NULL);
    CHECK(!memcmp(&w,&saved,sizeof(w)));
}
static void clock_and_invalid(void) {
    struct kui_retail_demand_window w={0};
    struct kui_retail_demand_window_visit v={0u,39u,1u,2u,10u,0x40000000u,0u,1u};
    record(&w,UINT64_C(0xfffffff0),UINT64_C(0x100000054),v);
    const struct kui_retail_demand_window_bin *b=kui_retail_demand_window_oldest(&w,0u);
    CHECK(b && b->gd_lo==100u && !b->gd_hi && b->span_cycles==100u && !w.bad_clock);
    struct kui_retail_demand_clock begin=clock_at(UINT64_C(0x100000060));
    struct kui_retail_demand_clock end=clock_at(UINT64_C(0x100000080));end.pmcr=0u;
    kui_retail_demand_window_record(&w,&begin,&end,&v);
    CHECK(w.count==0u && w.bad_clock==1u && (w.flags&KUI_RETAIL_DEMAND_WINDOW_CLOCK_INVALID));
    CHECK(w.untimed.gd==1u && w.untimed.bytes==10u && w.untimed.first_seq==2u);
    CHECK(w.untimed.span_cycles==0u && !w.untimed.gd_lo && !w.untimed.gd_hi);
    record(&w,UINT64_C(0x100000090),UINT64_C(0x1000000a0),v);
    b=kui_retail_demand_window_oldest(&w,0u);
    CHECK(w.epoch==2u && w.count==1u && b->gd_lo==16u && b->first_seq==3u);
    CHECK(w.untimed.gd==1u && w.bad_clock==1u);
    record(&w,UINT64_C(0x10000009f),UINT64_C(0x1000000b0),v);
    CHECK(w.flags&KUI_RETAIL_DEMAND_WINDOW_OVERLAP);
    CHECK(w.count==0u && w.bad_clock==2u && w.untimed.gd==2u);
    for(unsigned mode=0;mode<4u;mode++) {
        begin=clock_at(100u);end=clock_at(200u);
        if(mode==0u) end.frqcr^=1u;
        if(mode==1u) end.pmcr=0xe023u;
        if(mode==2u) end.cycles_hi=0x10000u;
        if(mode==3u) end.cycles_lo=50u;
        kui_retail_demand_window_record(&w,&begin,&end,&v);
    }
    CHECK(w.bad_clock==6u && !w.count && w.untimed.gd==6u);
    record(&w,100u,100u,v);CHECK(w.bad_clock==7u);
    struct kui_retail_demand_window fresh={0};
    record(&fresh,100u,200u,v);unsigned span=fresh.bin[0].span_cycles;
    end=clock_at(300u);end.pmcr=0u;kui_retail_demand_window_freeze(&fresh,&end);
    CHECK(fresh.frozen && fresh.bin[0].span_cycles==span);
    CHECK(fresh.flags&KUI_RETAIL_DEMAND_WINDOW_CLOCK_INVALID);
}
static void long_and_saturation(void) {
    struct kui_retail_demand_window w={0};
    struct kui_retail_demand_window_visit v={0u,16u,0u,0u,0u,0u,0u,0u};
    record(&w,0u,UINT64_C(0x100000001),v);
    const struct kui_retail_demand_window_bin *b=kui_retail_demand_window_oldest(&w,7u);
    CHECK(w.count==8u && b->gd_lo==1u && b->gd_hi==1u && b->max_body==UINT32_MAX);
    CHECK((b->flags&(KUI_RETAIL_DEMAND_WINDOW_BODY_CLIPPED|KUI_RETAIL_DEMAND_WINDOW_CROSSED_BIN))==20u);
    uint32_t lo=UINT32_C(0xfffffff0),hi=5u,flags=0u;
    kui_retail_demand_window_sum(&lo,&hi,0x40u,0u,&flags);
    CHECK(lo==0x30u && hi==6u && !flags);
    lo=hi=UINT32_MAX;kui_retail_demand_window_sum(&lo,&hi,1u,0u,&flags);
    CHECK(lo==UINT32_MAX && hi==UINT32_MAX && (flags&KUI_RETAIL_DEMAND_WINDOW_SATURATED));
    uint32_t n=UINT32_MAX-1u;flags=0u;kui_retail_demand_window_add(&n,2u,&flags);
    CHECK(n==UINT32_MAX && (flags&KUI_RETAIL_DEMAND_WINDOW_SATURATED));
    CHECK(kui_retail_demand_window_count(NULL)==0u && kui_retail_demand_window_oldest(NULL,0u)==NULL);
    kui_retail_demand_window_record(NULL,NULL,NULL,&v);kui_retail_demand_window_freeze(NULL,NULL);
}
static uint32_t random_next(uint32_t *state) {*state=*state*1664525u+1013904223u;return *state;}
static void independent_replay(void) {
    /* Reference bins use absolute uint64 host arithmetic/division. The
     * production header intentionally uses neither64-bit division nor a
     * loop proportional to elapsed time. */
    struct ref {uint32_t gd,irq,modes,bytes,ib,cb,masked,zero,dma,max,gap;uint64_t gc,ic;} refs[64]={{0}};
    struct kui_retail_demand_window w={0};uint32_t seed=91u;
    uint64_t first=100u,previous_begin=first,previous_end=first,last=0u;
    for(unsigned i=0;i<1000u;i++) {
        uint64_t begin=i?previous_end+random_next(&seed)%5000000u:first;
        uint64_t duration=1u+random_next(&seed)%1000000u,end=begin+duration;
        unsigned ordinal=(unsigned)((end-first-1u)/400000000u);
        CHECK(ordinal<64u);
        static const uint32_t commands[4]={16u,17u,38u,39u};
        struct kui_retail_demand_window_visit v={i&1u,commands[i&3u],i%3u,i%5u,
            i%2048u,(i&2u)?0x400000f0u:0x40000000u,i%7u==0u,i%11u==0u};
        struct ref *r=&refs[ordinal];if(v.kind) {++r->irq;r->ic+=duration;}else {++r->gd;r->gc+=duration;}
        r->modes|=1u<<(i&3u);r->bytes+=v.output_bytes;r->ib+=v.irq_blocks;r->cb+=v.call_blocks;
        if(!v.kind && (v.caller_sr&0xf0u)) ++r->masked;
        if(v.pending_dma_zero) ++r->dma;
        if(v.no_progress) ++r->zero;
        if(duration>r->max) r->max=(uint32_t)duration;
        uint32_t gap=i?(uint32_t)(begin-previous_begin):0u;if(gap>r->gap) r->gap=gap;
        record(&w,begin,end,v);previous_begin=begin;previous_end=end;last=end;
        unsigned count=ordinal+1u<8u?ordinal+1u:8u;
        CHECK(w.count==count && !w.bad_clock);
        for(unsigned j=0;j<count;j++) {
            unsigned k=ordinal+1u-count+j;const struct ref *x=&refs[k];
            const struct kui_retail_demand_window_bin *b=kui_retail_demand_window_oldest(&w,j);
            CHECK(b && b->ordinal==k && b->gd==x->gd && b->irq==x->irq && b->modes==x->modes);
            CHECK(total(b->gd_lo,b->gd_hi)==x->gc && total(b->irq_lo,b->irq_hi)==x->ic);
            CHECK(b->bytes==x->bytes && b->irq_blocks==x->ib && b->call_blocks==x->cb);
            CHECK(b->masked==x->masked && b->zero==x->zero && b->dma_zero==x->dma);
            CHECK(b->max_body==x->max && b->gap_max==x->gap);
        }
    }
    struct kui_retail_demand_clock now=clock_at(last+300000000u);
    kui_retail_demand_window_freeze(&w,&now);CHECK(w.frozen && w.count==8u);
}
int main(void) {
    boundaries();rolling_and_freeze();clock_and_invalid();long_and_saturation();independent_replay();
    printf("CE rolling demand window: %u independent contract checks OK\n",checks);
    return 0;
}
