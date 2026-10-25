/* SPDX-License-Identifier: GPL-3.0-only */
/* The actual worker wrapper, clocks and typed region wiring, alongside the
 * original save/lease/protocol suite. Hardware observations are read-only. */
#define KUI_VMU_NATIVE_BACKGROUND 1
#define KUI_RETAIL_ASYNC_TEST 1
#define KUI_NATIVE_READER_DIAGNOSTIC 1
#define KUI_NATIVE_COOP_PUMP 1
#define KUI_NATIVE_REQUEST_TIMING 1
#define KUI_NATIVE_IRQ_REARM 1
#define KUI_NATIVE_LOADER_TIMELINE 1
#define main persistent_suite_main
#include "test_vmu_native_persistent.c"
#undef main

static kui_retail_async_region_type observed_region;
static struct {
    uint32_t vbr,iprb,chcr,tcr,dmaor,ssr;
} timeline_hw={0x8c00f400u,0xa510u,0x4911u,0x1ffu,0x8201u,0x84u};
static unsigned timeline_mmio_reads,timeline_vbr_reads;

void *kui_vmu_native_reader_test_region(uint32_t address) {
    CHECK(kui_vmu_native_background_region_address(config.lease.payload,address));
    return &observed_region;
}
uint32_t kui_native_loader_timeline_test_vbr(void) {
    ++timeline_vbr_reads;return timeline_hw.vbr;
}
uint32_t kui_native_loader_timeline_test_read(uint32_t address,unsigned width) {
    CHECK(address!=UINT32_C(0xffe00014)); /* RDR would consume a data byte. */
    ++timeline_mmio_reads;
    switch(address) {
    case UINT32_C(0xffd00008):CHECK(width==2u);return timeline_hw.iprb;
    case UINT32_C(0xffa0001c):CHECK(width==4u);return timeline_hw.chcr;
    case UINT32_C(0xffa00018):CHECK(width==4u);return timeline_hw.tcr;
    case UINT32_C(0xffa00040):CHECK(width==4u);return timeline_hw.dmaor;
    case UINT32_C(0xffe00010):CHECK(width==1u);return timeline_hw.ssr;
    default:CHECK(false);return 0u;
    }
}
static void visit(uint32_t exit,uint32_t function,uint32_t original_sr) {
    uint32_t before[6];memcpy(before,&timeline_hw,sizeof(before));
    uint32_t inherited_sr=live_sr;
    kui_native_loader_timeline_visit(exit,function,original_sr,&observed_region);
    CHECK(!memcmp(before,&timeline_hw,sizeof(before)) && live_sr==inherited_sr);
}
static void check_inactive_and_region_guards(void) {
    diagnostic_setup();native_loader_timeline_start();
    unsigned clocks=clock_reads,reads=timeline_mmio_reads;
    visit(0u,KUI_GD_CHECK,0x40000060u);
    CHECK(!native_loader_timeline.events && timeline_mmio_reads==reads && clock_reads==clocks);
    diagnostic.active=1u;config.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL;
    visit(0u,KUI_GD_CHECK,0x40000060u);
    CHECK(!native_loader_timeline.events && timeline_mmio_reads==reads && clock_reads==clocks);
    config.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE;
    kui_native_loader_timeline_visit(0u,KUI_GD_CHECK,0u,NULL);
    kui_native_loader_timeline_visit(0u,KUI_GD_CHECK,0u,(uint8_t *)&observed_region+4u);
    CHECK(!native_loader_timeline.events && timeline_mmio_reads==reads && clock_reads==clocks);
}
static void check_gesture_and_actual_wrapper(void) {
    diagnostic_setup();memset(&observed_region,0,sizeof(observed_region));
    for(unsigned i=0u;i<30u;i++)diagnostic_pair(true);
    CHECK(diagnostic.active && !native_loader_timeline.events);
    unsigned leases=lease_calls,bytes=sd.bytes,clocks=clock_reads,reads=timeline_mmio_reads;
    struct kui_retail_gd *g=&observed_region.shared.service;
    test_clock.counter=2000000u;
    visit(0u,KUI_GD_REQUEST,0x40000060u);
    const struct kui_native_loader_event *e=&native_loader_timeline.entered;
    CHECK(native_loader_timeline.have_enter && e->tick==2000000u);
    CHECK(e->flags==(KUI_NATIVE_TIMELINE_ENTER|KUI_NATIVE_TIMELINE_CLOCK_VALID));
    CHECK(e->function==KUI_GD_REQUEST && e->caller_sr==0x40000060u);
    CHECK(e->vbr==timeline_hw.vbr && e->iprb==timeline_hw.iprb && e->chcr==timeline_hw.chcr);
    CHECK(e->tcr==timeline_hw.tcr && e->dmaor==timeline_hw.dmaor && e->ssr==timeline_hw.ssr);

    g->command=KUI_GD_DMAREAD;g->token=7u;g->lba=45000u;g->count=73u;
    g->pending=1u;g->status=KUI_GD_PROCESSING;g->diag.last_result=7;
    observed_region.engine.active=1u;observed_region.engine.hooked=1u;
    observed_region.engine.release.armed=1u;observed_region.shared.stream.state=2u;
    timeline_hw.vbr=(uint32_t)(uintptr_t)&observed_region-UINT32_C(0x100);
    test_clock.counter=1999000u;
    visit(1u,KUI_GD_REQUEST,0x40000060u);
    CHECK(native_loader_timeline.events==2u && native_loader_timeline.longest_count==1u);
    CHECK(native_loader_timeline.have_request && native_loader_timeline.active.token==7u);
    CHECK(native_loader_timeline.active.lba==45000u && native_loader_timeline.active.count==73u);
    CHECK(native_loader_timeline.active.inside_ticks==1000u && native_loader_timeline.active.calls==1u);
    const struct kui_native_loader_call *c=&native_loader_timeline.longest[0];
    CHECK(c->elapsed==1000u && c->end.result==7u && c->end.caller_sr==0x40000060u);
    CHECK(c->end.state==(2u|0xf00u|(KUI_GD_DMAREAD<<16)));
    CHECK(c->end.flags==(KUI_NATIVE_TIMELINE_EXIT|KUI_NATIVE_TIMELINE_CLOCK_VALID|
        KUI_NATIVE_TIMELINE_READ|KUI_NATIVE_TIMELINE_PENDING));
    CHECK(timeline_mmio_reads-reads==10u && clock_reads-clocks==2u);

    test_clock.counter=1990000u;
    visit(0u,KUI_GD_EXEC,0x40000060u);
    test_clock.counter=1985000u;
    g->pending=0u;g->status=KUI_GD_COMPLETED;g->diag.last_result=0;
    g->completed_bytes=73u*2048u;observed_region.engine.active=0u;
    observed_region.shared.stream.state=1u;
    visit(1u,KUI_GD_EXEC,0x40000060u);
    CHECK(!native_loader_timeline.have_request && native_loader_timeline.request_count==1u);
    const struct kui_native_loader_request *request=&native_loader_timeline.requests[0];
    CHECK(request->token==7u && request->progress==73u*2048u && request->result==0u);
    CHECK(request->total_ticks==15000u && request->inside_ticks==6000u && request->outside_ticks==9000u);
    CHECK(request->calls==2u && !request->flags && request->chain_length==1u);
    CHECK(native_loader_timeline.max_call_ticks==5000u && native_loader_timeline.max_gap_ticks==9000u);
    CHECK(native_loader_timeline.longest[0].end.result==0u &&
          (native_loader_timeline.longest[0].end.flags&KUI_NATIVE_TIMELINE_COMPLETE));

    state.complete_sr=0x400000f0u;test_clock.counter=1984000u;
    native_loader_timeline_heartbeat(&test_clock);
    CHECK(native_loader_timeline.events==5u && native_loader_timeline.bins[0].maples==1u);
    CHECK(native_loader_event.function==UINT32_MAX && native_loader_event.caller_sr==state.complete_sr);
    CHECK((native_loader_event.flags&KUI_NATIVE_TIMELINE_KIND_MASK)==KUI_NATIVE_TIMELINE_MAPLE);
    CHECK(native_loader_timeline.request_count==1u);
    CHECK(lease_calls==leases && sd.bytes==bytes && !durable_card.inflight && !durable_card.handling);

    test_clock.frqcr=0u;test_clock.counter=1980000u;
    visit(0u,KUI_GD_CHECK,0x40000060u);
    CHECK(native_loader_timeline.invalid==1u && native_loader_timeline.clock_breaks==1u);
    CHECK((native_loader_event.flags&KUI_NATIVE_TIMELINE_CLOCK_BREAK) &&
          !(native_loader_event.flags&KUI_NATIVE_TIMELINE_CLOCK_VALID));
    CHECK(!native_loader_timeline.have_enter);
    test_clock.frqcr=0xe0au;test_clock.counter=1970000u;
    visit(0u,KUI_GD_CHECK,0x40000060u);
    test_clock.counter=1969000u;visit(1u,KUI_GD_CHECK,0x40000060u);
    CHECK(native_loader_timeline.epoch==2u && native_loader_timeline.invalid==1u);
    CHECK(lease_calls==leases && sd.bytes==bytes && test_clock.frqcr==0xe0au &&
          test_clock.tcor==UINT32_MAX && test_clock.tcr==2u && test_clock.tstr==1u);
}
int main(void) {
    persistent_suite_main();
    check_inactive_and_region_guards();check_gesture_and_actual_wrapper();
    printf("Timeline worker: %u checks; gesture window, exact MMIO widths, original SR, actual GD result, request split, heartbeat and clock epochs passed\n",checks);
    return 0;
}
