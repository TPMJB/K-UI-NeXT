/* SPDX-License-Identifier: GPL-3.0-only */
/* The production raw entry with explicit storage/ownership boundary stubs.
 * SD wire CRC16 itself belongs to the separate real-card protocol models. */
#include "kui/retail_image.h"
#include "kui/retail_gd.h"
#include "retail_storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct kui_retail_manifest manifest;
static struct kui_retail_gd service;
static struct kui_retail_storage card;
static uint32_t kui_retail_hook_active;
#define RETAIL_MANIFEST (&manifest)
static uint32_t sr;
static bool owned,fault,stop_failure,release_failure;
static enum kui_loader_sd_result acquire_result;
static unsigned acquires,reads,stops,releases;
static uint32_t first,remaining,failing;
static uint8_t output[8u*512u];
uint32_t kui_smash_return_raw_test_sr(void) {return sr;}
bool kui_sci_sd_faulted(void) {return fault;}
bool kui_sci_sd_healthy(void) {return owned && !fault;}
enum kui_loader_sd_result kui_retail_storage_acquire(struct kui_retail_storage *s) {
    assert(s==&card && kui_retail_hook_active && !owned);++acquires;
    if(!acquire_result)owned=true;
    return acquire_result;
}
enum kui_loader_sd_result kui_retail_storage_read_run(struct kui_retail_storage *s,
    uint32_t lba,uint32_t available,uint8_t out[512]) {
    assert(s==&card && owned && kui_retail_hook_active && remaining);
    assert(lba==first+reads && available==remaining--);++reads;
    memset(out,(int)(lba&255u),512u);
    return lba==failing?KUI_LOADER_SD_CRC:KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_retail_storage_stop(struct kui_retail_storage *s) {
    assert(s==&card && owned && kui_retail_hook_active);++stops;
    return stop_failure?KUI_LOADER_SD_COMMAND:KUI_LOADER_SD_OK;
}
void kui_retail_storage_release(struct kui_retail_storage *s) {
    assert(s==&card && owned && kui_retail_hook_active);++releases;owned=false;
    if(release_failure)fault=true;
}
#define KUI_SMASH_RETURN_RAW_HOST_TEST 1
#include "smash_menu_return_raw.inc"
static void reset(void) {
    memset(&manifest,0,sizeof(manifest));memset(&service,0,sizeof(service));
    memset(&card,0,sizeof(card));memset(output,0xa5,sizeof(output));
    manifest.partition_start=100u;manifest.partition_end=200u;manifest.card_sectors=300u;
    service.initialized=1u;card.device.sd.ready=true;card.device.sd.blocks=300u;
    sr=0x700000f0u;owned=fault=stop_failure=release_failure=false;
    kui_retail_hook_active=0u;acquire_result=KUI_LOADER_SD_OK;
    acquires=reads=stops=releases=0;first=100u;remaining=4u;failing=UINT32_MAX;
}
static void unchanged(void) {
    for(unsigned i=0;i<sizeof(output);i++)assert(output[i]==0xa5u);
    assert(!acquires && !reads && !stops && !releases);
}
int main(void) {
    reset();assert(kui_smash_menu_return_read_card(first,output,4u)==KUI_LOADER_SD_OK);
    assert(acquires==1u && reads==4u && stops==1u && releases==1u && !owned && !kui_retail_hook_active);
    for(unsigned i=0;i<sizeof(output);i++)assert(output[i]==(i<2048u?(uint8_t)(100u+i/512u):0xa5u));
    for(unsigned how=0;how<17u;how++) {
        reset();uint32_t lba=100u,count=4u;uint8_t *out=output;
        if(how==0u)sr&=~0x10u;
        if(how==1u)kui_retail_hook_active=1u;
        if(how==2u)service.initialized=0u;
        if(how==3u)service.pending=1u;
        if(how==4u)service.executing=1u;
        if(how==5u)card.stream.active=true;
        if(how==6u)card.device.sd.ready=false;
        if(how==7u)card.device.sd.slow=true;
        if(how==8u)card.device.sd.blocks=0u;
        if(how==9u)fault=true;
        if(how==10u)out=NULL;
        if(how==11u)count=0u;
        if(how==12u)count=4097u;
        if(how==13u)out=(uint8_t *)(UINTPTR_MAX-255u);
        if(how==14u)lba=99u;
        if(how==15u){lba=199u;count=2u;}
        if(how==16u)card.device.sd.blocks=101u;
        assert(kui_smash_menu_return_read_card(lba,out,count)==KUI_LOADER_SD_NOT_READY);unchanged();
    }
    reset();failing=102u;
    assert(kui_smash_menu_return_read_card(first,output,4u)==KUI_LOADER_SD_CRC);
    assert(reads==3u && stops==1u && releases==1u && !kui_retail_hook_active && !owned);
    for(unsigned i=1536u;i<sizeof(output);i++)assert(output[i]==0xa5u);
    for(unsigned how=0;how<3u;how++) {
        reset();if(how==0u)stop_failure=true;if(how==1u)release_failure=true;
        if(how==2u)acquire_result=KUI_LOADER_SD_TIMEOUT;
        assert(kui_smash_menu_return_read_card(first,output,4u)==KUI_LOADER_SD_TIMEOUT);
        assert(!card.device.sd.ready && kui_retail_hook_active);
        unsigned before=acquires;assert(kui_smash_menu_return_read_card(first,output,4u)==KUI_LOADER_SD_NOT_READY);
        assert(acquires==before);
    }
    reset();acquire_result=KUI_LOADER_SD_UNSUPPORTED;
    assert(kui_smash_menu_return_read_card(first,output,4u)==KUI_LOADER_SD_UNSUPPORTED);
    assert(card.device.sd.ready && !kui_retail_hook_active && !owned && acquires==1u && !reads);
    puts("Smash native raw run: exact count, refusal before IO, CRC refusal and sticky quarantine passed");
}
