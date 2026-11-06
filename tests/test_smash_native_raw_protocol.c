/* SPDX-License-Identifier: GPL-3.0-only */
/* Real native raw gate -> storage -> CMD18/CMD12 -> SCI/DMAC C. Only MMIO,
 * cache operations and CPU status are authored fixtures. No raw read or
 * storage callback is replaced. The existing independent card/SCI oracles
 * are combined by the runner at their byte-wire boundary. */
#include "kui/retail_image.h"
#include "kui/retail_gd.h"
#include "retail_storage.h"
#include "native_return_hardware.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t kui_smash_native_raw_wire(uint8_t byte);
#define card spi_card
#include KUI_SMASH_NATIVE_RAW_CARD_FIXTURE
#undef card
#define main retained_sci_fixture_main
#define kui_sci_sd_test_read retained_sci_read
#define kui_sci_sd_test_write retained_sci_write
#include KUI_SMASH_NATIVE_RAW_SCI_FIXTURE
#undef kui_sci_sd_test_read
#undef kui_sci_sd_test_write
#undef main

static unsigned standby_reads,hardware_quiesces;
static bool cache_disabled;
static uint8_t kui_smash_native_raw_wire(uint8_t byte) {
    return hw.pdtr&0x80u?0xffu:card_clock(byte);
}
uint32_t kui_sci_sd_test_read(uint32_t address,unsigned width) {
    /* Renesas9.6 says retained registers survive module standby. Count this
     * ordering, but do not invent an invalid-register access or bus hang. */
    if((hw.stb&1u) && address>=SMR && address<=SPTR)++standby_reads;
    return retained_sci_read(address,width);
}
void kui_sci_sd_test_write(uint32_t address,uint32_t value,unsigned width) {
    retained_sci_write(address,value,width);
}
uint32_t kui_native_return_hw_test_sr(void) {return UINT32_C(0x700000f0);}
uint32_t kui_native_return_hw_test_physical(const void *p) {(void)p;assert(!"controller unused");return 0u;}
void kui_native_return_hw_test_cache_off(void) {cache_disabled=true;++hardware_quiesces;}
uint32_t kui_native_return_hw_test_read(uint32_t address) {
    if(address>=UINT32_C(0xffa0000c) && address<=UINT32_C(0xffa0003c)) {
        if(address==CHCR1)return hw.chcr;
        return 0u;
    }
    return 0u;
}
void kui_native_return_hw_test_write(uint32_t address,uint32_t value) {
    if(address==CHCR1) {hw.chcr=value;return;}
    (void)value;
}
static struct kui_retail_manifest manifest;
static struct kui_retail_gd service;
static struct kui_retail_storage card;
static uint32_t kui_retail_hook_active;
#define RETAIL_MANIFEST (&manifest)
uint32_t kui_smash_return_raw_test_sr(void) {return UINT32_C(0x700000f0);}
#define KUI_SMASH_RETURN_RAW_HOST_TEST 1
#include "smash_menu_return_raw.inc"
static _Alignas(32) uint8_t output[256u*512u];
static void fresh(bool dma,bool quiesce,bool standby) {
    reset();memset(&spi_card,0,sizeof(spi_card));
    spi_card.nac=1u;spi_card.nac_first=3u;spi_card.busy=6u;
    spi_card.blocks=100000u;spi_card.high_capacity=true;
    memset(&manifest,0,sizeof(manifest));memset(&service,0,sizeof(service));memset(&card,0,sizeof(card));
    manifest.partition_start=100u;manifest.partition_end=90000u;manifest.card_sectors=100000u;
    service.initialized=1u;card.transport=KUI_STORAGE_SCI;card.device.sd.bus=*kui_sci_sd_bus();
    card.device.sd.ready=true;card.device.sd.high_capacity=true;card.device.sd.blocks=100000u;
    /* PIO cases retain an unacknowledged TE flag: the real DMA borrower must
     * refuse that channel before touching it, then use its bounded fallback. */
    kui_retail_hook_active=0u;hw.dma_available=true;hw.chcr=dma?0u:2u;
    if(!standby)hw.stb&=(uint8_t)~1u;
    standby_reads=0u;cache_disabled=false;hardware_quiesces=0u;memset(output,0xa5,sizeof(output));
    if(quiesce)assert(kui_native_return_quiesce() && cache_disabled && hardware_quiesces==1u);
}
static void expected(uint32_t first,uint32_t blocks) {
    for(uint32_t block=0u;block<blocks;block++)for(unsigned i=0u;i<512u;i++)
        assert(output[block*512u+i]==data_at(first+block,i));
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--dma-fault")) {
        fresh(true,true,true);hw.dma_stall=true;
        assert(kui_smash_menu_return_read_card(100u,output,1u)==KUI_LOADER_SD_TIMEOUT);
        assert(!card.device.sd.ready && kui_retail_hook_active);
        unsigned bytes=hw.bytes;
        assert(kui_smash_menu_return_read_card(100u,output,1u)==KUI_LOADER_SD_NOT_READY && hw.bytes==bytes);
        puts("NATIVE_RAW_PROTOCOL_FAULT_JSON {\"dma_fault_quarantine\":true,\"no_retry\":true}");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--crc")) {
        fresh(true,true,true);spi_card.corrupt_lba=102u;spi_card.corrupt_count=1u;
        assert(kui_smash_menu_return_read_card(100u,output,4u)==KUI_LOADER_SD_CRC);
        assert(spi_card.cmd18==1u && spi_card.cmd12==1u && !spi_card.streaming && !kui_retail_hook_active);
        for(unsigned i=3u*512u;i<sizeof(output);i++)assert(output[i]==0xa5u);
        puts("NATIVE_RAW_PROTOCOL_CRC_JSON {\"crc_corruption_refused\":true,\"healthy_stop\":true}");return 0;
    }
    assert(argc==1);unsigned cases=0u,standby_cases=0u,dma_blocks=0u;
    for(unsigned dma=0u;dma<2u;dma++)for(unsigned quiesce=0u;quiesce<2u;quiesce++)
        for(unsigned standby=0u;standby<2u;standby++) {
            fresh(dma!=0u,quiesce!=0u,standby!=0u);
            assert(kui_smash_menu_return_read_card(100u,output,1u)==KUI_LOADER_SD_OK);expected(100u,1u);
            unsigned before=spi_card.cmd18;
            assert(kui_smash_menu_return_read_card(101u,output,197u)==KUI_LOADER_SD_OK);expected(101u,197u);
            assert(spi_card.cmd18-before==4u && spi_card.cmd18==5u && spi_card.cmd12==5u);
            assert(!card.stream.active && card.device.sd.ready && !kui_retail_hook_active &&
                !kui_sci_sd_faulted() && !spi_card.streaming && !hw.active && !hw.queued);
            assert((dma!=0u)==(hw.dma_starts!=0u));dma_blocks+=hw.dma_starts;
            if(!dma)assert(hw.chcr==2u && !hw.dma_writes);
            if(standby) {assert(standby_reads>=2u);++standby_cases;}
            else assert(!standby_reads);
            assert(cache_disabled==(quiesce!=0u));++cases;
        }
    printf("NATIVE_RAW_PROTOCOL_JSON {\"success_matrix_cases\":%u,\"standby_cases\":%u,\"dma_blocks\":%u,\"probe_blocks\":1,\"bulk_blocks\":197,\"bulk_cmd18_runs\":4,\"cache_behavior_modeled\":false}\n",cases,standby_cases,dma_blocks);
    return 0;
}
