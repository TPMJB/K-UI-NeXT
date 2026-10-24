/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
static uint32_t clock_counter=100000u;
uint32_t kui_retail_async_test_read32(uint32_t address) {
    assert(address==UINT32_C(0xffd8000c));return clock_counter;
}
#define KUI_NATIVE_HYBRID_MAIN hybrid_suite_main
#include "test_retail_async_native_hybrid.c"
static unsigned pump(void) {
    unsigned before=R.engine.stats.irq_blocks;
    R.engine.in_irq=2u;(void)kui_retail_async_irq();R.engine.in_irq=0u;
    unsigned delta=R.engine.stats.irq_blocks-before;
    CHECK(delta<=2u);return delta;
}
int main(void) {
    hybrid_suite_main();
    /* Reproduce an active request with no reception left to generate the
     * next interrupt, then recover it through the independent server. */
    cooked_fixture=true;setup(GAME_VBR,X,32,false);mode(2048);m.delay=0u;
    int32_t held=request(45001u,64u,OUTPUT);
    for(unsigned n=0;R.engine.active && kui_sci_stream_busy();++n){elapse();(void)kui_retail_async_irq();CHECK(n<1000u);}
    CHECK(R.engine.active && !kui_sci_stream_busy());
    unsigned stopped=R.engine.cursor.done;
    CHECK(kui_retail_async_irq()==1u && R.engine.cursor.done==stopped);
    for(unsigned n=0;R.engine.active;++n){elapse();(void)pump();CHECK(n<1000u);}
    CHECK(gd(KUI_GD_CHECK,(uint32_t)held,STATUS)==KUI_GD_COMPLETED);compare(45001u,64u,OUTPUT);
    for(unsigned cooked=0;cooked<2u;++cooked)for(unsigned how=X;how<=Y;++how) {
        cooked_fixture=cooked;setup(GAME_VBR,how,48,false);mode(2048);
        m.delay=RECEIVE_POLLS;
        clock_counter=100u;
        int32_t token=request(45001u,64u,OUTPUT);
        unsigned call_before=R.engine.stats.call_blocks;
        CHECK(pump()==0u && R.engine.stats.call_blocks==call_before);
        m.delay=0;elapse();
        CHECK(pump()==2u && R.engine.active);
        /* Complete the real request without supplying another game GD
         * entry. The independent producer must preserve the exact bytes. */
        clock_counter-=500000u;
        for(unsigned n=0;R.engine.active;++n){elapse();CHECK(pump()<=2u && n<1000u);}
        CHECK(R.engine.stats.call_blocks==call_before);
        CHECK(R.engine.stats.releases==500000u);
        CHECK(R.engine.native_max_lba==45001u && R.engine.native_max_count==64u);
        CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
        compare(45001u,64u,OUTPUT);
        CHECK(R.engine.in_irq==0u);
    }
    cooked_fixture=true;setup(GAME_VBR,X,48,false);mode(2048);
    m.sar=0x0c123450u;m.dar=0x0c156780u;m.tcr=0x345u;m.chcr=1u;
    int32_t token=request(45001u,64u,OUTPUT);
    for(unsigned n=0;R.engine.active;++n) {
        unsigned before=kui_sci_stream_stats()->polled;(void)pump();
        CHECK(kui_sci_stream_stats()->polled-before<=1u && n<1000u);
        CHECK(m.sar==0x0c123450u && m.dar==0x0c156780u && m.tcr==0x345u && m.chcr==1u);
    }
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);compare(45001u,64u,OUTPUT);
    puts("cooperative reader: two-block bound, pending-DMA yield, independent completion, exact data and full request timing passed");
    return 0;
}
