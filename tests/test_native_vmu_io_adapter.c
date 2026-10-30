/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the extracted actual resident read/capture functions. Protocol and
 * hardware boundaries are modeled to isolate diagnostic retention/order. */
#include "native_vmu_io_diagnostic.h"
#include "retail_storage.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static struct kui_retail_gd service;
static struct kui_retail_storage card;
static struct kui_retail_image image;
static enum kui_loader_sd_result card_result;
static enum kui_loader_sd_result acquire_result,read_result,stop_result;
static enum kui_game_result image_attempt;
static unsigned acquires,reads,stops,releases,checks;
static bool fault;
static enum kui_game_sector_format sector_format(uint32_t bytes) {
    return bytes==2352u?KUI_GAME_SECTOR_RAW:KUI_GAME_SECTOR_MODE1;
}
enum kui_loader_sd_result kui_retail_storage_acquire(struct kui_retail_storage *s) {
    assert(s==&card);++acquires;return acquire_result;
}
enum kui_loader_sd_result kui_retail_storage_stop(struct kui_retail_storage *s) {
    assert(s==&card);++stops;return stop_result;
}
void kui_retail_storage_release(struct kui_retail_storage *s) {assert(s==&card);++releases;}
enum kui_game_result kui_retail_image_read(struct kui_retail_image *in,uint32_t lba,uint32_t count,
    enum kui_game_sector_format format,void *out,size_t capacity) {
    assert(in==&image && lba==45002u && count==2u && format==KUI_GAME_SECTOR_MODE1);
    assert(out==&card && capacity==4096u);++reads;card_result=read_result;
    memset(image.block,0x53,sizeof(image.block));return image_attempt;
}
static bool kui_native_vmu_transport_faulted(uint32_t transport) {assert(transport==1u);return fault;}

#include "native_vmu_io_adapter_under_test.h"

static void fresh(void) {
    memset(&service,0,sizeof(service));memset(&card,0,sizeof(card));memset(&image,0xa5,sizeof(image));
    service.diag.last_command=KUI_GD_DMAREAD;service.diag.read_flags=KUI_GAME_MODE;
    service.lba=45000u;service.count=20u;service.destination=0x8c123400u;service.completed_bytes=4096u;
    card.transport=1u;card.device.sd.ready=true;card.device.sd.last_command=18u;card.device.sd.last_response=0x0bu;
    acquire_result=read_result=stop_result=KUI_LOADER_SD_OK;image_attempt=KUI_GAME_OK;
    acquires=reads=stops=releases=0u;fault=false;
}
static void captured(uint32_t expected_image,uint32_t expected_sd) {
    service.error=KUI_GD_ERROR_IO;
    struct kui_retail_image before=image;
    struct kui_retail_gd gd_before=service;struct kui_retail_storage storage_before=card;
    /* The second argument would be an invalid owner pointer if dereferenced.
     * The compact capture uses resident data only and ignores it completely. */
    capture_native_fault(KUI_GD_EXEC,UINT32_MAX);
    const kui_native_vmu_io_word *words=(const void *)image.block;
    assert(words[0]==2u && words[1]==0x11u && words[2]==expected_sd && words[3]==expected_image);
    assert(words[4]==(0x000b1200u|(card.device.sd.ready?1u:0u)|(fault?8u:0u)));
    assert(words[5]==45000u && words[6]==20u && words[7]==0x8c123400u && words[8]==4096u);
    size_t at=offsetof(struct kui_retail_image,block),bytes=9u*4u;
    assert(!memcmp(&image,&before,at));
    assert(!memcmp((const uint8_t *)&image+at+bytes,(const uint8_t *)&before+at+bytes,sizeof(image)-at-bytes));
    assert(!memcmp(&service,&gd_before,sizeof(service)) && !memcmp(&card,&storage_before,sizeof(card)));
    ++checks;
}
int main(void) {
    const enum kui_loader_sd_result denied[]={KUI_LOADER_SD_NOT_READY,KUI_LOADER_SD_UNSUPPORTED,KUI_LOADER_SD_TIMEOUT};
    for(unsigned i=0u;i<3u;i++) {
        fresh();acquire_result=denied[i];
        assert(read_sectors(NULL,45002u,2u,2048u,&card)==-1);
        assert(acquires==1u && !reads && !stops && !releases);
        captured(UINT32_MAX,denied[i]);
    }
    const enum kui_loader_sd_result failures[]={KUI_LOADER_SD_CRC,KUI_LOADER_SD_TOKEN,KUI_LOADER_SD_TIMEOUT};
    for(unsigned i=0u;i<3u;i++) {
        fresh();read_result=failures[i];image_attempt=KUI_GAME_IO;stop_result=KUI_LOADER_SD_TIMEOUT;
        assert(read_sectors(NULL,45002u,2u,2048u,&card)==-1);
        assert(acquires==1u && reads==1u && stops==1u && releases==1u && !image.cache_valid);
        captured(KUI_GAME_IO,failures[i]); /* Stop must not hide earlier CRC/token failure. */
    }
    fresh();image_attempt=KUI_GAME_MODE;
    assert(read_sectors(NULL,45002u,2u,2048u,&card)==-1);
    assert(acquires==1u && reads==1u && stops==1u && releases==1u);
    captured(KUI_GAME_MODE,KUI_LOADER_SD_OK);
    fresh();stop_result=KUI_LOADER_SD_TIMEOUT;
    assert(read_sectors(NULL,45002u,2u,2048u,&card)==-1 && !image.cache_valid);
    assert(acquires==1u && reads==1u && stops==1u && releases==1u);
    captured(KUI_GAME_OK,KUI_LOADER_SD_TIMEOUT);
    /* An earlier VMU/bus failure can poison the reader's shared descriptor.
     * Report that state explicitly without inventing a fresh wire timeout. */
    fresh();card.device.sd.ready=false;fault=true;
    read_result=KUI_LOADER_SD_NOT_READY;image_attempt=KUI_GAME_IO;
    assert(read_sectors(NULL,45002u,2u,2048u,&card)==-1);
    captured(KUI_GAME_IO,KUI_LOADER_SD_NOT_READY);
    fresh();assert(!read_sectors(NULL,45002u,2u,2048u,&card));
    assert(acquires==1u && reads==1u && stops==1u && releases==1u);
    assert(service.diag.read_flags==KUI_GAME_OK);++checks;
    fresh();assert(read_sectors(NULL,45002u,2u,512u,&card)==-1);
    assert(!acquires && !reads && !stops && !releases && service.diag.read_flags==UINT32_MAX);++checks;
    printf("VMU I/O resident adapter: %u cases; exact SD/image result, no stale attempt, first error preserved, one acquire/read/stop/release and bounded terminal writes\n",checks);
    return 0;
}
