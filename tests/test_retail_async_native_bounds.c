/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the real native reader and SCI/card/DMA model. A foreign DMA1
 * owner forces PIO; the requested output must span several bounded entries,
 * rather than turning one masked GD call or SCI interrupt into a whole read.
 * Reuse the existing byte-for-byte reference image and guest-vector fixture. */
#define main existing_async_suite_main
#include "test_retail_async.c"
#undef main

static void test_request_yields_to_foreign_dma(bool cooked,unsigned how) {
    cooked_fixture=cooked;
    setup(GAME_VBR,how,48,false);
    mode(2048);
    /* The tuple belongs to the game and must survive every PIO fallback. */
    m.sar=UINT32_C(0x0c123450);m.dar=UINT32_C(0x0c156780);
    m.tcr=UINT32_C(0x345);m.chcr=1u;
    unsigned before=kui_sci_stream_stats()->polled;
    int32_t token=request(45001u,64u,OUTPUT);
    unsigned polled=kui_sci_stream_stats()->polled-before;
    CHECK(polled<=1u);
    CHECK(R.shared.service.pending && R.engine.active);
    CHECK(R.engine.cursor.done<64u && !kui_sci_stream_busy());
    CHECK(!hooked() && hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
    CHECK(m.sar==UINT32_C(0x0c123450) && m.dar==UINT32_C(0x0c156780) &&
          m.tcr==UINT32_C(0x345) && m.chcr==1u);
    /* No interrupt will arrive for a paused PIO path. Ordinary EXEC/CHECK
     * calls alone must make bounded progress to the exact terminal result. */
    for(unsigned calls=0;calls<1000u;++calls) {
        before=kui_sci_stream_stats()->polled;
        int32_t status=gd(calls&1u?KUI_GD_EXEC:KUI_GD_CHECK,
            calls&1u?0u:(uint32_t)token,calls&1u?0u:STATUS);
        CHECK(kui_sci_stream_stats()->polled-before<=1u);
        CHECK(m.sar==UINT32_C(0x0c123450) && m.dar==UINT32_C(0x0c156780) &&
              m.tcr==UINT32_C(0x345) && m.chcr==1u);
        CHECK(!hooked() && hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
        if(!(calls&1u) && status==KUI_GD_COMPLETED) break;
        CHECK((calls&1u)?status==0:status==KUI_GD_PROCESSING);
        CHECK(calls<999u);
    }
    CHECK(get(STATUS+8u)==64u*2048u && !get(STATUS+4u));
    compare(45001u,64u,OUTPUT);
}

static void test_irq_yields_to_foreign_dma(void) {
    cooked_fixture=true;
    setup(GAME_VBR,X,48,false);mode(2048);
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(kui_sci_stream_busy() && hooked());
    /* The existing model's hostile takeover changes SAR during reception.
     * The IRQ must preserve that owner and leave the unfinished read pending. */
    m.foreign_during_rx=true;elapse();
    unsigned before=kui_sci_stream_stats()->polled;
    CHECK(!kui_retail_async_irq());
    CHECK(kui_sci_stream_stats()->polled-before<=1u);
    CHECK(R.engine.active && R.engine.cursor.done<64u);
    CHECK(R.shared.service.pending && !kui_sci_stream_busy());
    CHECK(m.sar==UINT32_C(0x0c200000) && m.chcr==UINT32_C(0x4911));
    CHECK(!hooked() && hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
    CHECK(finish(token,0,1)==KUI_GD_COMPLETED);
    compare(45001u,64u,OUTPUT);
}

static void test_paused_pio_returns_to_dma(void) {
    cooked_fixture=true;
    setup(GAME_VBR,X,48,false);mode(2048);
    m.chcr=1u;
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(kui_sci_stream_stats()->polled==1u && !kui_sci_stream_busy());
    CHECK(R.engine.active && R.shared.service.pending);
    /* DMA1 becomes available between entries; keep the ready block and
     * immediately return to the existing background DMA pipeline. */
    m.chcr=0u;
    unsigned starts=m.dma_starts;
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
    CHECK(m.dma_starts>starts && kui_sci_stream_stats()->polled==1u);
    CHECK(finish(token,4,1)==KUI_GD_COMPLETED);
    CHECK(!R.shared.service.error && m.chcr==0u);
    compare(45001u,64u,OUTPUT);
}

static void test_crc_retry_and_prefetch_share_allowance(void) {
    cooked_fixture=true;
    setup(GAME_VBR,X,48,false);mode(2048);
    card.corrupt_lba=manifest.slots[manifest.slots[2].track.first_extent].extent.card_lba+4u;
    card.corrupt_count=1u;
    m.chcr=1u;
    int32_t token=request(45001u,12u,OUTPUT);
    CHECK(kui_sci_stream_stats()->polled==1u);
    for(unsigned calls=0;calls<1000u;++calls) {
        unsigned before=kui_sci_stream_stats()->polled;
        int32_t status=gd(KUI_GD_CHECK,(uint32_t)token,STATUS);
        CHECK(kui_sci_stream_stats()->polled-before<=1u);
        if(status==KUI_GD_COMPLETED) break;
        CHECK(status==KUI_GD_PROCESSING && calls<999u);
    }
    CHECK(kui_sci_stream_stats()->crc_errors==1u && !R.shared.service.error);
    compare(45001u,12u,OUTPUT);
}

static void test_request_before_after_keeps_one_allowance(void) {
    cooked_fixture=true;
    setup(GAME_VBR,X,48,false);mode(2048);m.chcr=1u;
    int32_t token=request(45001u,1u,OUTPUT);
    CHECK(R.shared.service.pending);
    for(unsigned calls=0;R.shared.service.pending;++calls) {
        unsigned before=kui_sci_stream_stats()->polled;
        /* Before-work may complete the old read. REQUEST must still reject
         * its owned handle, so after-work cannot begin a second delivery. */
        CHECK(gd(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM)==0);
        CHECK(kui_sci_stream_stats()->polled-before<=1u);
        CHECK(R.shared.service.command==KUI_GD_DMAREAD && calls<10u);
    }
    CHECK(!R.engine.active);
    CHECK(gd(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM)==0);
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    unsigned before=kui_sci_stream_stats()->polled;
    token=request(45002u,1u,OUTPUT);
    CHECK(kui_sci_stream_stats()->polled-before<=1u);
    CHECK(finish(token,0,1)==KUI_GD_COMPLETED);
    compare(45002u,1u,OUTPUT);
}

int main(void) {
    for(unsigned cooked=0;cooked<2u;++cooked)
        for(unsigned how=X;how<=Y;++how)
            test_request_yields_to_foreign_dma(cooked,how);
    test_irq_yields_to_foreign_dma();
    test_paused_pio_returns_to_dma();
    test_crc_retry_and_prefetch_share_allowance();
    test_request_before_after_keeps_one_allowance();
    printf("native background bounded fallback: %u checks passed\n",checks);
    return 0;
}
