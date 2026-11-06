/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual launch boundary over real FatFs. Count the device callbacks, not
 * f_read calls, and keep the historical write-cut fixture available. */
#define kui_media_set history_observe_media
#define KUI_VMU_HISTORY_TEST_MAIN history_reference_main
#include "test_vmu_history.c"
#undef kui_media_set
#include "kui/hash.h"

void kui_media_set(const struct kui_media_ops *);
static struct kui_media_ops observed_ops;
static bool observing;
static uint64_t io_reads,io_read_sectors,io_writes,io_write_sectors,io_syncs;
static uint64_t io_single_reads,io_multi_reads;
static uint64_t allocation_calls,allocation_reads,allocation_read_sectors;
int __real_kui_vmu_library_check_allocation(kui_cancel_fn);
int __wrap_kui_vmu_library_check_allocation(kui_cancel_fn cancel) {
    uint64_t before=io_reads,sectors=io_read_sectors;bool active=observing;
    int result=__real_kui_vmu_library_check_allocation(cancel);
    if(active){++allocation_calls;allocation_reads+=io_reads-before;allocation_read_sectors+=io_read_sectors-sectors;}
    return result;
}
static int observed_read(void *context,uint32_t lba,size_t count,uint8_t *out) {
    if(observing){++io_reads;io_read_sectors+=count;if(count==1u)++io_single_reads;else ++io_multi_reads;}
    return read_media(context,lba,count,out);
}
static int observed_write(void *context,uint32_t lba,size_t count,const uint8_t *data) {
    if(observing){++io_writes;io_write_sectors+=count;}
    return write_media(context,lba,count,data);
}
static int observed_sync(void *context) {
    if(observing)++io_syncs;
    return sync_media(context);
}
void history_observe_media(const struct kui_media_ops *ops) {
    observed_ops=*ops;observed_ops.read=observed_read;observed_ops.write=observed_write;
    observed_ops.sync=observed_sync;kui_media_set(&observed_ops);
}
static void watch_image(const char *path) {
    FIL file;DWORD map[8]={8u};
    CHECK(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)==KUI_VMU_IMAGE_BYTES);
    file.cltbl=map;CHECK(f_lseek(&file,CREATE_LINKMAP)==FR_OK);
    CHECK(map[0]==4u && map[1]*fs.csize>=256u);
    payload_first=(uint64_t)volume.start+fs.database+(uint64_t)(map[2]-2u)*fs.csize;
    payload_end=payload_first+256u;file.cltbl=NULL;CHECK(f_close(&file)==FR_OK);
    payload_single_reads=payload_multi_reads=payload_sectors=0u;
}
static void launch_trial(BYTE kind,DWORD cluster,bool changed) {
    format(kind,cluster);uint8_t wire[4096];unsigned pair=create_game("MK-51000",wire);
    struct kui_vmu_persist_profile profile;CHECK(!kui_vmu_persist_profile_decode(&profile,wire,sizeof(wire)));
    struct kui_vmu_history_request request={.action=KUI_VMU_HISTORY_CHECKPOINT};
    CHECK(kui_vmu_game_card_key("MK-51000",request.card.product));request.card.pair=pair;
    memcpy(request.card.identity,profile.image_id,16u);request.card.selected=true;
    CHECK(history_storage(&volume,&request.card.storage));
    char source[96],path[192];pair_path(source,pair,"card.vmd");
    uint8_t *image=malloc(KUI_VMU_IMAGE_BYTES);CHECK(image);
    CHECK(file_read(source,image,KUI_VMU_IMAGE_BYTES)==KUI_VMU_IMAGE_BYTES);
    struct kui_vmu_history_view view;
    for(unsigned i=1u;i<=KUI_VMU_HISTORY_KEEP;++i) {
        image[100u*512u]=(uint8_t)i;file_write(source,image,KUI_VMU_IMAGE_BYTES);
        CHECK(!operation(&request,&view));
    }
    CHECK(view.count==KUI_VMU_HISTORY_KEEP);uint64_t previous=view.entries[0].sequence;
    CHECK(!session_begin(&request.card,&view));
    if(changed) {image[100u*512u]=99u;file_write(source,image,KUI_VMU_IMAGE_BYTES);}
    history_path(path,previous,"card.vmd");watch_image(path);
    io_reads=io_read_sectors=io_writes=io_write_sectors=io_syncs=io_single_reads=io_multi_reads=0u;
    allocation_calls=allocation_reads=allocation_read_sectors=0u;observing=true;
    CHECK(!session_begin(&request.card,&view));observing=false;
    CHECK(view.known && view.count==KUI_VMU_HISTORY_KEEP && view.entries[0].sequence==previous+(changed?1u:0u));
    CHECK(view.entries[0].reason==(changed?KUI_VMU_HISTORY_AFTER_RETURN:KUI_VMU_HISTORY_MANUAL));
#ifdef KUI_HISTORY_REUSE_BUDGET
    /* The watched retained image has two complete verification passes.
     * Earlier code re-read this same unchanged image six times. */
    CHECK(payload_sectors==512u);
#endif
    struct kui_sha256 hash;uint8_t digest[32];char hex[65];
    kui_sha256_init(&hash);kui_sha256_update(&hash,disk,disk_bytes);kui_sha256_digest(&hash,digest);kui_hex(digest,32u,hex);
    printf("{\"filesystem\":\"%s\",\"cluster\":%u,\"actual_cluster\":%u,\"scenario\":\"%s\",\"reads\":%" PRIu64 ",\"read_sectors\":%" PRIu64
        ",\"single_sector_read_calls\":%" PRIu64 ",\"multi_sector_read_calls\":%" PRIu64
        ",\"writes\":%" PRIu64 ",\"write_sectors\":%" PRIu64 ",\"syncs\":%" PRIu64
        ",\"allocation_walks\":%" PRIu64 ",\"allocation_reads\":%" PRIu64 ",\"allocation_read_sectors\":%" PRIu64
        ",\"retained_image_read_sectors\":%u,\"retained_image_read_calls\":%u,\"disk_sha256\":\"%s\"}\n",
        kind==FM_FAT32?"FAT32":"exFAT",(unsigned)cluster,(unsigned)fs.csize*512u,changed?"changed":"unchanged",io_reads,io_read_sectors,
        io_single_reads,io_multi_reads,io_writes,io_write_sectors,io_syncs,allocation_calls,allocation_reads,allocation_read_sectors,
        payload_sectors,payload_single_reads+payload_multi_reads,hex);
    /* A later invocation must verify again: no proof survives the boundary.
     * Foreign identity, cancellation and device faults never publish a receipt. */
    changed_storage=true;writes=0;CHECK(session_begin(&request.card,&view)==KUI_VMU_HISTORY_STALE && !writes);changed_storage=false;
    cancelled_flag=true;writes=0;CHECK(session_begin(&request.card,&view)==KUI_VMU_HISTORY_CANCELLED && !writes);cancelled_flag=false;
    failed_read_lba=payload_first;writes=0;CHECK(session_begin(&request.card,&view)<0 && !writes);failed_read_lba=UINT64_MAX;
    unmount();mount();watch_image(path);mutate_payload_after_first=true;writes=0;
    CHECK(session_begin(&request.card,&view)>=0 && view.damaged==1u);
    CHECK(!mutate_payload_after_first && payload_sectors>=512u);
    payload_first=UINT64_MAX;payload_end=0u;
    unmount();free(image);free(disk);disk=NULL;
}
int main(void) {
    for(unsigned changed=0u;changed<2u;++changed) {
        launch_trial(FM_FAT32,512u,changed!=0u);launch_trial(FM_EXFAT,4096u,changed!=0u);
        launch_trial(FM_EXFAT,131072u,changed!=0u);
    }
    return 0;
}
