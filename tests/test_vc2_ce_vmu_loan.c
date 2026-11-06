/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute production loan C with authored state/cache operations. There is
 * no SH4 exception, actual cache, SD timing, or console-compatibility claim. */
#define KUI_VC2_VMU_LOAN_TEST 1
#define KUI_RETAIL_ASYNC_TEST 1
#include "../src/loader/vc2_ce_vmu_loan.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fixture {
    kui_retail_async_region_type region;
    _Alignas(32) struct kui_retail_manifest map;
};
static _Alignas(32) struct fixture low;
static struct kui_loader_sd copied;
static uint32_t test_sr;
static unsigned publications,invalidations,sr_writes,io_calls,checks;
static bool dirty_neighbor,dirty_old_lock;
static uint32_t neighbor_value;
#define CHECK(expression) do {++checks;assert(expression);} while(0)
#define ACTIVE ((volatile uint32_t *)(void *)((uint8_t *)&low.map+244u))
#define FAULT ((volatile uint32_t *)(void *)((uint8_t *)&low.map+240u))
#define NEIGHBOR ((volatile uint32_t *)(void *)((uint8_t *)&low.map+248u))
#define WORKER_SR UINT32_C(0x40008000)
#define MASKED_SR UINT32_C(0x700080f0)

uint32_t kui_vc2_vmu_loan_test_sr(void) {return test_sr;}
void kui_vc2_vmu_loan_test_set_sr(uint32_t value) {
    ++sr_writes;test_sr=value;
}
void kui_vc2_vmu_loan_test_publish(uintptr_t begin,uintptr_t end) {
    CHECK((test_sr&0xf0u)==0xf0u);
    CHECK(begin>=(uintptr_t)&low && end<=(uintptr_t)&low+sizeof(low) && begin<end);
    if(loan.magic==LOAN_MAGIC)CHECK(begin==loan.begin && end==loan.end);
    ++publications;
    /* Model a dirty native alias being published only at the cache barrier. */
    if(dirty_neighbor) {*NEIGHBOR=neighbor_value;dirty_neighbor=false;}
    if(dirty_old_lock) {*ACTIVE=0u;dirty_old_lock=false;}
}
void kui_vc2_vmu_loan_test_invalidate(uintptr_t at) {
    CHECK((test_sr&0xf0u)==0xf0u);
    CHECK(at>=(uintptr_t)&low && at<(uintptr_t)&low+sizeof(low));
    ++invalidations;
}
static void impossible_bus(void *ctx) {(void)ctx;++io_calls;assert(!"loan wire I/O");}
static void impossible_select(void *ctx,bool selected) {(void)selected;impossible_bus(ctx);}
static uint8_t impossible_transfer(void *ctx,uint8_t value,bool slow) {
    (void)value;(void)slow;impossible_bus(ctx);return 0u;
}
static uint32_t impossible_ticks(void *ctx) {impossible_bus(ctx);return 0u;}
static bool impossible_block(void *ctx,const uint8_t *tx,uint8_t *rx,
    size_t bytes,bool slow,uint16_t *crc) {
    (void)tx;(void)rx;(void)bytes;(void)slow;(void)crc;impossible_bus(ctx);return false;
}
static void fresh(void) {
    memset(&loan,0,sizeof(loan));memset(&low,0,sizeof(low));
    memset(&copied,0xa5,sizeof(copied));
    test_sr=MASKED_SR;publications=invalidations=sr_writes=io_calls=0u;
    dirty_neighbor=dirty_old_lock=false;neighbor_value=0u;
    struct kui_retail_gd *s=&low.region.shared.service;
    s->initialized=true;s->ops.context=(void *)(uintptr_t)1u;
    s->tracks=low.map.slots;s->token=42u;s->command=38u;
    s->completed_bytes=0x8800u;s->outputs[0]=0x800u;
    low.region.engine.manifest=&low.map;
    low.region.engine.token=42u;low.region.engine.destination=0x88123000u;
    low.region.engine.cursor.block=123u;
    low.region.shared.card.transport=KUI_STORAGE_SCI;
    struct kui_loader_sd *sd=&low.region.shared.card.device.sd;
    sd->blocks=1000000u;sd->ready=true;sd->high_capacity=true;
    sd->last_command=18u;sd->last_response=0u;
    sd->bus=(struct kui_loader_sd_bus){
        &low.region.shared.port,impossible_bus,impossible_bus,impossible_select,
        impossible_transfer,impossible_ticks,impossible_block};
    low.map.storage_transport=KUI_STORAGE_SCI;low.map.card_sectors=sd->blocks;
}
static int bind_low(void) {
    return kui_vc2_ce_vmu_loan_bind(&low.region,ACTIVE,FAULT,
        (uintptr_t)&low,(uintptr_t)&low+sizeof(low));
}
static void ready(void) {
    fresh();CHECK(bind_low()==0);CHECK(publications==1u && !invalidations);
    test_sr=WORKER_SR;publications=invalidations=sr_writes=0u;
}
static void success(void) {
    ready();kui_retail_async_region_type original=low.region;
    struct kui_retail_manifest map=low.map;
    CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==0);
    CHECK(test_sr==WORKER_SR && sr_writes==2u);
    CHECK(publications==1u && invalidations==1u && *ACTIVE==2u);
    CHECK(memcmp(&copied,&original.shared.card.device.sd,sizeof(copied))==0);
    CHECK(memcmp(&low.region,&original,sizeof(original))==0);
    copied.blocks=9u;copied.ready=false; /* No retained caller pointer. */
    CHECK(kui_vc2_ce_vmu_loan_end(true)==0);
    CHECK(test_sr==WORKER_SR && sr_writes==4u && *ACTIVE==0u);
    CHECK(publications==2u && invalidations==2u && !io_calls);
    CHECK(memcmp(&low.region,&original,sizeof(original))==0);
    CHECK(memcmp(&low.map,&map,sizeof(map))==0);
    uint32_t report[8]={0};kui_vc2_ce_vmu_loan_snapshot(report);
    CHECK(report[0]==LOAN_MAGIC && report[1]==LOAN_IDLE);
    CHECK(report[2]==1u && report[3]==1u && report[4]==0u);
    CHECK(report[5]==0u && report[6]==(uint32_t)(uintptr_t)&low.region && report[7]==0u);
    CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==0);
    CHECK(kui_vc2_ce_vmu_loan_end(true)==0);
    CHECK(loan.claims==2u && loan.releases==2u);
}
static void idle_control_window(void) {
    ready();
    /* The real loan can overlap a driver's ordinary DRIVE/control call. */
    struct kui_retail_gd *service=&low.region.shared.service;
    service->token=0u;service->command=0u;service->status=KUI_GD_NOT_FOUND;
    service->completed_bytes=0u;service->outputs[0]=0u;
    CHECK(!service->pending && !service->executing);
    CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==0);
    CHECK(*ACTIVE==2u && loan.state==LOAN_WRITER);
    CHECK(kui_vc2_ce_vmu_loan_end(true)==0 && *ACTIVE==0u);
}
static void busy_state(unsigned index) {
    ready();
    switch(index) {
    case 0:low.region.shared.service.initialized=false;break;
    case 1:low.region.shared.service.pending=true;break;
    case 2:low.region.shared.service.executing=true;break;
    case 3:low.region.shared.service.callback_due=true;break;
    case 4:low.region.engine.active=1u;break;
    case 5:low.region.engine.in_irq=1u;break;
    case 6:low.region.engine.opened=1u;break;
    case 7:low.region.engine.hooked=1u;break;
    case 8:low.region.engine.release.armed=1u;break;
    case 9:low.region.engine.release.depth=1u;break;
    case 10:low.region.shared.port.acquired=true;break;
    case 11:low.region.shared.stream.state=1u;break;
    case 12:low.region.shared.card.stream.active=true;break;
    case 13:*ACTIVE=1u;break; /* An ordinary GD entry owns the low reader. */
    case 14:*ACTIVE=2u;break; /* A writer token without this loan's ownership. */
    case 15:loan.state=LOAN_WRITER;break;
    default:assert(false);
    }
    struct fixture before=low;struct kui_loader_sd out=copied;
    CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==1);
    CHECK(test_sr==WORKER_SR && publications==1u && sr_writes==2u);
    CHECK(!invalidations && !io_calls && loan.busy==1u && !loan.claims);
    CHECK(memcmp(&low,&before,sizeof(low))==0);
    CHECK(memcmp(&copied,&out,sizeof(out))==0);
}
static void contexts(void) {
    static const uint32_t invalid[]={0x50008000u,0x40008010u,0x00008000u,0x700080f0u};
    for(unsigned i=0u;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        ready();test_sr=invalid[i];struct fixture before=low;
        CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==3);
        CHECK(test_sr==invalid[i] && !publications && !invalidations && !sr_writes);
        CHECK(memcmp(&before,&low,sizeof(low))==0 && !io_calls);
        test_sr=WORKER_SR;CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==0);
        unsigned p=publications,n=invalidations,w=sr_writes;
        test_sr=invalid[i];CHECK(kui_vc2_ce_vmu_loan_end(true)==3);
        CHECK(test_sr==invalid[i] && *ACTIVE==2u && loan.state==LOAN_WRITER);
        CHECK(publications==p && invalidations==n && sr_writes==w && !io_calls);
        test_sr=WORKER_SR;CHECK(kui_vc2_ce_vmu_loan_end(true)==0);
    }
}
static void binding(void) {
    fresh();test_sr=WORKER_SR;CHECK(bind_low()==-1);CHECK(!publications && !invalidations);
    fresh();CHECK(kui_vc2_ce_vmu_loan_bind(NULL,ACTIVE,FAULT,
        (uintptr_t)&low,(uintptr_t)&low+sizeof(low))==-1);CHECK(!publications);
    fresh();CHECK(kui_vc2_ce_vmu_loan_bind((uint8_t *)&low+1u,ACTIVE,FAULT,
        (uintptr_t)&low,(uintptr_t)&low+sizeof(low))==-1);CHECK(!publications);
    fresh();CHECK(kui_vc2_ce_vmu_loan_bind(&low.region,ACTIVE+1,FAULT,
        (uintptr_t)&low,(uintptr_t)&low+sizeof(low))==-1);CHECK(publications==1u);
    fresh();CHECK(kui_vc2_ce_vmu_loan_bind(&low.region,ACTIVE,FAULT,
        (uintptr_t)&low+32u,(uintptr_t)&low+sizeof(low))==-1);CHECK(!publications);
    fresh();CHECK(kui_vc2_ce_vmu_loan_bind(&low.region,ACTIVE,FAULT,
        (uintptr_t)&low,(uintptr_t)&low+sizeof(low)-32u)==-1);CHECK(publications==1u);
    fresh();low.region.engine.manifest=(const void *)((uint8_t *)&low.map+1u);
    CHECK(bind_low()==-1);CHECK(publications==1u && !invalidations);
    fresh();low.region.shared.service.tracks=NULL;CHECK(bind_low()==-1);
    fresh();low.region.shared.service.ops.context=NULL;CHECK(bind_low()==-1);
    fresh();CHECK(bind_low()==0);CHECK(bind_low()==-1);CHECK(loan.state==LOAN_IDLE);
    fresh();*ACTIVE=1u;CHECK(bind_low()==0); /* Called inside initializer's low entry. */
    CHECK(*ACTIVE==1u && !io_calls);
}
static void assert_quarantined(unsigned why,bool cleared) {
    CHECK(loan.state==LOAN_QUARANTINED && loan.first_fault==why);
    CHECK(low.region.shared.port.fault && !low.region.shared.card.device.sd.ready);
    CHECK(low.region.engine.failed==KUI_GD_ERROR_IO && *FAULT==1u);
    if(cleared)CHECK(*ACTIVE==0u);
    unsigned p=publications,n=invalidations,w=sr_writes;
    CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==2);
    CHECK(kui_vc2_ce_vmu_loan_end(true)==2);
    CHECK(publications==p && invalidations==n && sr_writes==w && !io_calls);
}
static void unhealthy(unsigned index) {
    ready();
    switch(index) {
    case 0:*FAULT=1u;break;
    case 1:low.region.shared.port.fault=true;break;
    case 2:low.region.engine.failed=1u;break;
    case 3:low.region.shared.card.transport=KUI_STORAGE_SCIF;break;
    case 4:low.map.storage_transport=KUI_STORAGE_SCIF;break;
    case 5:low.region.shared.card.device.sd.ready=false;break;
    case 6:low.region.shared.card.device.sd.slow=true;break;
    case 7:low.region.shared.card.device.sd.blocks=0u;break;
    case 8:low.region.shared.card.device.sd.blocks=UINT64_C(0x100000001);break;
    case 9:low.region.shared.card.device.sd.blocks++;break;
    case 10:low.region.engine.manifest=(const void *)(uintptr_t)1u;break;
    case 11:low.region.shared.service.tracks=NULL;break;
    case 12:low.region.shared.service.ops.context=NULL;break;
    default:assert(false);
    }
    uint32_t token=low.region.shared.service.token,completed=low.region.shared.service.completed_bytes;
    CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==2);
    CHECK(test_sr==WORKER_SR && !loan.claims);
    CHECK(low.region.shared.service.token==token && low.region.shared.service.completed_bytes==completed);
    assert_quarantined(FAULT_LOW_STATE,true);
}
static void restore_failures(void) {
    ready();CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==0);
    CHECK(kui_vc2_ce_vmu_loan_end(false)==2);assert_quarantined(FAULT_RESTORE,true);
    ready();CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==0);*ACTIVE=0u;
    CHECK(kui_vc2_ce_vmu_loan_end(true)==2);assert_quarantined(FAULT_LOCK,true);
    ready();CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==0);*ACTIVE=1u;
    CHECK(kui_vc2_ce_vmu_loan_end(true)==2);CHECK(*ACTIVE==1u);
    assert_quarantined(FAULT_LOCK,false); /* Never clear an ordinary entry's lock. */
    ready();CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==0);*ACTIVE=3u;
    CHECK(kui_vc2_ce_vmu_loan_end(true)==2);CHECK(*ACTIVE==3u);
    assert_quarantined(FAULT_LOCK,false);
    ready();CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==0);low.region.shared.stream.state=1u;
    CHECK(kui_vc2_ce_vmu_loan_end(true)==2);CHECK(low.region.shared.stream.state==1u);
    assert_quarantined(FAULT_LOCK,true);
    ready();struct fixture before=low;CHECK(kui_vc2_ce_vmu_loan_end(true)==2);
    CHECK(loan.state==LOAN_QUARANTINED && loan.first_fault==FAULT_LOCK);
    CHECK(memcmp(&low,&before,sizeof(low))==0 && !publications && !invalidations);
}
static void cache_order(void) {
    ready();CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==0);
    dirty_neighbor=true;neighbor_value=0xdeadbeefu;
    CHECK(kui_vc2_ce_vmu_loan_end(true)==0);
    CHECK(!dirty_neighbor && *NEIGHBOR==neighbor_value && *ACTIVE==0u);
    ready();CHECK(kui_vc2_ce_vmu_loan_begin(&copied)==0);
    dirty_old_lock=true; /* A stale alias may not clear lock without detection. */
    CHECK(kui_vc2_ce_vmu_loan_end(true)==2);assert_quarantined(FAULT_LOCK,true);
}
static void output_bounds(void) {
    ready();struct fixture before=low;
    CHECK(kui_vc2_ce_vmu_loan_begin(NULL)==2);
    CHECK(kui_vc2_ce_vmu_loan_begin((void *)((uint8_t *)&copied+1u))==2);
    CHECK(kui_vc2_ce_vmu_loan_begin((void *)&loan)==2);
    CHECK(kui_vc2_ce_vmu_loan_begin((void *)&low.region)==2);
    CHECK(kui_vc2_ce_vmu_loan_begin((void *)(UINTPTR_MAX-2u))==2);
    CHECK(!publications && !invalidations && !sr_writes && !io_calls);
    CHECK(memcmp(&before,&low,sizeof(low))==0);
    kui_vc2_ce_vmu_loan_snapshot(NULL);
    kui_vc2_ce_vmu_loan_snapshot((void *)((uint8_t *)&copied+1u));
    kui_vc2_ce_vmu_loan_snapshot((void *)&loan);
    CHECK(loan.state==LOAN_IDLE && loan.magic==LOAN_MAGIC);
}
int main(void) {
    success();idle_control_window();for(unsigned i=0u;i<16u;++i)busy_state(i);
    contexts();binding();for(unsigned i=0u;i<13u;++i)unhealthy(i);
    restore_failures();cache_order();output_bounds();
    printf("VC2_FOREGROUND_LOAN_JSON {\"checks\":%u,\"busy_states\":16,"
        "\"writer_lock_value\":2,\"ordinary_lock_value\":1,"
        "\"unhealthy_states\":13,\"wire_io\":0,\"hardware_executed\":false}\n",checks);
    return 0;
}
