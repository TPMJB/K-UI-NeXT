/* SPDX-License-Identifier: GPL-3.0-only */
#define KUI_VMU_NATIVE_BACKGROUND 1
#define KUI_RETAIL_ASYNC_TEST 1
#define KUI_NATIVE_READER_DIAGNOSTIC 1
#define KUI_NATIVE_COOP_PUMP 1
#define KUI_NATIVE_REQUEST_TIMING 1
#define main persistent_suite_main
#include "test_vmu_native_persistent.c"
#undef main
static kui_retail_async_region_type observed_region;
static unsigned pump_visits;
static int pump_result;
static uint32_t test_vbr=UINT32_C(0x8c00f400);
static uint16_t test_iprb=0xa500u;
static unsigned restorations;
#if KUI_NATIVE_TIMED_PUMP
static unsigned pump_ready_after,window_clock_step=10u;
static bool window_changes_clock;
void kui_native_timed_test_clock(struct kui_vmu_native_profile_sample *s) {
    *s=test_clock;test_clock.counter-=window_clock_step;
}
#endif
uint32_t kui_native_cooperative_test_vbr(void) {return test_vbr;}
uint16_t kui_native_cooperative_test_iprb(void) {return test_iprb;}
void kui_native_cooperative_test_restore(uint32_t vbr,uint16_t iprb) {
    CHECK(vbr==UINT32_C(0x8c00f400) && iprb==0xa500u);
    test_vbr=vbr;test_iprb=(uint16_t)((test_iprb&~0xf0u)|(iprb&0xf0u));++restorations;
}
void *kui_vmu_native_reader_test_region(uint32_t address) {
    CHECK(kui_vmu_native_background_region_address(config.lease.payload,address));return &observed_region;
}
int kui_native_cooperative_test_visit(void) {
    CHECK(state.armed && !state.pending && state.submitted==state.completed);
    CHECK(!durable_card.inflight && !durable_card.handling);
    CHECK((live_sr&0xf0u)==0xf0u);
    ++pump_visits;
    test_vbr=UINT32_C(0x8c6ff000);test_iprb=0xa510u;
#if KUI_NATIVE_TIMED_PUMP
    if(window_changes_clock)test_clock.tstr=0u;
    if(pump_visits>=pump_ready_after)
#endif
    if(!pump_result)observed_region.engine.stats.irq_blocks+=2u;
    return pump_result;
}
int main(void) {
    persistent_suite_main();
    diagnostic_setup();diagnostic_pair(false);
    CHECK(pump_visits==0u); /* Initial full proof precedes the fast path. */
    observed_region.engine.active=1u;
    unsigned leases=lease_calls,bytes=sd.bytes;
#if KUI_NATIVE_PACED_CHECK
    diagnostic_pair(false);reader_cooperative_service();
    CHECK(!pump_visits && !restorations && !reader_cooperative.blocks);
    CHECK(lease_calls==leases && sd.bytes==bytes);
#else
    diagnostic_pair(false);
    CHECK(pump_visits==1u && reader_cooperative.blocks==2u);
    CHECK(restorations==1u && test_vbr==UINT32_C(0x8c00f400) && test_iprb==0xa500u);
    CHECK(lease_calls==leases && sd.bytes==bytes);
    pump_result=KUI_VMU_SD_LEASE_BUSY;diagnostic_pair(false);
    CHECK(pump_visits==2u && reader_cooperative.busy==1u && reader_cooperative.blocks==2u);
    pump_result=0;
    unsigned before=pump_visits;
    durable_card.inflight=1u;reader_cooperative_service();CHECK(pump_visits==before);durable_card.inflight=0u;
    durable_card.handling=1u;reader_cooperative_service();CHECK(pump_visits==before);durable_card.handling=0u;
    for(unsigned mode=2u;mode<=4u;++mode){config.diagnostics=mode;reader_cooperative_service();CHECK(pump_visits==before);}
    config.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE;
#endif
    reader_capture_start();
    diagnostic.active=1u;
    observed_region.engine.stats.hooks=test_clock.counter+300000u;
    observed_region.engine.stats.releases=250000u;
    observed_region.engine.native_max_lba=45678u;observed_region.engine.native_max_count=64u;
    observed_region.shared.service.pending=1u;observed_region.shared.service.token=99u;observed_region.engine.token=99u;
    diagnostic_pair(false);
    CHECK(reader_capture.samples==1u && !observed_region.engine.stats.releases);
    CHECK(reader_capture.call_max==250000u);
    observed_region.engine.stats.releases=500000u;
    observed_region.engine.native_max_lba=56789u;observed_region.engine.native_max_count=128u;
    diagnostic_pair(false);
    CHECK(reader_capture.call_max==500000u && reader_cooperative.max_lba==56789u && reader_cooperative.max_count==128u);
    CHECK(reader_capture.previous.irq_blocks==observed_region.engine.stats.irq_blocks-reader_cooperative.blocks);
#if KUI_NATIVE_TIMED_PUMP
    /* A receiver that is still pending on the first two opportunities
     * must deliver in the same bounded window without another game call. */
    diagnostic_setup();diagnostic_pair(false);observed_region.engine.active=1u;
    pump_result=0;unsigned old=pump_visits,restored=restorations;
    window_clock_step=20u;pump_ready_after=old+3u;
    reader_capture_start();reader_cooperative_service();
    CHECK(pump_visits==old+3u && restorations==restored+1u);
    CHECK(reader_cooperative.calls==3u && reader_cooperative.blocks==2u && !reader_cooperative.expired);
    CHECK(reader_cooperative.opportunities==1u && !reader_cooperative.clock_bad);
    /* A never-ready receiver ends at the read-only deadline. */
    old=pump_visits;pump_ready_after=UINT32_MAX;window_clock_step=300u;
    reader_capture_start();reader_cooperative_service();
    CHECK(pump_visits==old+2u && reader_cooperative.expired==1u);
    CHECK(reader_cooperative.max_ticks==600u && !reader_cooperative.blocks);
    /* An unsupported clock gets one ordinary nonblocking visit. */
    old=pump_visits;test_clock.tstr=0u;
    reader_capture_start();reader_cooperative_service();
    CHECK(pump_visits==old+1u && reader_cooperative.clock_bad==1u && !reader_cooperative.expired);
    /* An unchanged/frozen counter cannot make this an infinite loop. */
    test_clock.tstr=1u;window_clock_step=0u;old=pump_visits;
    reader_capture_start();reader_cooperative_service();
    CHECK(pump_visits==old+READER_WINDOW_VISITS && reader_cooperative.expired==1u);
    /* Down-counter wrap remains admitted; a profile change aborts. */
    test_clock.counter=5u;window_clock_step=20u;old=pump_visits;pump_ready_after=old+3u;
    reader_capture_start();reader_cooperative_service();
    CHECK(pump_visits==old+3u && reader_cooperative.blocks==2u && !reader_cooperative.clock_bad);
    old=pump_visits;pump_ready_after=UINT32_MAX;window_changes_clock=true;
    reader_capture_start();reader_cooperative_service();
    CHECK(pump_visits==old+1u && reader_cooperative.clock_bad==1u);
    puts("timed reader: pending-then-ready, deadline, invalid/changed/frozen clocks, wrap and two-block window bound passed");
#endif
#if KUI_NATIVE_PACED_CHECK
    puts("paced worker: original save/ownership suite, controller pump disabled, no storage lease and complete-request timing passed");
#else
    puts("cooperative worker: original save/ownership suite, post-completion pump, busy/storage exclusions and request timing passed");
#endif
    return 0;
}
