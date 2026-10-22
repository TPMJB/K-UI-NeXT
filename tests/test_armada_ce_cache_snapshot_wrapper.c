/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_maple_rx_slice.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if !KUI_ARMADA_CE_CACHE_SNAPSHOT_NATIVE || !KUI_ARMADA_CE_MAPLE_A2
#error This harness must compile the production native header routes
#endif
static const uint32_t VA=0x080325e0u,P1=0x8c2345e0u,PAGE=0x0c23417du;
static unsigned checks,scenarios,leaf_calls,leaf_status;
static uint32_t expected_mask;
static struct kui_armada_ce_maple_traffic_scratch traffic_work;
static struct kui_armada_ce_maple_rx_slice_scratch rx_work;
static struct kui_armada_ce_maple_traffic_scratch capture;
struct kui_armada_ce_cache_snapshot_job kui_armada_ce_cache_snapshot_job;
#define CHECK(c) do {++checks;assert(c);} while(0)
static const uint32_t details[3]={0xf40005e0u,0x12345679u,0x12345678u};
static const uint32_t latched[5]={42u,0x11111111u,0x22222222u,0x33333333u,0x44444444u};
static unsigned bits(uint32_t n) {unsigned count=0;for(;n;n>>=1) count+=n&1u;return count;}
static uint32_t result_word(unsigned i) {return 0xcafe1000u+i;}
static bool unexpected_read(void *context,uint32_t at,uint32_t *value) {
    (void)context;(void)at;(void)value;CHECK(0);return false;
}
static struct kui_armada_ce_maple_traffic_io traffic_io={
    unexpected_read,unexpected_read,unexpected_read,NULL,&traffic_work};
static struct kui_armada_ce_maple_rx_slice_io rx_io={
    unexpected_read,unexpected_read,unexpected_read,NULL,&capture,&rx_work,0,0,0x105u,0};

/* Emulate only the native leaf boundary. Its machine implementation is
 * separately modeled/audited; this harness compiles the actual C wrapper. */
unsigned kui_armada_ce_cache_snapshot(struct kui_armada_ce_cache_snapshot_job *job) {
    ++leaf_calls;
    CHECK(job==&kui_armada_ce_cache_snapshot_job);
    CHECK(job->va==VA && job->p1==P1 && job->page==PAGE && job->mask==expected_mask);
    for(unsigned i=0;i<3u;i++) CHECK(job->detail[i]==0u);
    for(unsigned i=0;i<8u;i++) job->word[i]=result_word(i);
    for(unsigned i=0;i<3u;i++) job->detail[i]=details[i];
    return leaf_status;
}
#include "armada-ce-cache-snapshot-wrapper.inc"

static void prepare(uint32_t reads,bool previous_fault) {
    memset(&traffic_work,0,sizeof(traffic_work));memset(&rx_work,0,sizeof(rx_work));
    memset(&kui_armada_ce_cache_snapshot_job,0xa5,sizeof(kui_armada_ce_cache_snapshot_job));
    traffic_work.reads=rx_work.reads=reads;leaf_calls=0;
    if(previous_fault) {
        memcpy(traffic_work.error,latched,sizeof(latched));memcpy(rx_work.error,latched,sizeof(latched));
    }
}
/* Route0=common traffic through cache_line;1=RX through cache_line;
 *2=the actual native RX line-read entry through both wrappers. */
static unsigned invoke(unsigned route,uint32_t mask,uint32_t out[8]) {
    if(route==2u) return kui_armada_ce_maple_rx_slice_line_read(&rx_io,VA,P1,PAGE,mask,out)?0u:1u;
    return kui_armada_ce_cache_line(VA,P1,PAGE,0x105u,mask,
        route?kui_armada_ce_maple_rx_slice_cache:kui_armada_ce_maple_traffic_cache,
        route?(void *)&rx_io:(void *)&traffic_io,out);
}
static void matrix(void) {
    static const unsigned statuses[]={0u,1u,2u,3u,99u};
    uint32_t out[8];
    for(unsigned route=0;route<3u;route++) for(uint32_t mask=1;mask<256u;mask++) {
        const uint32_t limit=route?KUI_ARMADA_CE_MAPLE_RX_SLICE_READ_LIMIT:KUI_ARMADA_CE_MAPLE_TRAFFIC_READ_LIMIT;
        const uint32_t cost=8u+2u*bits(mask);
        /* Available budgets0,cost-1,cost,cost+1, full budget; plus already
         * invalid counters above the limit and UINT32_MAX. */
        const uint32_t reads[]={limit,limit-(cost-1u),limit-cost,limit-(cost+1u),0u,limit+1u,UINT32_MAX};
        for(unsigned boundary=0;boundary<7u;boundary++) for(unsigned s=0;s<5u;s++)
            for(unsigned previous=0;previous<2u;previous++) {
                ++scenarios;prepare(reads[boundary],previous!=0u);
                expected_mask=mask;leaf_status=statuses[s];
                for(unsigned i=0;i<8u;i++) out[i]=0xa5a5a5a5u;
                unsigned result=invoke(route,mask,out);
                uint32_t *error=route?rx_work.error:traffic_work.error;
                uint32_t after=route?rx_work.reads:traffic_work.reads;
                bool denied=reads[boundary]>limit || cost>limit-reads[boundary];
                CHECK(leaf_calls==(denied?0u:1u));
                CHECK(after==reads[boundary]+(denied?0u:cost));
                if(route) CHECK(traffic_work.reads==reads[boundary]);else CHECK(rx_work.reads==reads[boundary]);
                if(denied || leaf_status) {
                    CHECK(result!=0u);for(unsigned i=0;i<8u;i++) CHECK(out[i]==0u);
                } else {
                    CHECK(result==0u);
                    for(unsigned i=0;i<8u;i++) CHECK(out[i]==((mask&(1u<<i))?result_word(i):0u));
                }
                if(previous) CHECK(!memcmp(error,latched,sizeof(latched)));
                else if(denied) {
                    CHECK(error[0]==(route?KUI_ARMADA_CE_MAPLE_RX_SLICE_BUDGET:KUI_ARMADA_CE_MAPLE_TRAFFIC_BUDGET));
                    CHECK(error[1]==VA && error[2]==reads[boundary] && error[3]==limit && error[4]==cost);
                } else if(leaf_status) {
                    CHECK(error[0]==(leaf_status==2u?
                        (route?KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED:KUI_ARMADA_CE_MAPLE_TRAFFIC_CHANGED):
                        (route?KUI_ARMADA_CE_MAPLE_RX_SLICE_CACHE:KUI_ARMADA_CE_MAPLE_TRAFFIC_CACHE)));
                    CHECK(!memcmp(error+1,details,sizeof(details)) && error[4]==mask);
                } else CHECK(error[0]==0u);
                if(route!=2u) CHECK(result==(denied?KUI_ARMADA_CE_RAM_BIND_READ:
                    leaf_status==0u?KUI_ARMADA_CE_RAM_BIND_OK:
                    leaf_status==1u?KUI_ARMADA_CE_RAM_BIND_CACHE:
                    leaf_status==2u?KUI_ARMADA_CE_RAM_BIND_CHANGED:KUI_ARMADA_CE_RAM_BIND_ARGUMENT));
            }
    }
}
static void invalid_inputs(void) {
    static const struct {uint32_t va,p1,page,ccr,mask;} bad[]={
        {0x080325e4u,0x8c2345e0u,0x0c23417du,0x105u,1u},
        {0x880325e0u,0x8c2345e0u,0x0c23417du,0x105u,1u},
        {0x08032600u,0x8c2345e0u,0x0c23417du,0x105u,1u},
        {0x080325e0u,0x8c2345e4u,0x0c23417du,0x105u,1u},
        {0x080325e0u,0xac2345e0u,0x0c23417du,0x105u,1u},
        {0x080325e0u,0x8c0045e0u,0x0c00417du,0x105u,1u},
        {0x080325e0u,0x8c2345e0u,0x0c23417cu,0x105u,1u},
        {0x080325e0u,0x8c2345e0u,0x0c23517du,0x105u,1u},
        {0x080325e0u,0x8c2345e0u,0x0c23417du,0x125u,1u},
        {0x080325e0u,0x8c2345e0u,0x0c23417du,0x105u,0u},
        {0x080325e0u,0x8c2345e0u,0x0c23417du,0x105u,256u},
        {0x080325e0u,0x8c2345e0u,0x0c23417du,0x105u,UINT32_MAX}
    };
    uint32_t out[8];
    for(unsigned route=0;route<3u;route++) for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        prepare(0u,false);rx_io.ccr=bad[i].ccr;
        for(unsigned word=0;word<8u;word++) out[word]=UINT32_MAX;
        unsigned status=route==2u?
            (kui_armada_ce_maple_rx_slice_line_read(&rx_io,bad[i].va,bad[i].p1,bad[i].page,bad[i].mask,out)?0u:1u):
            kui_armada_ce_cache_line(bad[i].va,bad[i].p1,bad[i].page,bad[i].ccr,bad[i].mask,
                route?kui_armada_ce_maple_rx_slice_cache:kui_armada_ce_maple_traffic_cache,
                route?(void *)&rx_io:(void *)&traffic_io,out);
        CHECK(status!=0u && leaf_calls==0u && traffic_work.reads==0u && rx_work.reads==0u);
        for(unsigned word=0;word<8u;word++) CHECK(out[word]==0u);
    }
    rx_io.ccr=0x105u;
    for(unsigned previous=0;previous<2u;previous++) {
        prepare(0u,previous!=0u);
        CHECK(kui_armada_ce_cache_line(VA,P1,PAGE,0x105u,255u,unexpected_read,&traffic_io,out)==
            KUI_ARMADA_CE_RAM_BIND_ARGUMENT);
        CHECK(leaf_calls==0u && traffic_work.reads==0u && rx_work.reads==0u);
        for(unsigned i=0;i<8u;i++) CHECK(out[i]==0u);
        CHECK(previous?!memcmp(traffic_work.error,latched,sizeof(latched)):traffic_work.error[0]==0u);
    }
    prepare(0u,false);
    CHECK(kui_armada_ce_cache_line(VA,P1,PAGE,0x105u,1u,NULL,&traffic_io,out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);
    CHECK(kui_armada_ce_cache_line(VA,P1,PAGE,0x105u,1u,kui_armada_ce_maple_traffic_cache,&traffic_io,NULL)==
        KUI_ARMADA_CE_RAM_BIND_ARGUMENT);
    CHECK(leaf_calls==0u && !traffic_work.reads && !rx_work.reads);
}
int main(void) {
    matrix();invalid_inputs();
    printf("Native snapshot C wrapper: %u scenarios, %u checks; actual extracted production function\n",scenarios,checks);
    return 0;
}
