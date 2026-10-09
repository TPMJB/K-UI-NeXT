/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the accepted independent SCI byte/DMAC model without changing it.
 * The comparison adds an MMIO ledger and runs only the affected read paths. */
#define main sci_bus_original_matrix_main
#define kui_sci_sd_test_read comparison_model_read
#define kui_sci_sd_test_write comparison_model_write
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type" /* Renamed main loses implicit return0. */
#include "test_sci_sd_bus.c"
#pragma GCC diagnostic pop
#undef main
#undef kui_sci_sd_test_read
#undef kui_sci_sd_test_write

static unsigned channel_reads,channel_writes;
uint32_t kui_sci_sd_test_read(uint32_t address,unsigned width) {
    if((address>=SAR1 && address<=CHCR1) || address==DMAOR) channel_reads++;
    return comparison_model_read(address,width);
}
void kui_sci_sd_test_write(uint32_t address,uint32_t value,unsigned width) {
    if((address>=SAR1 && address<=CHCR1) || address==DMAOR) channel_writes++;
    comparison_model_write(address,value,width);
}
static void read_case(const struct kui_loader_sd_bus *bus,unsigned latency,bool occupied) {
    _Alignas(32) uint8_t guarded[576];
    uint8_t *buffer=guarded+32u;
    reset();assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
    hw.patterned_rx=true;hw.dma_available=true;hw.byte_polls=latency;
    if(occupied) hw.chcr=0x4001u;
    uint32_t chcr=hw.chcr, before=bus->ticks(NULL);
    channel_reads=channel_writes=0;
    memset(guarded,0x5a,sizeof(guarded));
    uint16_t crc=0xa55au;
    assert(bus->transfer_block(NULL,NULL,buffer,512u,false,&crc));
    assert(crc==crc16_reference(buffer,512u));
    assert(kui_sci_sd_healthy() && hw.bytes==512u);
    assert(bus->ticks(NULL)-before==4096u);
    for(unsigned i=0;i<512u;i++) assert(buffer[i]==received_byte(i));
    for(unsigned i=0;i<32u;i++) assert(guarded[i]==0x5au && guarded[i+544u]==0x5au);
#if KUI_SCI_SD_PIO_ONLY
    assert(!channel_reads && !channel_writes && !hw.dma_starts && !hw.cache_purges);
    assert(!hw.irq_disables && !hw.irq_restores && hw.received==512u);
#else
    assert(channel_reads);
    if(occupied) assert(!channel_writes && !hw.dma_starts && !hw.cache_purges);
    else assert(channel_writes && hw.dma_starts==1u && hw.dma_bytes==512u);
#endif
    assert(hw.chcr==chcr && hw.sar==0x0c002000u && hw.dar==0x0c004000u && hw.tcr==7u);
    assert(bus->transfer(NULL,0x40u,false)==received_byte(512u));
    assert(hw.sent[512u]==reversed(0x40u));
    kui_sci_sd_release();
    hw.chcr=0x4000u;restored();
}
static void pio_fault_case(const struct kui_loader_sd_bus *bus,bool overrun) {
    _Alignas(32) uint8_t guarded[576];
    reset();assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
    hw.patterned_rx=true;hw.dma_available=true;hw.chcr=0x4001u;
    hw.fault_byte=137u;hw.overrun=overrun;hw.timeout=!overrun;
    channel_reads=channel_writes=0;
    memset(guarded,0x5a,sizeof(guarded));
    uint16_t crc=0xa55au;
    assert(!bus->transfer_block(NULL,NULL,guarded+32u,512u,false,&crc));
    assert(crc==0xa55au && !kui_sci_sd_healthy() && hw.scr==0u);
    assert(hw.bytes==137u && hw.polls<12000u && !channel_writes && !hw.dma_starts);
#if KUI_SCI_SD_PIO_ONLY
    assert(!channel_reads);
#endif
    for(unsigned i=0;i<136u;i++) assert(guarded[32u+i]==received_byte(i));
    for(unsigned i=168u;i<sizeof(guarded);i++) assert(guarded[i]==0x5au);
    for(unsigned i=0;i<32u;i++) assert(guarded[i]==0x5au);
    unsigned reads=hw.reads,writes=hw.writes;
    assert(bus->transfer(NULL,0xffu,false)==0xffu);
    assert(!bus->transfer_block(NULL,NULL,guarded+32u,512u,false,&crc));
    assert(hw.reads==reads && hw.writes==writes && crc==0xa55au);
    kui_sci_sd_release();hw.chcr=0x4000u;restored();
}
int main(void) {
    const struct kui_loader_sd_bus *bus=kui_sci_sd_bus();
    assert(bus && bus->transfer_block && bus->ticks && bus->end);
    const unsigned latencies[]={1u,7u,97u};
    for(unsigned i=0;i<3u;i++) for(unsigned occupied=0;occupied<2u;occupied++)
        read_case(bus,latencies[i],occupied!=0u);
    pio_fault_case(bus,false);pio_fault_case(bus,true);
#if !KUI_SCI_SD_PIO_ONLY
    test_dma_failures(bus);
#endif
    puts("SCI read comparison: CRC, guarded payloads, bounded faults, MMIO exclusion and register restoration PASS");
    return 0;
}
