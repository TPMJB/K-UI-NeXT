/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise capture arming, live worker sampling and private maxima reset
 * alongside the complete real persistent VMU/storage/ownership suite. */
#define KUI_VMU_NATIVE_BACKGROUND 1
#define KUI_RETAIL_ASYNC_TEST 1
#define KUI_NATIVE_READER_DIAGNOSTIC 1
#define main persistent_suite_main
#include "test_vmu_native_persistent.c"
#undef main
static kui_retail_async_region_type observed_region;
void *kui_vmu_native_reader_test_region(uint32_t address) {
    CHECK(kui_vmu_native_background_region_address(config.lease.payload,address));
    return &observed_region;
}
int main(void) {
    persistent_suite_main();
    diagnostic_setup();
    for(unsigned i=0;i<30u;i++)diagnostic_pair(true);
    CHECK(diagnostic.active && !reader_capture.samples);
    diagnostic_pair(false);
    CHECK(reader_capture.samples==1u && !reader_capture.top_count);
    observed_region.engine.stats.execs=500u;observed_region.engine.stats.exec_int=30u;
    observed_region.engine.stats.call_blocks=20u;observed_region.engine.stats.irq_blocks=8u;
    observed_region.shared.stream.stats.polled=1u;
    test_clock.counter-=781250u;
    diagnostic_pair(false);
    CHECK(reader_capture.samples==2u && reader_capture.top_count==1u);
    CHECK(reader_capture.top[0].gap>=781250u && reader_capture.call_max==500u && reader_capture.irq_max==30u);
    CHECK(reader_capture.top[0].call_blocks==20u && reader_capture.top[0].irq_blocks==8u);
    CHECK(reader_capture.top[0].polled==1u);
    CHECK(!observed_region.engine.stats.execs && !observed_region.engine.stats.exec_int);
    puts("B stall worker: gesture, live snapshots, interval maxima reset and one-second gap capture passed");
    return 0;
}
