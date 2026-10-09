/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the accepted independent byte/DMAC model. New register injection and
 * cleanup ledger exercise the actual first-fault capture branches. */
#define main sci_bus_original_matrix_main
#define kui_sci_sd_test_read fault_model_read
#define kui_sci_sd_test_write fault_model_write
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "test_sci_sd_bus.c"
#pragma GCC diagnostic pop
#undef main
#undef kui_sci_sd_test_read
#undef kui_sci_sd_test_write
enum injection {NONE,FEED_TIMEOUT,FEED_ERROR,COMPLETE_ERROR,COMPLETE_DMAOR,POST_COUNT,POST_DMAOR};
static enum injection injected;
static bool require_capture,post_armed;
static unsigned cleanups;
uint32_t kui_sci_sd_test_read(uint32_t address,unsigned width) {
    if(address==0xffa0002cu || address==0xffa00028u) {
        assert(width==4u);return address==0xffa0002cu?0x12c0u:0x80u;
    }
    uint32_t value=fault_model_read(address,width);
    if(injected==POST_DMAOR && address==CHCR1 && (value&2u)) post_armed=true;
    if(hw.dma_starts && address==SSR) {
        if(injected==FEED_TIMEOUT) value&=~TDRE;
        if(injected==FEED_ERROR || (injected==COMPLETE_ERROR && hw.dma_bytes==512u)) value|=ORER;
    }
    if(hw.dma_starts && hw.dma_bytes==512u && injected==COMPLETE_DMAOR && address==DMAOR) value|=4u;
    if(post_armed && address==DMAOR) value|=4u;
    if(hw.dma_starts && hw.dma_bytes==512u && injected==POST_COUNT && address==TCR1) value=1u;
    return value;
}
void kui_sci_sd_test_write(uint32_t address,uint32_t value,unsigned width) {
    if(require_capture && ((address==SCR && value==0u && hw.scr!=0u) ||
                          (address==CHCR1 && value==0u && (hw.chcr&1u)))) {
        const struct kui_sci_sd_diagnostic *d=kui_sci_sd_diagnostic_get();
        assert(d->phase && d->smr==0x80u && d->brr==hw.brr);
        if(address==SCR) assert(d->scr==hw.scr);
        if(hw.chcr&1u) assert(d->chcr1&1u);
        ++cleanups;
    }
    fault_model_write(address,value,width);
}
static struct kui_sci_sd_diagnostic snapshot(void) {
    unsigned reads=hw.reads,writes=hw.writes;
    struct kui_sci_sd_diagnostic d=*kui_sci_sd_diagnostic_get();
    assert(hw.reads==reads && hw.writes==writes);return d;
}
static void unchanged_after_release(struct kui_sci_sd_diagnostic first) {
    require_capture=false;kui_sci_sd_release();
    struct kui_sci_sd_diagnostic after=snapshot();
    assert(!memcmp(&first,&after,sizeof(first)));
    hw.dmaor=0x0301u;restored();
    hw.ssr|=ORER;
    assert(kui_sci_sd_acquire()==KUI_LOADER_SD_UNSUPPORTED);
    after=snapshot();assert(!memcmp(&first,&after,sizeof(first)));
    hw.ssr=TDRE|TEND;hw.timeout=hw.overrun=false;injected=NONE;
    assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
    after=snapshot();assert(!after.phase && after.started==first.started &&
        after.success==first.success && after.fallback==first.fallback);
    kui_sci_sd_release();restored();
}
static void scalar_case(const struct kui_loader_sd_bus *bus,bool error) {
    injected=NONE;require_capture=false;reset();assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
    hw.timeout=!error;hw.overrun=error;hw.fault_byte=1u;
    require_capture=true;cleanups=0;
    assert(bus->transfer(NULL,0xffu,true)==0xffu);
    struct kui_sci_sd_diagnostic d=snapshot();
    assert(d.phase==KUI_SCI_PHASE_FLAG && d.reason==(error?KUI_SCI_REASON_SCI:KUI_SCI_REASON_TIMEOUT));
    assert(d.expected==RDRF && d.polls==(error?hw.byte_polls:10000u));
    assert(d.scr==0x30u && d.ssr==(error?(TDRE|TEND|ORER):TDRE));
    assert(d.chcr1==0x4000u && d.tcr1==7u && d.dmaor==0x301u && d.chcr2==0x12c0u && d.tcr2==0x80u);
    assert(cleanups==1u && hw.scr==0u && !kui_sci_sd_healthy());
    unchanged_after_release(d);
}
static void dma_case(const struct kui_loader_sd_bus *bus,enum injection kind,
                     bool completion_stall,bool late_error,unsigned phase,unsigned reason) {
    _Alignas(32) uint8_t guarded[576];uint16_t crc=0xa55au;
    injected=NONE;require_capture=post_armed=false;dma_ready(bus);
    struct kui_sci_sd_diagnostic before=snapshot();
    hw.byte_polls=1u;hw.dma_completion_stall=completion_stall;hw.dma_late_error=late_error;
    injected=kind;require_capture=true;cleanups=0;memset(guarded,0x5a,sizeof(guarded));
    assert(!bus->transfer_block(NULL,NULL,guarded+32u,512u,false,&crc));
    struct kui_sci_sd_diagnostic d=snapshot();
    if(d.phase!=phase || d.reason!=reason) fprintf(stderr,"fault injection %u: phase %u reason %u expected %u/%u\n",kind,d.phase,d.reason,phase,reason);
    assert(d.phase==phase && d.reason==reason && d.scr==0x70u && (d.chcr1&1u));
    assert(d.chcr2==0x12c0u && d.tcr2==0x80u && d.started==before.started+1u && d.success==before.success);
    assert(d.fallback==before.fallback && crc==0xa55au && !kui_sci_sd_healthy());
    assert(cleanups==2u && hw.scr==0u && hw.chcr==0x4000u && hw.tcr==7u);
    assert(hw.sar==0x0c002000u && hw.dar==0x0c004000u && hw.irq_disables==1u && hw.irq_restores==1u);
    for(unsigned i=0;i<32u;i++) assert(guarded[i]==0x5au && guarded[i+544u]==0x5au);
    unsigned reads=hw.reads,writes=hw.writes;
    assert(bus->transfer(NULL,0xffu,false)==0xffu &&
           !bus->transfer_block(NULL,NULL,guarded+32u,512u,false,&crc));
    assert(hw.reads==reads && hw.writes==writes);
    if(reason==KUI_SCI_REASON_TIMEOUT) assert(d.polls==10000u);
    unchanged_after_release(d);
}
static void counters_case(const struct kui_loader_sd_bus *bus,bool occupied) {
    _Alignas(32) uint8_t guarded[576];uint16_t crc=0u;
    injected=NONE;require_capture=false;dma_ready(bus);
    if(occupied) hw.chcr=0x4001u;
    struct kui_sci_sd_diagnostic before=snapshot();memset(guarded,0x5a,sizeof(guarded));
    assert(bus->transfer_block(NULL,NULL,guarded+32u,512u,false,&crc));
    struct kui_sci_sd_diagnostic after=snapshot();
    assert(!after.phase && after.started==before.started+!occupied && after.success==before.success+!occupied);
    assert(after.fallback==before.fallback+occupied && crc==crc16_reference(guarded+32u,512u));
    for(unsigned i=0;i<32u;i++) assert(guarded[i]==0x5au && guarded[i+544u]==0x5au);
    if(occupied) assert(!hw.dma_starts && !hw.dma_writes && hw.chcr==0x4001u);
    kui_sci_sd_release();hw.chcr=0x4000u;restored();
}
int main(void) {
    const struct kui_loader_sd_bus *bus=kui_sci_sd_bus();
    scalar_case(bus,false);scalar_case(bus,true);
    dma_case(bus,FEED_TIMEOUT,false,false,KUI_SCI_PHASE_FEED,KUI_SCI_REASON_TIMEOUT);
    dma_case(bus,FEED_ERROR,false,false,KUI_SCI_PHASE_FEED,KUI_SCI_REASON_SCI);
    dma_case(bus,NONE,true,false,KUI_SCI_PHASE_COMPLETE,KUI_SCI_REASON_TIMEOUT);
    dma_case(bus,COMPLETE_ERROR,true,false,KUI_SCI_PHASE_COMPLETE,KUI_SCI_REASON_SCI);
    dma_case(bus,COMPLETE_DMAOR,true,false,KUI_SCI_PHASE_COMPLETE,KUI_SCI_REASON_DMAOR);
    dma_case(bus,POST_DMAOR,false,false,KUI_SCI_PHASE_POST,KUI_SCI_REASON_DMAOR);
    dma_case(bus,POST_COUNT,false,false,KUI_SCI_PHASE_POST,KUI_SCI_REASON_COUNT);
    counters_case(bus,false);counters_case(bus,true);
    puts("SCI first-fault: nine branch injections, pre-cleanup snapshots, bounded latches, restoration and counters PASS");
    return 0;
}
