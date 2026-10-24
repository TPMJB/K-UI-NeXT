/* SPDX-License-Identifier: GPL-3.0-only */
/* Production GD/SCI/cursor code, adversarial completion speeds, exact image
 * comparison. Work limits must hold even when DMA never makes us wait. */
#define main original_async_main
#include "test_retail_async.c"
#undef main
#define KUI_NATIVE_BOUNDS_EMBED 1
#include "test_retail_async_native_bounds.c"

static unsigned maximum_request,maximum_check,maximum_exec,maximum_irq;
static unsigned blocks(void) {return R.engine.stats.call_blocks+R.engine.stats.irq_blocks;}
static void maximum(unsigned *out,unsigned value) {if(value>*out)*out=value;}
static unsigned visit(uint32_t function,int32_t token) {
    unsigned before=blocks(),polled=kui_sci_stream_stats()->polled;
    unsigned polls_before=m.rx_delay;
    (void)gd(function,function==KUI_GD_CHECK?(uint32_t)token:0u,
        function==KUI_GD_CHECK?STATUS:0u);
    unsigned done=blocks()-before;
    CHECK(done<=(function==KUI_GD_EXEC?8u:1u));
    CHECK(kui_sci_stream_stats()->polled-polled<=1u);
    if(polls_before && m.rx && polls_before>m.rx_delay)
        CHECK(polls_before-m.rx_delay<=2u*KUI_SCI_STREAM_POLL_SLICE+8u);
    maximum(function==KUI_GD_EXEC?&maximum_exec:&maximum_check,done);
    return done;
}
static void fast_dma(bool cooked,unsigned how,unsigned entry) {
    cooked_fixture=cooked;setup(GAME_VBR,how,48,false);mode(2048);
    m.delay=entry==0u?0u:RECEIVE_POLLS;
    unsigned before=blocks();int32_t token=request(45001u,64u,OUTPUT);
    unsigned done=blocks()-before;maximum(&maximum_request,done);
    if(done>1u)fprintf(stderr,"unbounded REQUEST delivered %u SD blocks\n",done);
    CHECK(done<=1u && R.engine.active && !kui_sci_stream_stats()->polled);
    m.delay=0;m.rx_delay=0;
    if(entry==1u) (void)visit(KUI_GD_CHECK,token);
    if(entry==2u) (void)visit(KUI_GD_EXEC,token);
    if(entry==3u) {
        before=blocks();CHECK(!kui_retail_async_irq());done=blocks()-before;
        maximum(&maximum_irq,done);CHECK(done<=1u);
    }
    CHECK(R.engine.active && !kui_sci_stream_stats()->polled);
    CHECK(finish(token,8,1)==KUI_GD_COMPLETED);
    compare(45001u,64u,OUTPUT);
}
static void polling_yields_and_recovers(void) {
    cooked_fixture=true;setup(GAME_VBR,X,48,false);mode(2048);
    m.delay=100000u;
    int32_t token=request(45001u,64u,OUTPUT);
    uint32_t left=m.rx_delay,done=blocks();
    (void)visit(KUI_GD_CHECK,token);
    CHECK(left-m.rx_delay<=4u && blocks()==done && kui_sci_stream_busy());
    left=m.rx_delay;(void)visit(KUI_GD_EXEC,token);
    CHECK(left-m.rx_delay<=2u*KUI_SCI_STREAM_POLL_SLICE+8u);
    CHECK(kui_sci_stream_busy() && R.engine.active && !R.engine.failed);
    m.delay=RECEIVE_POLLS;elapse();
    CHECK(finish(token,8,1)==KUI_GD_COMPLETED);compare(45001u,64u,OUTPUT);
}
static void stalled_dma_has_cumulative_timeout(void) {
    cooked_fixture=true;setup(GAME_VBR,X,48,false);mode(2048);
    m.stall=true;
    int32_t token=request(45001u,3u,OUTPUT);
    for(unsigned n=0;n<1000u && !kui_sci_stream_stats()->overruns;++n) {
        (void)visit(KUI_GD_EXEC,token);CHECK(n<999u);
    }
    CHECK(kui_sci_stream_stats()->overruns && R.shared.service.pending);
    m.stall=false;
    CHECK(finish(token,8,1)==KUI_GD_COMPLETED);compare(45001u,3u,OUTPUT);
}
static void progress_without_interrupts(bool cooked,bool scattered,unsigned how) {
    cooked_fixture=cooked;setup(GAME_VBR,how,scattered?48u:2000u,scattered);mode(2048);
    int32_t token=request(45001u,64u,OUTPUT);
    for(unsigned n=0;R.engine.active;++n) {
        (void)visit(n&1u?KUI_GD_CHECK:KUI_GD_EXEC,token);CHECK(n<10000u);
    }
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    compare(45001u,64u,OUTPUT);
}
static void movie_pipeline_keeps_one_stream(void) {
    cooked_fixture=true;setup(GAME_VBR,X,2000,false);mode(2048);
    int32_t token=request(45001u,64u,OUTPUT);
    unsigned irqs=interrupts(1000u);
    CHECK(irqs>=256u && !R.engine.active);
    CHECK(card.cmd18==1u && !card.cmd12 && !kui_sci_stream_stats()->polled);
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    compare(45001u,64u,OUTPUT);
}
int main(void) {
    for(unsigned cooked=0;cooked<2u;++cooked)
        for(unsigned how=X;how<=Y;++how) {
            for(unsigned entry=0;entry<4u;++entry)fast_dma(cooked,how,entry);
            for(unsigned scattered=0;scattered<2u;++scattered)
                progress_without_interrupts(cooked,scattered,how);
        }
    polling_yields_and_recovers();stalled_dma_has_cumulative_timeout();
    movie_pipeline_keeps_one_stream();
    /* Existing cancellation and CRC cases exercise the new schedule too. */
    test_command_acknowledgment();
    test_faults_retried();test_cancel_writes_nothing_more();
    test_read_fails_after_retries();
    for(unsigned cooked=0;cooked<2u;++cooked)
        for(unsigned how=X;how<=Y;++how)
            test_request_yields_to_foreign_dma(cooked,how);
    test_irq_yields_to_foreign_dma();test_paused_pio_returns_to_dma();
    test_crc_retry_and_prefetch_share_allowance();
    test_request_before_after_keeps_one_allowance();
    printf("native hybrid: %u checks passed; maximum blocks request=%u check=%u exec=%u irq=%u\n",
        checks,maximum_request,maximum_check,maximum_exec,maximum_irq);
    return 0;
}
