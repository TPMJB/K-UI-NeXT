/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual card/DMA reader: sparse CHECK must not wait, tightly repeated CHECK
 * must make progress, and EXEC must retain its original movie catch-up. */
#include <assert.h>
#include <stdint.h>
static uint32_t test_line,clock_counter=5u;
uint32_t kui_retail_async_test_read32(uint32_t address) {
    if(address==UINT32_C(0xa05f810c))return test_line;
    assert(address==UINT32_C(0xffd8000c));
    uint32_t result=clock_counter;clock_counter-=19u;return result;
}
#define main original_async_main
#include "test_retail_async.c"
#undef main
#define KUI_NATIVE_BOUNDS_EMBED 1
#include "test_retail_async_native_bounds.c"

static unsigned blocks(void) {return R.engine.stats.call_blocks+R.engine.stats.irq_blocks;}
static int32_t begin(unsigned how) {
    setup(GAME_VBR,how,48,false);mode(2048);test_line=100u;
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(R.engine.active && kui_sci_stream_busy() && !blocks());
    return token;
}
static void sparse_and_tight(unsigned how) {
    int32_t token=begin(how);unsigned target=how==Y?25u:20u;
    for(unsigned n=0;n<64u;++n) {
        test_line=(test_line+3u)&0x3ffu;
        CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
        CHECK(!blocks() && !R.engine.stats.waits);
    }
    /* An arrived block is delivered immediately, even on a sparse CHECK. */
    elapse();test_line+=3u;
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
    CHECK(blocks()==1u && !R.engine.stats.waits);
    /* Fifteen close polls stay brief; the sixteenth earns full catch-up. */
    test_line+=3u;
    for(unsigned n=0;n<15u;++n) {
        CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
        CHECK(blocks()==1u && !R.engine.stats.waits);
    }
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
    CHECK(blocks()==1u+target && R.engine.stats.waits==1u);
    CHECK(finish(token,0,0)==KUI_GD_COMPLETED);compare(45001u,64u,OUTPUT);
}
static void exec_and_irq(unsigned how) {
    int32_t token=begin(how);unsigned target=how==Y?25u:20u;
    /* Nonblocking checks between EXECs must not shrink the EXEC allowance. */
    for(unsigned n=0;R.engine.active;++n) {
        test_line+=3u;unsigned before=blocks();
        (void)gd(KUI_GD_CHECK,(uint32_t)token,STATUS);
        CHECK(blocks()==before);
        (void)gd(KUI_GD_EXEC,0u,0u);
        CHECK(blocks()-before<=target && (blocks()-before==target || !R.engine.active));
        CHECK(!R.engine.stats.exec_int && n<20u);
    }
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    compare(45001u,64u,OUTPUT);
    token=begin(how);m.delay=0u;elapse();
    CHECK(!kui_retail_async_irq() && blocks()==KUI_RETAIL_NATIVE_IRQ_BATCH_LIMIT);
    CHECK(finish(token,0,1)==KUI_GD_COMPLETED);compare(45001u,64u,OUTPUT);
}
static void spin_reset(unsigned how) {
    int32_t token=begin(how);
    for(unsigned n=0;n<15u;++n)(void)gd(KUI_GD_CHECK,(uint32_t)token,STATUS);
    CHECK(!blocks() && !R.engine.stats.waits);
    test_line=0u; /* backwards/wrapped line breaks a tight-poll sequence */
    (void)gd(KUI_GD_CHECK,(uint32_t)token,STATUS);
    CHECK(!blocks() && !R.engine.stats.waits && R.engine.stats.exec_int==1u);
    (void)gd(KUI_GD_EXEC,0u,0u);CHECK(R.engine.stats.exec_int==0u);
    CHECK(finish(token,0,1)==KUI_GD_COMPLETED);compare(45001u,64u,OUTPUT);
    token=begin(how);CHECK(R.engine.stats.exec_int==0u);
    CHECK(finish(token,4,0)==KUI_GD_COMPLETED);compare(45001u,64u,OUTPUT);
}
int main(void) {
    for(unsigned cooked=0;cooked<2u;++cooked) {
        cooked_fixture=cooked;
        for(unsigned how=X;how<=Y;++how) {
            sparse_and_tight(how);exec_and_irq(how);spin_reset(how);
            test_request_yields_to_foreign_dma(cooked,how);
        }
    }
    test_command_acknowledgment();test_faults_retried();
    test_cancel_writes_nothing_more();test_read_fails_after_retries();
    test_irq_yields_to_foreign_dma();test_paused_pio_returns_to_dma();
    test_crc_retry_and_prefetch_share_allowance();
    test_request_before_after_keeps_one_allowance();
    printf("paced reader: %u checks; sparse/tight CHECK, full EXEC20/25, IRQ8, PIO1, faults, exact cooked/raw bytes passed\n",checks);
    return 0;
}
