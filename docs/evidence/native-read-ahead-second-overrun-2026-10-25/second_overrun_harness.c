/* Scratch-only transport experiments. No candidate policy is changed. */
#define main original_native_readahead_aux_test_main
#include "test_native_readahead_aux.c"
#undef main

static void double_loss(void) {
    setup_real();begin(DISC_BASE,4u);complete(DISC_BASE,4u);
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && kui_sci_stream_busy());
    region.shared.stream.unrepaired=1u;
    m.overrun_after=388u;m.overrun_again=458u;m.rx_delay=0u;
    region.engine.in_irq=1u;
    CHECK(visit(KUI_NATIVE_RA_DELIVER)==1u && C.deferred && !C.abandoned &&
        region.shared.stream.state==RESUME && !kui_sci_stream_busy());
    unsigned fill=region.shared.stream.fill;
    uint32_t first_lost=region.shared.stream.lost[fill];
    CHECK(first_lost==390u && region.shared.stream.unrepaired==1u &&
        region.shared.stream.stats.repaired==1u && !region.shared.stream.stats.overruns);
    printf("first ORER: injected_at=388 fill=%u lost=%u unrepaired=1 state=RESUME repaired=1 overrun=0 abandon=0\n",fill,first_lost);
    region.engine.in_irq=0u;
    CHECK(visit(KUI_NATIVE_RA_TICK)==1u && kui_sci_stream_busy() && !C.deferred);
    m.rx_delay=0u;region.engine.in_irq=1u;
    CHECK(visit(KUI_NATIVE_RA_DELIVER)==1u && C.abandoned && !C.spec_owned &&
        region.shared.stream.state==LOST && !kui_sci_stream_busy());
    const struct kui_native_ra_failure_snapshot *s=&C.diagnostics.first;
    CHECK(s->stage==KUI_NATIVE_RA_STAGE_POLL && s->result==KUI_SCI_STREAM_OVERRUN &&
        s->loan_state>>16==1u && s->lost==first_lost<<(16u*fill) &&
        region.shared.stream.lost[fill]==first_lost &&
        region.shared.stream.stats.overruns==1u && region.shared.stream.stats.repaired==1u &&
        !region.shared.stream.stats.crc_errors && !region.shared.stream.stats.token_errors &&
        !C.stats.crc_errors && !C.stats.token_errors);
    printf("second ORER: injected_at=458 fill=%u current_lost=%u unrepaired=1 preTCR=%u preCHCR=%04X preSSR=%02X result=POLL/OVERRUN state=LOST abandon=1 CRC=0 TOKEN=0\n",fill,first_lost,s->pre.tcr,s->pre.chcr,s->pre.sci>>24);
}
static void pio_block(void) {
    uint32_t next;
    CHECK(C.current_event==KUI_NATIVE_RA_TICK && !region.engine.active &&
        !region.shared.service.pending && !region.shared.service.executing &&
        !region.engine.failed && region.engine.opened && region.shared.port.acquired &&
        !region.shared.port.fault && !C.spec_owned && !C.deferred &&
        kui_native_ra_cache_next(&C.cache,&next));
    CHECK(region.shared.stream.state==PAUSED && region.shared.stream.position==next);
    enum kui_sci_stream_result result=kui_sci_stream_fetch(next,256u,true);
    CHECK(result==KUI_SCI_STREAM_OK && !kui_sci_stream_busy() &&
        region.shared.stream.state==PAUSED && region.shared.stream.position==next+1u);
    const uint8_t *block=kui_sci_stream_take(next,&result);
    CHECK(block && result==KUI_SCI_STREAM_OK &&
        !memcmp(block,disk[next-CARD_BASE],512u) &&
        kui_native_ra_cache_append(&C.cache,next,block));
}
static void fresh_paused_pio(void) {
    setup_real();begin(DISC_BASE,4u);complete(DISC_BASE,4u);
    region.shared.stream.unrepaired=1u;
    /* The same DMA-overrun injection remains armed. PIO clocks one byte at
     * a time and never starts that vulnerable receive-only DMA transfer. */
    m.overrun_after=388u;m.overrun_again=458u;m.chcr2=1u;
    uint32_t sar=m.sar,dar=m.dar,tcr=m.tcr,chcr=m.chcr,dmaor=m.dmaor;
    unsigned dma_starts=m.dma_starts,cmd18=card.cmd18,cmd12=card.cmd12;
    uint32_t polled=region.shared.stream.stats.polled;
    C.current_event=KUI_NATIVE_RA_TICK;
    for(unsigned heartbeat=0u;heartbeat<12u;++heartbeat) {
        uint32_t count=C.cache.count;
        pio_block();
        CHECK(C.cache.count==count+1u && !C.abandoned && !C.diagnostics.failures &&
            m.sar==sar && m.dar==dar && m.tcr==tcr && m.chcr==chcr && m.dmaor==dmaor &&
            m.dma_starts==dma_starts && card.cmd18==cmd18 && card.cmd12==cmd12 &&
            region.shared.stream.unrepaired==1u);
    }
    CHECK(region.shared.stream.stats.polled-polled==12u &&
        !region.shared.stream.stats.overruns && !region.shared.stream.stats.repaired &&
        !region.shared.stream.stats.crc_errors && !region.shared.stream.stats.token_errors &&
        m.overrun_after==388u && m.overrun_again==458u);
    puts("proposed fresh-PAUSED optional PIO: 12 heartbeat visits, 12 checked512B blocks appended; CRC/TOKEN/overrun/abandon=0; DMA starts/SAR/DAR/TCR/CHCR/DMAOR unchanged; no extra CMD18/CMD12; CH2 active and identical DMA-overrun injection remains armed.");
}
int main(void) {
    double_loss();fresh_paused_pio();
    puts("PASS: software/card/register fixture only. PIO is a scratch transport proposal, not a built auxiliary candidate or a measured console performance result. RESUME is excluded because fetch(polled=true) would still restart DMA there.");
    return 0;
}
