/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual General split worker, protocol and durable storage over the existing
 * independent RAM/card fixture. No retail program bytes are embedded. */
#define KUI_NATIVE_PERF_CAPTURE 1
#define KUI_SPLIT_CONTROLLER_TEST_MAIN inherited_split_checks
#include "test_vmu_native_split_controller.c"
#if KUI_NATIVE_READER_DIAGNOSTIC
static kui_retail_async_region_type perf_observed_region;
void *kui_vmu_native_reader_test_region(uint32_t address) {
    CHECK(kui_vmu_native_background_region_address(config.lease.payload,address));
    return &perf_observed_region;
}
#endif
static const struct kui_vmu_native_profile_metric *metric(unsigned kind) {
    return &perf_capture.timing[kind/4u].metric[kind%4u];
}
static bool expect_report;
static void perf_pair(bool gesture) {
    split_batch(1u,0u,0u);
    kui_vmu_native_passive_split_submit(&frame);
    split_done(1u,0u,0x01000000u);
    /* All four main units are actual controllers for the full packet proof;
     * the split fixture also tests other main-unit functions independently. */
    for(unsigned port=1u;port<4u;port++)put(rx_at(port*6u)+4u,4u,0x01000000u);
    put(rx_at(0u)+8u,4u,gesture?UINT32_C(0xfffffffd):UINT32_MAX);
    if(expect_report)kui_vmu_native_passive_complete(&frame);
    else complete_return();
    idle();
}
static void start_capture(void) {
#if KUI_NATIVE_READER_DIAGNOSTIC
    memset(&perf_observed_region,0,sizeof(perf_observed_region));
    perf_observed_region.engine.manifest=(const void *)(uintptr_t)
        (((config.lease.payload&UINT32_C(0x1fffffff))|UINT32_C(0x80000000))+
         KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES);
    perf_observed_region.engine.stats.call_blocks=100u;
#endif
    for(unsigned i=0u;i<KUI_NATIVE_PERF_CAPTURE_HOLD-1u;i++)perf_pair(true);
    CHECK(!perf_capture.active && !clock_reads);
    perf_pair(false);CHECK(!perf_capture.hold && !clock_reads);
    for(unsigned i=0u;i<KUI_NATIVE_PERF_CAPTURE_HOLD;i++)perf_pair(true);
    CHECK(perf_capture.active && !perf_capture.polls && clock_reads==2u);
    CHECK(metric(NATIVE_PERF_EMPTY)->valid==1u && metric(NATIVE_PERF_EMPTY)->ticks_total==10u);
#if KUI_NATIVE_READER_DIAGNOSTIC
    CHECK(perf_capture.reader_begin[0]==1u && perf_capture.reader_begin[2]==100u);
#endif
}
static void controller_and_clock(void) {
    split_setup();start_capture();
    perf_pair(false);
    CHECK(perf_capture.polls==1u && !perf_capture.flags);
    CHECK(metric(KUI_NATIVE_PERF_FULL_SUBMIT?NATIVE_PERF_FULL_SUBMIT:NATIVE_PERF_SPLIT_SUBMIT)->valid==1u);
    CHECK(metric(NATIVE_PERF_FAST_COMPLETE)->valid==1u);
    CHECK(!metric(NATIVE_PERF_FULL_COMPLETE)->calls && !metric(NATIVE_PERF_READ_LEASE)->calls);
    CHECK(!lease_calls && !sd.bytes && !sd.writes);
    CHECK(!state.pending && state.submitted==state.completed);
    test_clock.counter=5u;perf_capture.last_tick=15u;
    perf_pair(false);CHECK(!perf_capture.flags); /* Natural uint32 downcounter wrap. */
    test_clock.frqcr=0u;perf_pair(false);
    CHECK(perf_capture.clock_bad && (perf_capture.flags&1u));
    CHECK(metric(NATIVE_PERF_FAST_COMPLETE)->invalid==1u);
    test_clock.frqcr=0xe0au;perf_pair(false);
    CHECK(!state.pending && !durable_card.faulted);
    perf_capture.polls=KUI_NATIVE_PERF_CAPTURE_POLLS-1u;
#if KUI_NATIVE_READER_DIAGNOSTIC
    perf_observed_region.engine.stats.call_blocks+=7u;
    perf_observed_region.engine.stats.irq_blocks+=3u;
    perf_observed_region.shared.stream.stats.blocks+=5u;
    perf_observed_region.shared.stream.stats.polled+=2u;
#endif
    expect_report=true;
    if(setjmp(terminal)==0) {perf_pair(false);CHECK(false);}
    expect_report=false;
    CHECK(terminal_phase==19u && !terminal_error && !state.pending);
    CHECK(!durable_card.handling && !durable_card.inflight && !overlay.pending);
#if KUI_NATIVE_READER_DIAGNOSTIC
    CHECK(perf_capture.reader_end[0]==1u && perf_capture.reader_end[2]-perf_capture.reader_begin[2]==7u);
    CHECK(perf_capture.reader_end[1]-perf_capture.reader_begin[1]==3u);
    CHECK(perf_capture.reader_end[3]-perf_capture.reader_begin[3]==5u &&
          perf_capture.reader_end[4]-perf_capture.reader_begin[4]==2u);
#endif
}
static void storage_and_deferred_terminal(void) {
    split_setup();start_capture();perf_pair(false);
    uint8_t next[512];for(unsigned i=0u;i<sizeof(next);i++)next[i]=(uint8_t)(i^0x6du);
    stage_block(42u,next);
    CHECK(durable_card.protocol.active && !metric(NATIVE_PERF_WRITE_LEASE)->calls);
    /* The requested stop must wait for the staged write's real final SYNC. */
    perf_capture.polls=KUI_NATIVE_PERF_CAPTURE_POLLS-1u;
    perf_pair(false);CHECK(perf_capture.polls>=KUI_NATIVE_PERF_CAPTURE_POLLS && durable_card.protocol.active);
    begin_batch(13u,42u,4u,NULL);physical_complete();
    if(setjmp(terminal)==0) {kui_vmu_native_passive_complete(&frame);CHECK(false);}
    CHECK(terminal_phase==19u && !terminal_error && durable_card.commits==1u);
    CHECK(metric(NATIVE_PERF_WRITE_LEASE)->valid==1u && !metric(NATIVE_PERF_READ_LEASE)->calls);
    CHECK(metric(NATIVE_PERF_FULL_COMPLETE)->ticks_max>=metric(NATIVE_PERF_WRITE_LEASE)->ticks_max);
    CHECK(!durable_card.protocol.active && !durable_card.handling && !durable_card.inflight);
    CHECK(!state.pending && !overlay.pending && release_saw_unpublished);
    idle();
    split_setup();start_capture();transfer_batch(11u,42u,0u,NULL);
    CHECK(metric(NATIVE_PERF_READ_LEASE)->valid==1u && !metric(NATIVE_PERF_WRITE_LEASE)->calls);
    CHECK(durable_card.reads-perf_capture.reads==1u && !durable_card.commits);
    idle();
}
static void reader_only_and_chords(void) {
    CHECK(native_perf_gesture(UINT32_C(0xfffffffd)));
    const uint32_t reject[]={UINT32_MAX,UINT32_C(0xfffff9f9),UINT32_C(0xfffffff5),
        UINT32_C(0xfffffffb),UINT32_C(0xffc7fffd),UINT32_C(0xc7fffffd)};
    for(unsigned i=0u;i<sizeof(reject)/sizeof(*reject);i++)CHECK(!native_perf_gesture(reject[i]));
    split_prepare();input.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_READER_ONLY;
    memset(&input.durable,0,sizeof(input.durable));arm();install_hooks();
    CHECK(reader_only());start_capture();perf_pair(false);
    CHECK(metric(NATIVE_PERF_FAST_COMPLETE)->calls==1u);
    CHECK(!durable_card.ready && !lease_calls && !sd.bytes && !sd.writes);
    CHECK(!(rx_bytes(0u)[2]&VIRTUAL));
}
int main(void) {
    controller_and_clock();storage_and_deferred_terminal();reader_only_and_chords();
    printf("Native performance capture full=%u checks=%u\n",KUI_NATIVE_PERF_FULL_SUBMIT,checks);
    return 0;
}
