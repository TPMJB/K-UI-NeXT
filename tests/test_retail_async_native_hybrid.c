/* SPDX-License-Identifier: GPL-3.0-only */
/* Bound ready-DMA drains without reducing the original catch-up quantum. */
#define main original_async_main
#include "test_retail_async.c"
#undef main
#define KUI_NATIVE_BOUNDS_EMBED 1
#include "test_retail_async_native_bounds.c"

static void ready_batch(bool cooked,unsigned how,unsigned entry) {
    cooked_fixture=cooked;setup(GAME_VBR,how,48,false);mode(2048);
    unsigned target=how==Y?25u:20u;
    CHECK(R.engine.target==target);
    m.delay=entry?RECEIVE_POLLS:0u;
    int32_t token=request(45001u,64u,OUTPUT);
    if(!entry) {
        CHECK(R.engine.stats.call_blocks==target);
        CHECK(R.engine.active && !kui_sci_stream_stats()->polled);
    } else {
        m.delay=0;elapse();
        unsigned before=R.engine.stats.call_blocks+R.engine.stats.irq_blocks;
        if(entry==3u)CHECK(!kui_retail_async_irq());
        else (void)gd(entry==2u?KUI_GD_EXEC:KUI_GD_CHECK,
            entry==2u?0u:(uint32_t)token,entry==2u?0u:STATUS);
        unsigned delta=R.engine.stats.call_blocks+R.engine.stats.irq_blocks-before;
        CHECK(delta==(entry==3u?KUI_RETAIL_NATIVE_IRQ_BATCH_LIMIT:target) && R.engine.active);
    }
    for(unsigned n=0;R.engine.active;++n) {
        unsigned before=R.engine.stats.call_blocks;
        (void)gd(KUI_GD_EXEC,0,0);
        CHECK(R.engine.stats.call_blocks-before<=target && n<1000u);
    }
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    compare(45001u,64u,OUTPUT);
}
static void pending_check_keeps_catchup(unsigned how) {
    cooked_fixture=true;setup(GAME_VBR,how,48,false);mode(2048);
    int32_t token=request(45001u,64u,OUTPUT);
    unsigned before=R.engine.stats.call_blocks;
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
    CHECK(R.engine.stats.call_blocks-before==(how==Y?25u:20u));
    CHECK(finish(token,1,1)==KUI_GD_COMPLETED);compare(45001u,64u,OUTPUT);
}
#ifndef KUI_NATIVE_HYBRID_MAIN
#define KUI_NATIVE_HYBRID_MAIN main
#endif
int KUI_NATIVE_HYBRID_MAIN(void) {
    for(unsigned cooked=0;cooked<2u;++cooked)
        for(unsigned how=X;how<=Y;++how)
            for(unsigned entry=0;entry<4u;++entry)ready_batch(cooked,how,entry);
    pending_check_keeps_catchup(X);pending_check_keeps_catchup(Y);
    test_command_acknowledgment();test_faults_retried();
    test_cancel_writes_nothing_more();test_read_fails_after_retries();
    for(unsigned cooked=0;cooked<2u;++cooked)
        for(unsigned how=X;how<=Y;++how)
            test_request_yields_to_foreign_dma(cooked,how);
    test_irq_yields_to_foreign_dma();test_paused_pio_returns_to_dma();
    test_crc_retry_and_prefetch_share_allowance();
    test_request_before_after_keeps_one_allowance();
    printf("native DMA ceiling: %u checks passed; original 20/25 catch-up retained\n",checks);
    return 0;
}
