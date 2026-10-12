/* SPDX-License-Identifier: GPL-3.0-only */
/* Production SCHED v2 around the real GD core. Engine counter increments
 * and cleanup are independently controlled; no console throughput is modeled. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "kui/toy_loader_schedule_report.h"
#include "toy_pilot_sci.h"
#ifndef KUI_SCHEDULE_REPORT_SOURCE
#define KUI_SCHEDULE_REPORT_SOURCE "../src/loader/toy_loader_schedule_report.c"
#endif
#include KUI_SCHEDULE_REPORT_SOURCE

#define BEGIN UINT32_C(0x8c100000)
#define PARAM (BEGIN+0x100u)
#define STATUS (BEGIN+0x200u)
#define OUTPUT (BEGIN+0x1000u)
static struct kui_retail_gd gd;
static struct kui_toy_pilot_sci_stats engine;
static struct kui_toy_pilot_sci_startup_stats startup;
static struct kui_toy_pilot_sci_budget_stats budget;
static uint8_t ram[64u*1024u];
static uint32_t ticks,readonly_scope,deny_status,capture,cleanup_calls,cleanup_ok;

uint32_t kui_toy_loader_schedule_report_host_read(uint32_t address,unsigned bytes) {
    switch(address) {
    case 0xffc00000u:assert(bytes==2u);return 0xe0au;
    case 0xffd80008u:assert(bytes==4u);return UINT32_MAX;
    case 0xffd80010u:assert(bytes==2u);return 2u;
    case 0xffd80004u:assert(bytes==1u);return 1u;
    case 0xffd8000cu:assert(bytes==4u);return ~ticks;
    default:assert(!"Unexpected SCHED MMIO read");return 0u;
    }
}
const struct kui_toy_pilot_sci_stats *kui_toy_pilot_sci_snapshot(void){return &engine;}
const struct kui_toy_pilot_sci_startup_stats *kui_toy_pilot_sci_startup_snapshot(void){return &startup;}
const struct kui_toy_pilot_sci_budget_stats *kui_toy_pilot_sci_budget_snapshot(void){return &budget;}
bool kui_toy_pilot_sci_quiesce(void) {
    ++cleanup_calls;memset(&engine,0xde,sizeof(engine));memset(&startup,0xad,sizeof(startup));
    memset(&budget,0xcc,sizeof(budget));return cleanup_ok!=0u;
}
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int access) {
    (void)context;assert(access>=0 && access<=KUI_RETAIL_MAP_VALIDATE);
    if(readonly_scope)assert(access==0);
    if((address==STATUS && deny_status) || address<BEGIN || address-BEGIN>sizeof(ram) ||
       bytes>sizeof(ram)-(address-BEGIN))return NULL;
    return access==KUI_RETAIL_MAP_VALIDATE?ram+sizeof(ram):ram+address-BEGIN;
}
static int check(void *context,uint32_t lba,uint32_t sectors,uint32_t bytes) {
    (void)context;return bytes==2048u && lba>=45000u && lba<=45100u && sectors<=45100u-lba?0:-1;
}
static int read_data(void *context,uint32_t lba,uint32_t sectors,uint32_t bytes,void *out) {
    (void)context;assert(!check(NULL,lba,sectors,bytes));ticks+=sectors*100u;
    for(uint32_t n=0u;n<sectors;++n)for(uint32_t i=0u;i<bytes;++i)((uint8_t *)out)[n*bytes+i]=(uint8_t)i;
    return 0;
}
static void put(uint32_t address,uint32_t value){memcpy(ram+address-BEGIN,&value,4u);}
static void fresh(void) {
    static const union kui_retail_slot tracks[]={
        {.track={.start_lba=0u,.end_lba=16u,.control=4u|KUI_RETAIL_TRACK_COOKED}},
        {.track={.start_lba=16u,.end_lba=32u,.control=0u}},
        {.track={.start_lba=45000u,.end_lba=45100u,.control=4u}}
    };
    const struct kui_gd_ops ops={NULL,map,check,read_data};
    assert(!kui_retail_gd_init(&gd,tracks,3u,&ops,BEGIN,BEGIN+sizeof(ram)));
    gd.step=2u;memset(ram,0xa5,sizeof(ram));memset(&engine,0,sizeof(engine));
    startup=(struct kui_toy_pilot_sci_startup_stats){1u,0u,7u,0u};
    budget=(struct kui_toy_pilot_sci_budget_stats){KUI_TOY_PILOT_DATA_BUDGET_PROFILE,
        KUI_TOY_SCI_SERVICE_BLOCKS,KUI_TOY_SCI_SERVICE_TICKS,0u,0u,0u,0u,0u,0u,0u,0u,0u};
    memset(schedule_report,0,sizeof(schedule_report));memset(&schedule_visit,0,sizeof(schedule_visit));
    ticks=readonly_scope=deny_status=cleanup_calls=0u;capture=cleanup_ok=1u;
}
static int32_t call(uint32_t function,uint32_t a,uint32_t b,uint32_t irq,uint32_t retries) {
    if(capture)kui_toy_loader_schedule_report_begin(&gd);
    ticks+=3u;int32_t result=kui_retail_gd_dispatch(&gd,a,b,0u,function);
    /* This models counter progress during the real dispatch, including an
     * initial pump before an accepted REQUEST returns to report_end(). */
    engine.irq_blocks+=irq;engine.retries+=retries;
    if(capture){readonly_scope=1u;kui_toy_loader_schedule_report_end(&gd,function,a,result);readonly_scope=0u;}
    return result;
}
static uint32_t request(uint32_t sectors,uint32_t offset,uint32_t irq,uint32_t retries) {
    put(PARAM,45150u+offset);put(PARAM+4u,sectors);put(PARAM+8u,OUTPUT);put(PARAM+12u,0u);
    int32_t token=call(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM,irq,retries);assert(token>0);return (uint32_t)token;
}
static uint32_t *active(void){return schedule_report+48u+schedule_report[8]*18u;}
static void counter_lifetime(void) {
    fresh();engine.irq_blocks=100u;engine.retries=20u;
    uint32_t token=request(3u,0u,2u,1u);uint32_t *r=active();
    assert(r[16]==2u && r[17]==1u); /* Includes initial pump. */
    assert(call(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM,0u,0u)==0 && schedule_report[6]==1u);
    assert(r[16]==2u && r[17]==1u); /* Refusal cannot rebase an existing token. */
    assert(!call(KUI_GD_EXEC,0u,0u,4u,2u));assert(r[16]==6u && r[17]==3u);
    assert(call(KUI_GD_CHECK,token+1u,STATUS,1u,0u)==KUI_GD_FAILED && gd.pending);
    assert(r[16]==7u && r[17]==3u);
    assert(!call(KUI_GD_EXEC,0u,0u,3u,1u));assert(!gd.pending && r[16]==10u && r[17]==4u);
    deny_status=1u;assert(call(KUI_GD_CHECK,token,STATUS,1u,0u)==KUI_GD_FAILED && gd.command);deny_status=0u;
    assert(r[16]==11u && r[17]==4u);
    assert(call(KUI_GD_CHECK,token,STATUS,2u,1u)==KUI_GD_COMPLETED);
    assert((r[7]&8u) && r[16]==13u && r[17]==5u && schedule_report[8]==UINT32_MAX);
    uint32_t consumed[18];memcpy(consumed,r,sizeof(consumed));
    engine.irq_blocks+=100u;engine.retries+=50u;
    assert(!call(KUI_GD_RESET,0u,0u,7u,4u));assert(!memcmp(consumed,r,sizeof(consumed)));
    kui_toy_loader_schedule_report_freeze();assert(!memcmp(consumed,r,sizeof(consumed)));
}
static void wrap_and_reset(void) {
    fresh();engine.irq_blocks=UINT32_MAX-2u;engine.retries=UINT32_MAX-1u;
    uint32_t token=request(3u,0u,5u,3u);uint32_t *r=active();
    assert(r[16]==5u && r[17]==3u && !schedule_report[10]);
    assert(call(KUI_GD_CHECK,token,STATUS,0u,0u)==KUI_GD_PROCESSING);
    assert(r[16]==5u && r[17]==3u);
    assert(!call(KUI_GD_EXEC,0u,0u,4u,1u));uint32_t credit=r[8];
    assert(!call(KUI_GD_RESET,0u,0u,2u,2u));
    assert((r[7]&256u) && r[16]==11u && r[17]==6u && r[8]==credit);
    uint32_t reset_record[18];memcpy(reset_record,r,sizeof(reset_record));
    assert(!call(KUI_GD_INIT,0u,0u,12u,5u));assert(!memcmp(reset_record,r,sizeof(reset_record)));
    fresh();(void)request(1u,0u,UINT32_MAX,0u);r=active();
    assert(r[16]==UINT32_MAX && !r[17] && schedule_report[10]);
}
static void freeze_pending_and_budget(void) {
    for(uint32_t ok=0u;ok<2u;++ok) {
        fresh();engine.irq_blocks=77u;engine.retries=13u;cleanup_ok=ok;
        (void)request(3u,0u,2u,1u);uint32_t *r=active();assert(r[16]==2u && r[17]==1u);
        /* Background IRQ verification continues between serialized calls. */
        engine.irq_blocks+=9u;engine.retries+=2u;
        budget.service_calls=7u;budget.block_exits=1u;budget.time_exits=2u;
        budget.token_exits=1u;budget.step_exits=1u;budget.finished_or_inactive_exits=2u;
        budget.service_verified_blocks=11u;budget.zero_credit_calls=3u;budget.credited_bytes=2048u;
        struct kui_toy_pilot_sci_stats expected_engine=engine;
        struct kui_toy_pilot_sci_startup_stats expected_startup=startup;
        struct kui_toy_pilot_sci_budget_stats expected_budget=budget;
        kui_toy_loader_schedule_report_freeze();
        assert(r[16]==11u && r[17]==3u); /* Must refresh before cleanup. */
        assert(!memcmp(schedule_report+16u,&expected_engine,64u));
        assert(!memcmp(schedule_report+32u,&expected_startup,16u));
        assert(!memcmp(schedule_report+36u,&expected_budget,48u));
        assert(cleanup_calls==1u && schedule_report[14]==ok && schedule_report[15]==!ok);
        uint32_t frozen[192];memcpy(frozen,schedule_report,sizeof(frozen));
        assert(!call(KUI_GD_RESET,0u,0u,42u,4u));kui_toy_loader_schedule_report_freeze();
        assert(cleanup_calls==1u && !memcmp(frozen,schedule_report,sizeof(frozen)));
    }
    fresh();budget.service_calls=UINT32_MAX;kui_toy_loader_schedule_report_freeze();assert(schedule_report[10]);
    fresh();budget.credited_bytes=UINT32_MAX;kui_toy_loader_schedule_report_freeze();assert(schedule_report[10]);
}
static void layout_and_eviction(void) {
    fresh();assert(KUI_TOY_SCHEDULE_REPORT_WORDS==192u && KUI_TOY_SCHEDULE_RECORD_WORDS==18u);
    assert(sizeof(schedule_report)==768u);
    put(PARAM,45150u);put(PARAM+4u,101u);put(PARAM+8u,OUTPUT);put(PARAM+12u,0u);
    assert(call(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM,7u,2u)==0 && !schedule_report[6]);
    for(uint32_t ordinal=1u;ordinal<=9u;++ordinal) {
        uint32_t token=request(1u,ordinal-1u,ordinal,1u);uint32_t *r=active();
        assert(r[0]==ordinal && r[16]==ordinal && r[17]==1u);
        assert(!call(KUI_GD_EXEC,0u,0u,1u,1u));
        assert(call(KUI_GD_CHECK,token,STATUS,1u,0u)==KUI_GD_COMPLETED);
        assert(r[16]==ordinal+2u && r[17]==2u && r[11]==UINT32_C(0x9f5edd58));
    }
    assert(schedule_report[0]==UINT32_C(0x4c534331) && schedule_report[1]==2u && schedule_report[2]==192u);
    assert(schedule_report[6]==9u && schedule_report[7]==1u);
    for(uint32_t i=0u;i<8u;++i) {
        uint32_t ordinal=i+2u,slot=(schedule_report[7]+i)%8u;
        const uint32_t *r=schedule_report+48u+slot*18u;
        assert(r[0]==ordinal && r[2]==45000u+ordinal-1u && r[16]==ordinal+2u && r[17]==2u);
    }
}
static uint64_t protocol(int instrumented) {
    fresh();capture=(uint32_t)instrumented;uint32_t token=request(5u,0u,2u,1u);
    while(gd.pending)assert(!call(KUI_GD_EXEC,0u,0u,4u,1u));
    assert(call(KUI_GD_CHECK,token,STATUS,1u,0u)==KUI_GD_COMPLETED);
    uint64_t digest=UINT64_C(14695981039346656037);
    for(size_t i=0u;i<sizeof(ram);++i){digest^=ram[i];digest*=UINT64_C(1099511628211);}
    return digest;
}
int main(void) {
    uint64_t base=protocol(0),observed=protocol(1);assert(base==observed);
    counter_lifetime();wrap_and_reset();freeze_pending_and_budget();layout_and_eviction();
    printf("BUDGET SCHED protocol/payload digest=%016llx\n",(unsigned long long)base);
    puts("SCHED v2 production: pre-pump IRQ/retry baselines, refused handles, wrap, consumed-CHECK immutability, reset, pending IRQ freeze, exact budget snapshot, saturation and rolling192-word geometry PASS");
    return 0;
}
