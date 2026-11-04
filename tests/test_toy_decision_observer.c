/* SPDX-License-Identifier: GPL-3.0-only */
#include "fmv_toy_profile.h"
#undef KUI_TOY_FMV_IDLE_REARM
#define KUI_TOY_FMV_IDLE_REARM 0
#define KUI_TOY_DECISION_PROFILE 2
#define KUI_TOY_DECISION_HOST_TEST 1
#include "../src/loader/toy_decision_observer.c"
#include <assert.h>
#include <stdio.h>
static uint32_t now,clock_reads,valid_clock=1u,saved_sr,current_vbr=0x8c010000u,priority=7u,updater;
static struct kui_toy_pilot_sci_stats engine;
uint32_t kui_toy_decision_host_read(uint32_t a,unsigned n) {
    (void)n;
    switch(a) {
    case 0xffc00000u:return valid_clock?0xe0au:0u;
    case 0xffd80008u:return UINT32_MAX;
    case 0xffd80010u:return 2u;
    case 0xffd80004u:return 1u;
    case 0xffd8000cu:++clock_reads;return ~now;
    case 0xffd00008u:return priority<<4;
    default:assert(0);return 0u;
    }
}
uint32_t kui_toy_decision_host_sr(void) {return saved_sr;}
uint32_t kui_toy_decision_host_vbr(void) {return current_vbr;}
uint32_t kui_toy_decision_host_updater(void) {return updater;}
uint32_t kui_toy_pilot_sci_decision_vbr(void) {return 0x8cfd0100u;}
const struct kui_toy_pilot_sci_stats *kui_toy_pilot_sci_snapshot(void) {return &engine;}
static void reset(void) {
    memset(&decision,0,sizeof(decision));memset(&visit,0,sizeof(visit));memset(&engine,0,sizeof(engine));
    decision_enabled=2u;now=clock_reads=saved_sr=updater=0u;
    valid_clock=1u;priority=7u;current_vbr=0x8c010000u;
}
static void scalar(uint32_t begin,uint32_t end) {
    struct kui_retail_gd s={0};now=begin;kui_toy_decision_gd_begin(&s,KUI_GD_DRIVE,0u);
    now=end;kui_toy_decision_gd_end(&s,KUI_GD_DRIVE,0u,0);
}
int main(void) {
    reset();scalar(100u,200u);
    assert(clock_reads==2u && decision.count==1u && decision.bins[0].gd_ticks==100u);
    assert(!(decision.bins[0].flags&KUI_TOY_DECISION_CROSS_BIN));
    scalar(KUI_TOY_DECISION_BIN_TICKS-10u,KUI_TOY_DECISION_BIN_TICKS+200u);
    assert(decision.count==2u && (decision.bins[1].flags&KUI_TOY_DECISION_CROSS_BIN));
    for(unsigned i=2u;i<12u;++i)scalar(i*KUI_TOY_DECISION_BIN_TICKS+100u,i*KUI_TOY_DECISION_BIN_TICKS+200u);
    assert(decision.count==8u);
    scalar(30u*KUI_TOY_DECISION_BIN_TICKS,30u*KUI_TOY_DECISION_BIN_TICKS+50u);
    assert(decision.count==1u && (decision.bins[decision.head].flags&KUI_TOY_DECISION_SKIPPED_BINS));
    now=50u*KUI_TOY_DECISION_BIN_TICKS;kui_toy_decision_freeze();
    assert(decision.freeze_valid && !decision.count && decision.last_sample_age>8u*KUI_TOY_DECISION_BIN_TICKS);
    struct kui_toy_decision_report frozen=decision;
    scalar(now+1u,now+2u);kui_toy_decision_freeze();assert(!memcmp(&frozen,&decision,sizeof(decision)));

    reset();scalar(UINT32_MAX-40u,20u);assert(decision.bins[0].gd_ticks==61u && decision.bins[0].gd_valid==1u);
    valid_clock=0u;scalar(100u,200u);assert(decision.invalid_clocks==2u);
    valid_clock=1u;scalar(300u,400u);assert(decision.count==1u && decision.bins[decision.head].epoch==2u);
    valid_clock=0u;kui_toy_decision_freeze();assert(!decision.freeze_valid && decision.last_sample_age==UINT32_MAX);

    reset();struct kui_retail_gd s={0};
    now=100u;kui_toy_decision_gd_begin(&s,KUI_GD_REQUEST,KUI_GD_DMAREAD);
    s.command=KUI_GD_DMAREAD;s.token=9u;s.lba=150u;s.count=8u;s.sector_bytes=2048u;s.pending=1u;
    now=150u;kui_toy_decision_gd_end(&s,KUI_GD_REQUEST,KUI_GD_DMAREAD,9);
    s.completed_bytes=2048u;engine.irq_blocks=4u;updater=3u;
    saved_sr=0x60u;current_vbr=kui_toy_pilot_sci_decision_vbr();
    now=200u;kui_toy_decision_gd_begin(&s,KUI_GD_EXEC,0u);
    struct kui_toy_decision_service work={1u,80u,80u,5u,1u,20u,50u,10u,0u};
    kui_toy_decision_service_commit(&work);engine.call_blocks=4u;s.completed_bytes=4096u;
    now=300u;kui_toy_decision_gd_end(&s,KUI_GD_EXEC,0u,0);
    struct kui_toy_decision_bin *b=&decision.bins[0];
    assert(b->bytes==4096u && b->irq_blocks==4u && b->call_blocks==4u && b->updater_entries==3u);
    assert(b->wait_ticks+b->credit_ticks+b->other_ticks==b->service_ticks);
    assert(b->accepted_data==1u && b->read_modes==2u && b->max_request==8u);
    assert(b->mask_low==2u && b->route_own==1u && b->route_other==1u);
    assert(!(b->priority&0x1000000u));
    s.completed_bytes=s.count*s.sector_bytes;s.pending=0u;s.status=KUI_GD_COMPLETED;
    now=400u;kui_toy_decision_gd_begin(&s,KUI_GD_CHECK,9u);
    s.command=0u;now=450u;kui_toy_decision_gd_end(&s,KUI_GD_CHECK,9u,KUI_GD_COMPLETED);
    assert(b->bytes==16384u && b->completed_data==1u);
    now=500u;kui_toy_decision_gd_begin(&s,KUI_GD_CHECK,9u);
    now=550u;kui_toy_decision_gd_end(&s,KUI_GD_CHECK,9u,KUI_GD_COMPLETED);
    assert(b->bytes==16384u && b->completed_data==1u);
    priority=2u;s.command=KUI_GD_DMAREAD;
    now=600u;kui_toy_decision_gd_begin(&s,KUI_GD_EXEC,0u);
    now=650u;kui_toy_decision_gd_end(&s,KUI_GD_EXEC,0u,0);
    assert((b->priority&0x1000000u) && ((b->priority>>8)&15u)==2u);

    reset();decision_enabled=1u;scalar(1u,2u);kui_toy_decision_freeze();
    assert(!clock_reads && !decision.count && !decision.enabled && decision.frozen);
    puts("PASS rolling Toy observer: bounded aging, quiet expiry, wrap, profile loss, partial bins, IRQ credit, modes and freeze");
}
