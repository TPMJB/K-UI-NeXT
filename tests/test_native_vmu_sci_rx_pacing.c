/* SPDX-License-Identifier: GPL-3.0-only */
/* Extend the retained MMIO model with delayed RX-DMA arbitration. The old
 * model consumed RDR on the same edge that completed every wire byte; here
 * the request remains pending while a competing DMA user owns bus cycles.
 * No production register read, flag clear or fault recovery is substituted. */
#define main retained_sci_bus_fixture_tests
#define kui_sci_sd_test_read retained_sci_bus_fixture_read
#define kui_sci_sd_test_write retained_sci_bus_fixture_write
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "test_sci_sd_bus.c"
#pragma GCC diagnostic pop
#undef kui_sci_sd_test_read
#undef kui_sci_sd_test_write
#undef main

#ifndef EXPECT_NATIVE_PACED
#define EXPECT_NATIVE_PACED 0
#endif
#ifndef EXPECT_NATIVE_PIO
#define EXPECT_NATIVE_PIO 0
#endif

static struct {
    bool active, pending, strict_tdre, tdre_witness, awaiting_credit;
    unsigned delay, left, drained, overruns, concealed_reads, conceal_budget;
} arbitration;

void kui_sci_sd_test_write(uint32_t address,uint32_t value,unsigned width) {
    if(arbitration.active && arbitration.strict_tdre && address==SSR &&
       (hw.scr&TE) && !(value&TDRE)) {
        /* A sampled 1 is required before every write of 0, even when RX
         * has finished. Delaying visibility below makes that requirement
         * observable instead of assuming TDRE was seen early. */
        assert(arbitration.tdre_witness);
        arbitration.tdre_witness=false;
        if(hw.scr&0x40u) arbitration.awaiting_credit=true;
    }
    retained_sci_bus_fixture_write(address,value,width);
}

uint32_t kui_sci_sd_test_read(uint32_t address,unsigned width) {
    if(!arbitration.active) return retained_sci_bus_fixture_read(address,width);
    /* Keep the retained model's immediate drain disabled. Only the delayed
     * arbiter below can satisfy a pending receive request. DMA2's precise
     * workload is deliberately not modeled or accessed by production code. */
    hw.dma_stall=true;
    if(arbitration.pending && (hw.ssr&RDRF) && dma_running(9u) &&
       (hw.scr&0x40u) && (hw.dmaor&7u)==1u) {
        if(arbitration.left) --arbitration.left;
        else {
            assert(hw.tcr && hw.sar==0x1fe00014u);
            *dma_pointer(hw.dar++)=hw.rdr;
            ++hw.dma_bytes;++arbitration.drained;
            hw.ssr&=(uint8_t)~RDRF;
            if(!--hw.tcr) hw.dma_complete_delay=2u;
            arbitration.pending=false;
            arbitration.awaiting_credit=false;
            arbitration.concealed_reads=arbitration.conceal_budget;
        }
    }
    if(arbitration.strict_tdre && address==SSR) {
        /* This is a flag-observation control, not a timing claim about
         * Dreamcast TDRE edges. RX credit can become visible before the
         * fixture permits software to witness TDRE's asserted value. */
        if(arbitration.awaiting_credit || arbitration.concealed_reads) {
            hw.missing_flags|=TDRE;
            if(!arbitration.awaiting_credit && arbitration.concealed_reads)
                --arbitration.concealed_reads;
        } else hw.missing_flags&=(uint8_t)~TDRE;
    }
    bool before=(hw.ssr&RDRF)!=0u;
    uint32_t value=retained_sci_bus_fixture_read(address,width);
    if(address==SSR && (value&TDRE)) arbitration.tdre_witness=true;
    if(!before && (hw.ssr&RDRF) && dma_running(9u) && (hw.scr&0x40u)) {
        arbitration.pending=true;
        arbitration.left=arbitration.delay;
    }
    if(hw.ssr&ORER) ++arbitration.overruns;
    return value;
}

static void receive_credit_does_not_replace_tdre(const struct kui_loader_sd_bus *bus,bool timeout) {
    if(!EXPECT_NATIVE_PACED) return;
    _Alignas(32) uint8_t storage[576];
    dma_ready(bus);memset(storage,0xa5,sizeof(storage));
    arbitration=(__typeof__(arbitration)){.active=true,.strict_tdre=true,
        .delay=8u,.conceal_budget=timeout?UINT32_MAX:3u};
    uint16_t crc=0xa55au;
    bool ok=bus->transfer_block(NULL,NULL,storage+32u,512u,false,&crc);
    if(timeout) {
        assert(!ok && !kui_sci_sd_healthy() && crc==0xa55au);
        assert(arbitration.drained==1u && hw.bytes==1u && hw.polls<11000u);
        unsigned reads=hw.reads,writes=hw.writes;
        assert(!bus->transfer_block(NULL,NULL,storage+32u,512u,false,&crc));
        assert(hw.reads==reads && hw.writes==writes);
    } else {
        assert(ok && kui_sci_sd_healthy() && !arbitration.overruns);
        assert(arbitration.drained==512u && hw.bytes==512u);
        assert(crc==crc16_reference(storage+32u,512u));
        for(unsigned i=0;i<512u;i++) assert(storage[32u+i]==received_byte(i));
    }
    assert(hw.sar==0x0c002000u && hw.dar==0x0c004000u && hw.tcr==7u);
    assert(hw.chcr==0x4000u && hw.dmaor==0x0301u && !hw.irq_disabled);
    arbitration.active=false;hw.missing_flags=0;
    kui_sci_sd_release();restored();
}

static void delayed_block(const struct kui_loader_sd_bus *bus,unsigned delay) {
    _Alignas(32) uint8_t storage[576];
    dma_ready(bus);memset(storage,0xa5,sizeof(storage));
    arbitration=(__typeof__(arbitration)){.active=true,.delay=delay};
    uint16_t crc=0xa55au;
    bool ok=bus->transfer_block(NULL,NULL,storage+32u,512u,false,&crc);
    if(EXPECT_NATIVE_PACED || EXPECT_NATIVE_PIO || !delay) {
        assert(ok && kui_sci_sd_healthy());
        assert(crc==crc16_reference(storage+32u,512u));
        for(unsigned i=0;i<512u;i++) assert(storage[i+32u]==received_byte(i));
        assert(hw.bytes==512u && !arbitration.overruns);
        assert(hw.dma_bytes==(EXPECT_NATIVE_PIO?0u:512u));
        assert(arbitration.drained==(EXPECT_NATIVE_PIO?0u:512u));
    } else {
        /* The exact old block path clocks another byte before a contended
         * RX request drains, then latches its normal terminal bus failure. */
        assert(!ok && !kui_sci_sd_healthy() && arbitration.overruns);
        assert(crc==0xa55au && hw.bytes<512u && hw.dma_starts==1u);
        unsigned reads=hw.reads,writes=hw.writes,bytes=hw.bytes;
        assert(!bus->transfer_block(NULL,NULL,storage+32u,512u,false,&crc));
        assert(bus->transfer(NULL,0xff,false)==0xff);
        assert(hw.reads==reads && hw.writes==writes && hw.bytes==bytes);
    }
    assert(hw.sar==0x0c002000u && hw.dar==0x0c004000u && hw.tcr==7u);
    assert(hw.chcr==0x4000u && hw.dmaor==0x0301u);
    assert(hw.irq_disables==hw.irq_restores && !hw.irq_disabled);
    for(unsigned i=0;i<32u;i++) assert(storage[i]==0xa5u && storage[544u+i]==0xa5u);
    arbitration.active=false;
    kui_sci_sd_release();restored();
}

static void terminal_failures(const struct kui_loader_sd_bus *bus) {
    if(EXPECT_NATIVE_PIO) return;
    for(unsigned fault=0;fault<6u;fault++) {
        _Alignas(32) uint8_t storage[576];
        dma_ready(bus);memset(storage,0xa5,sizeof(storage));
        hw.fault_byte=137u;
        hw.timeout=fault==0u;hw.overrun=fault==1u;hw.dma_stall=fault==2u;
        hw.dma_completion_stall=fault==3u;hw.dma_error=fault==4u;
        hw.dma_late_error=fault==5u;
        uint16_t crc=0xa55au;
        assert(!bus->transfer_block(NULL,NULL,storage+32u,512u,false,&crc));
        assert(crc==0xa55au && !kui_sci_sd_healthy());
        assert(hw.dma_starts==1u && hw.bytes<=512u && hw.polls<40000u);
        assert(!hw.active && !hw.queued && !hw.irq_disabled);
        assert(hw.sar==0x0c002000u && hw.dar==0x0c004000u && hw.tcr==7u && hw.chcr==0x4000u);
        assert(hw.dmaor==(fault>=4u?0x0305u:0x0301u));
        for(unsigned i=0;i<32u;i++) assert(storage[i]==0xa5u && storage[544u+i]==0xa5u);
        unsigned reads=hw.reads,writes=hw.writes;
        assert(!bus->transfer_block(NULL,NULL,storage+32u,512u,false,&crc));
        assert(hw.reads==reads && hw.writes==writes);
        kui_sci_sd_release();
        hw.dmaor=0x0301u;restored();
    }
}

int main(void) {
    const struct kui_loader_sd_bus *bus=kui_sci_sd_bus();assert(bus);
    delayed_block(bus,0u);
    delayed_block(bus,8u);
    delayed_block(bus,32u);
    receive_credit_does_not_replace_tdre(bus,false);
    receive_credit_does_not_replace_tdre(bus,true);
    /* An owned, completed-unacknowledged or IRQ-pending DMA1 always keeps
     * the existing programmed fallback, with its tuple and priority intact. */
    test_dma_unavailable(bus);
    terminal_failures(bus);
    printf("SCI RX arbitration: paced=%u pio=%u; delayed receive, exact CRC, ownership and terminal failures passed\n",
           (unsigned)EXPECT_NATIVE_PACED,(unsigned)EXPECT_NATIVE_PIO);
    return 0;
}
