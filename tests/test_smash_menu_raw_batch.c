/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual foreground/card implementation; authored card and SCI registers.
 * Keep the existing one-block fixtures as ABI-adapted regression coverage. */
#include "retail_async.h"
#define kui_vc2_ce_vmu_read_card(lba,out) kui_vc2_ce_vmu_read_card(lba,out,1u)
#include KUI_SMASH_FOREGROUND_FIXTURE
#undef kui_vc2_ce_vmu_read_card

bool kui_smash_menu_raw_test_redirect(uint32_t address,uint32_t bytes) {
    (void)address;(void)bytes;return true;
}

static struct {uint8_t before[32],bytes[512u*456u],after[32];} batch;
static unsigned raw_exact_runs,raw_dma_blocks,raw_crc_refusals;
static void guards(void) {
    for(unsigned i=0;i<32u;i++)CHECK(batch.before[i]==0xa5u && batch.after[i]==0xa5u);
}
static void exact_run(unsigned count) {
    late_card_setup();memset(&batch,0xa5,sizeof(batch));
    const struct kui_retail_gd service=R.shared.service;
    const struct kui_retail_async engine=R.engine;
    unsigned acquired=hw.acquires,released=bus_releases;
    unsigned started=card.cmd18,stopped=card.cmd12;
    unsigned dma=m.dma_starts,polled=R.shared.stream.stats.polled;
    CHECK(kui_vc2_ce_vmu_read_card(100u,batch.bytes,count)==KUI_SCI_STREAM_OK);
    CHECK(!memcmp(batch.bytes,image_card+100u*512u,count*512u));
    for(unsigned i=count*512u;i<sizeof(batch.bytes);i++)CHECK(batch.bytes[i]==0xa5u);
    guards();CHECK(!memcmp(&service,&R.shared.service,sizeof(service)));
    CHECK(!memcmp(&engine,&R.engine,sizeof(engine)));
    CHECK(hw.acquires==acquired+1u && bus_releases==released+1u &&
        card.cmd18==started+1u && card.cmd12==stopped+1u);
    CHECK(m.dma_starts==dma+count && R.shared.stream.stats.polled==polled);
    ++raw_exact_runs;raw_dma_blocks+=count;
    quiescent(GAME_VBR,0x5a9fu);
}
static void bounds(void) {
    for(unsigned how=0;how<7u;how++) {
        late_card_setup();memset(&batch,0xa5,sizeof(batch));
        uint32_t lba=100u,count=4u;uint8_t *out=batch.bytes;
        if(how==0u)count=0u;
        if(how==1u)count=4097u;
        if(how==2u){lba=1999u;count=2u;}
        if(how==3u){manifest.card_sectors=101u;count=2u;}
        if(how==4u)out=(uint8_t *)(UINTPTR_MAX-255u);
        if(how==5u){lba=UINT32_MAX-1u;manifest.partition_start=UINT32_MAX-2u;
            manifest.partition_end=UINT64_C(0x100000000);manifest.card_sectors=manifest.partition_end;}
        if(how==6u)out=NULL;
        unsigned acquired=hw.acquires,clocks=card.clocks,released=bus_releases;
        CHECK(kui_vc2_ce_vmu_read_card(lba,out,count)==KUI_SCI_STREAM_BUSY);
        CHECK(hw.acquires==acquired && card.clocks==clocks && bus_releases==released);
        for(unsigned i=0;i<sizeof(batch);i++)CHECK(((uint8_t *)&batch)[i]==0xa5u);
        quiescent(GAME_VBR,0x5a9fu);
    }
}
static void failed_block(void) {
  for(unsigned bad=0u;bad<3u;bad++) {
    late_card_setup();memset(&batch,0xa5,sizeof(batch));
    unsigned offset=(unsigned[]){0u,2u,3u}[bad];
    card.corrupt_lba=100u+offset;card.corrupt_count=1u;
    unsigned acquired=hw.acquires,released=bus_releases,started=card.cmd18,stopped=card.cmd12;
    unsigned dma=m.dma_starts,polled=R.shared.stream.stats.polled;
    CHECK(kui_vc2_ce_vmu_read_card(100u,batch.bytes,4u)==KUI_SCI_STREAM_CRC);
    CHECK(!memcmp(batch.bytes,image_card+100u*512u,offset*512u));
    for(unsigned i=offset*512u;i<sizeof(batch.bytes);i++)CHECK(batch.bytes[i]==0xa5u);
    CHECK(hw.acquires==acquired+1u && bus_releases==released+1u &&
        card.cmd18==started+1u && card.cmd12==stopped+1u);
    CHECK(m.dma_starts==dma+offset+1u && R.shared.stream.stats.polled==polled);
    ++raw_crc_refusals;
    guards();quiescent(GAME_VBR,0x5a9fu);
  }
}
static void failed_stop(void) {
    late_card_setup();memset(&batch,0xa5,sizeof(batch));inject_stop_failure=true;
    unsigned acquired=hw.acquires,released=bus_releases;
    CHECK(kui_vc2_ce_vmu_read_card(100u,batch.bytes,4u)==KUI_SCI_STREAM_RESET);
    CHECK(!memcmp(batch.bytes,image_card+100u*512u,2048u));guards();
    CHECK(R.shared.port.fault && R.shared.port.acquired && bus_owned && R.engine.opened);
    CHECK(hw.acquires==acquired+1u && bus_releases==released);
    const kui_retail_async_region_type quarantined=R;
    unsigned clocks=card.clocks;acquired=hw.acquires;
    CHECK(kui_vc2_ce_vmu_read_card(104u,batch.bytes+2048u,2u)==KUI_SCI_STREAM_RESET);
    CHECK(!memcmp(&quarantined,&R,sizeof(R)) && clocks==card.clocks && acquired==hw.acquires);
    for(unsigned i=2048u;i<sizeof(batch.bytes);i++)CHECK(batch.bytes[i]==0xa5u);
}
static void dma_faults(void) {
    unsigned refused=0u;
    for(unsigned how=0u;how<5u;how++) {
        late_card_setup();memset(&batch,0xa5,sizeof(batch));
        const uint32_t saved[4]={m.sar,m.dar,m.tcr,m.chcr};
        unsigned dma=m.dma_starts,polled=R.shared.stream.stats.polled;
        if(how<3u)m.overrun_after=(unsigned[]){1u,255u,508u}[how];
        if(how==3u)m.stall=true;
        if(how==4u)m.foreign_during_rx=true;
        CHECK(kui_vc2_ce_vmu_read_card(100u,batch.bytes,4u)==KUI_SCI_STREAM_RESET);
        CHECK(R.shared.port.fault && !R.shared.port.acquired && !bus_owned);
        CHECK(m.dma_starts==dma+1u && R.shared.stream.stats.polled==polled);
        for(unsigned i=0u;i<sizeof(batch);i++)CHECK(((uint8_t *)&batch)[i]==0xa5u);
        if(how!=4u)CHECK(m.sar==saved[0] && m.dar==saved[1] && m.tcr==saved[2] && m.chcr==saved[3]);
        else CHECK(m.sar==UINT32_C(0x0c200000) && m.chcr==m.armed_chcr && m.tcr==513u);
        quiescent(GAME_VBR,0x5a9fu);
        unsigned clocks=card.clocks,acquired=hw.acquires;
        CHECK(kui_vc2_ce_vmu_read_card(104u,batch.bytes,1u)==KUI_SCI_STREAM_RESET);
        CHECK(card.clocks==clocks && hw.acquires==acquired);++refused;
    }
    /* No channel was borrowed in this case: retain the existing initial
     * fallback without enabling DMAOR or changing another owner's policy. */
    for(unsigned how=0u;how<2u;how++) {
        late_card_setup();memset(&batch,0xa5,sizeof(batch));
        if(!how)m.dmaor=0u;
        else {m.sar=UINT32_C(0x0c200000);m.chcr|=1u;}
        const uint32_t saved[5]={m.sar,m.dar,m.tcr,m.chcr,m.dmaor};
        unsigned dma=m.dma_starts,polled=R.shared.stream.stats.polled;
        CHECK(kui_vc2_ce_vmu_read_card(100u,batch.bytes,4u)==KUI_SCI_STREAM_OK);
        CHECK(!memcmp(batch.bytes,image_card+100u*512u,2048u));guards();
        CHECK(m.sar==saved[0] && m.dar==saved[1] && m.tcr==saved[2] &&
            m.chcr==saved[3] && m.dmaor==saved[4]);
        CHECK(m.dma_starts==dma && R.shared.stream.stats.polled==polled+4u);
        quiescent(GAME_VBR,0x5a9fu);
    }
    printf("SMASH_RAW_DMA_FAULT_JSON {\"post_start_refusals\":%u,\"pre_start_pio_cases\":2,\"pre_start_pio_blocks\":8,\"hardware_executed\":false}\n",refused);
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--dma-fault")){dma_faults();return 0;}
    if(argc==2 && !strcmp(argv[1],"--stop")){failed_stop();puts("Smash raw batch stop quarantine: OK");return 0;}
    exact_run(1u);exact_run(2u);exact_run(31u);exact_run(456u);
    bounds();failed_block();
    /* The original suite's final stop-failure case deliberately quarantines
     * its bus, so run that terminal regression after the fresh batch cases. */
    char *base_argv[]={argv[0],NULL};CHECK(!original_foreground_main(1,base_argv));
    printf("SMASH_RAW_DMA_JSON {\"exact_runs\":%u,\"dma_blocks\":%u,\"crc_refusals\":%u,\"hardware_executed\":false}\n",
        raw_exact_runs,raw_dma_blocks,raw_crc_refusals);
    puts("Smash raw batch exact runs, bounds and CRC16 failure: OK");return 0;
}
