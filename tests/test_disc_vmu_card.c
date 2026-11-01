/* SPDX-License-Identifier: GPL-3.0-only */
/* Physical-disc card glue with the actual product/profile codecs. The fake
 * selector exercises its real mounted/raw callbacks and identity-pinned input;
 * card allocation/recovery itself has separate FAT32/exFAT integration tests. */
#include "kui/disc_vmu_card.h"
#include "kui/vmu_backing.h"
#include "kui/clock.h"
#include "kui/media.h"
#include "kui/storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,calls,exact_calls,identity_reads,raw_reads,mounts,unmounts,active_reads;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"%s:%u: %s\n",__FILE__,__LINE__,#x);exit(1);}} while(0)
static FATFS filesystem;
static struct kui_volume volume={2048u,1000000u,true};
static struct kui_disc_vmu_card card;
static struct kui_vmu_persist_profile profile;
static unsigned transport=KUI_STORAGE_SCI;
static bool mounted=true,remount_error,move_volume,bad_profile,bad_path,bad_partition,bad_identity;
static bool wrong_key,late_cancel,immediate_cancel;
static int select_result;
static struct kui_vmu_game_selection selection;

unsigned kui_storage_active(void) {++active_reads;return transport;}
const struct kui_volume *kui_media_volume(void) {return &volume;}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE opt) {
    CHECK(!strcmp(path,"0:"));
    if(!fs) {CHECK(!opt);++unmounts;mounted=false;return FR_OK;}
    CHECK(fs==&filesystem && opt==1u);++mounts;
    if(remount_error) {filesystem.fs_type=0;return FR_DISK_ERR;}
    mounted=true;if(move_volume) volume.start+=8u;return FR_OK;
}
static int raw_read(void *context,uint32_t block,size_t count,uint8_t *data) {
    CHECK(context==&filesystem && !mounted && count==1u);++raw_reads;
    memset(data,(int)(block&255u),512u);return 0;
}
bool kui_sd_raw_read_ops(struct kui_media_ops *out) {
    CHECK(!mounted);*out=(struct kui_media_ops){.ctx=&filesystem,.read=raw_read};return true;
}
bool kui_sd_vmu_profile_identity(struct kui_vmu_sd_identity *out) {
    CHECK(mounted);++identity_reads;memset(out,0,sizeof(*out));
    memcpy(out->cid,profile.cid,16u);out->sectors=profile.card_sectors;out->high_capacity=true;return true;
}
bool kui_clock_now(struct kui_datetime *out) {memset(out,0,sizeof(*out));return false;}
bool kui_clock_to_seconds(const struct kui_datetime *value,int64_t *out) {(void)value;*out=0;return false;}
static bool cancel(void) {return immediate_cancel || (late_cancel && calls);}
static void log_line(const char *format,...) {(void)format;}
static int snapshot(void *context,unsigned role,uint32_t block,uint8_t out[512]) {
    (void)context;memset(out,(int)(role*37u+block),512u);
    if(role==1u && block==1u) memset(out,0,512u);
    if(role==2u) memcpy(out,profile.image_id,16u);
    return 0;
}
static void fixture(void) {
    memset(&profile,0,sizeof(profile));
    profile.card_sectors=2000000u;profile.partition_first=2048u;profile.partition_end=1002048u;
    profile.transport=1u;profile.high_capacity=1u;profile.flags=1u;
    memset(profile.cid,0x42,16u);memset(profile.image_id,0x23,16u);
    strcpy(profile.pair_path,"/KUI/vmu-test/p0023");
    profile.geometry=(struct kui_vmu_persist_geometry){.fs_type=KUI_VMU_PERSIST_FAT32,
        .sector_bytes=512u,.sectors_per_cluster=8u,.volume_serial=0x12345678u,
        .fat_count=2u,.active_fat=0u,.fat_first_lba=4096u,.fat_sectors=1024u,
        .data_first_lba=8192u,.cluster_count=100000u,.root_cluster=2u,.fat_mirrored=1u};
    const unsigned sizes[3]={131072u,1024u,16u},sectors[3]={256u,2u,1u},clusters[3]={10u,1000u,2000u};
    for(unsigned role=0;role<3u;role++) {
        struct kui_vmu_persist_file *f=&profile.files[role];
        f->file_bytes=f->valid_bytes=sizes[role];f->first_cluster=clusters[role];
        f->directory_first_cluster=5u;f->entry_lba=8192u+24u;f->entry_offset=role*96u;
        f->entry_count=2u;f->entry_kind=1u;f->useful_count=f->allocated_count=1u;
        f->useful[0]=(struct kui_vmu_persist_extent){0u,8192u+(clusters[role]-2u)*8u,sectors[role]};
        f->allocated[0]=f->useful[0];f->allocated[0].blocks=((sectors[role]+7u)/8u)*8u;
    }
    CHECK(!kui_vmu_persist_profile_collect_snapshot(&profile,snapshot,NULL));
    filesystem=(FATFS){.fs_type=FS_FAT32,.last_clst=77u};volume=(struct kui_volume){2048u,1000000u,true};
    transport=KUI_STORAGE_SCI;mounted=true;
    remount_error=move_volume=bad_profile=bad_path=bad_partition=bad_identity=wrong_key=late_cancel=immediate_cancel=false;
    select_result=KUI_VMU_GAME_CARD_READY;
    calls=exact_calls=identity_reads=raw_reads=mounts=unmounts=active_reads=0u;
    selection=(struct kui_vmu_game_selection){.pair=23u,.selected=true,.candidates_sequence=17u};
    strcpy(selection.product,"MK-51006");memset(selection.identity,0x23,16u);
    memcpy(selection.storage.cid,profile.cid,16u);selection.storage.card_sectors=profile.card_sectors;
    selection.storage.high_capacity=1u;selection.storage.partition_first=2048u;selection.storage.partition_end=1002048u;
}
int kui_vmu_game_card_select(const char *product,uint64_t first,uint64_t end,
    const struct kui_vmu_fs_range *forbidden,size_t count,const struct kui_vmu_game_card_platform *platform,
    struct kui_vmu_game_card_work *work,uint8_t wire[4096],struct kui_vmu_game_card_choice *choice,
    kui_log_fn log,kui_cancel_fn stop) {
    ++calls;CHECK(work && !strcmp(product,"MK-51006") && first==2048u && end==1002048u);
    CHECK(!forbidden && count==0u && log==log_line && stop==cancel && mounted);
    struct kui_vmu_game_card_raw raw={0};CHECK(platform->identity(platform->context,&raw));
    CHECK(raw.card_sectors==profile.card_sectors && raw.high_capacity && !memcmp(raw.cid,profile.cid,16u));
    CHECK(platform->raw_begin(platform->context,&raw));uint8_t sector[512];
    CHECK(!raw.read_sector(raw.context,0x1234u,sector) && sector[0]==0x34u);
    bool ended=platform->raw_end(platform->context);
    if(!ended) return KUI_VMU_GAME_CARD_FAILED;
    uint8_t a[16],b[16];platform->salt(platform->context,a);platform->salt(platform->context,b);CHECK(memcmp(a,b,16u));
    strcpy(choice->key,wrong_key?"WRONG-ID":"MK-51006");choice->pair=23u;
    if(select_result!=KUI_VMU_GAME_CARD_READY) {
        strcpy(choice->problem,"selector-specific refusal");memset(wire,0xa5,4096u);return select_result;
    }
    if(bad_path) strcpy(profile.pair_path,"/KUI/vmu-test/p0005");
    if(bad_partition) profile.partition_first=1024u;
    if(bad_identity) {
        profile.image_id[0]^=1u;
        CHECK(!kui_vmu_persist_profile_collect_snapshot(&profile,snapshot,NULL));
    }
    CHECK(!kui_vmu_persist_profile_encode(&profile,wire));
    if(bad_profile) wire[3000]^=1u;
    return select_result;
}
int kui_vmu_game_card_select_exact(const char *product,const struct kui_vmu_game_candidate_target *target,
    const struct kui_vmu_game_candidates_storage *storage,uint64_t first,uint64_t end,
    const struct kui_vmu_fs_range *forbidden,size_t count,const struct kui_vmu_game_card_platform *platform,
    struct kui_vmu_game_card_work *work,uint8_t wire[4096],struct kui_vmu_game_card_choice *choice,
    kui_log_fn log,kui_cancel_fn stop) {
    ++exact_calls;CHECK(target && target->pair==selection.pair && !memcmp(target->identity,selection.identity,16u));
    CHECK(storage==&selection.storage && storage->partition_first==first && storage->partition_end==end);
    return kui_vmu_game_card_select(product,first,end,forbidden,count,platform,work,wire,choice,log,stop);
}
static bool zero(const void *data,size_t bytes) {
    const uint8_t *p=data;for(size_t i=0;i<bytes;i++) if(p[i]) return false;return true;
}
static int run(bool enabled,const struct kui_vmu_game_selection *selected) {
    memset(&card,0xa5,sizeof(card));
    return kui_disc_vmu_card_prepare(" mk-51006 ",enabled,&filesystem,selected,&card,log_line,cancel);
}
int main(void) {
    fixture();CHECK(kui_disc_vmu_card_prepare((const char *)(uintptr_t)1,false,
        (FATFS *)(uintptr_t)1,(const struct kui_vmu_game_selection *)(uintptr_t)1,&card,NULL,cancel)==KUI_DISC_VMU_CARD_PHYSICAL_ONLY);
    CHECK(zero(&card,sizeof(card)) && !active_reads && !calls && !identity_reads && !mounts && !unmounts);
    CHECK(kui_disc_vmu_card_prepare(NULL,true,&filesystem,NULL,NULL,log_line,cancel)==KUI_DISC_VMU_CARD_FAILED);
    const char *invalid[]={NULL,"","000-000","bad/product","product-longer-than-ten","12345678901234567890123456789012"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        CHECK(kui_disc_vmu_card_prepare(invalid[i],true,&filesystem,NULL,&card,log_line,cancel)==KUI_DISC_VMU_CARD_FAILED);
        CHECK(!card.enabled && zero(card.profile_wire,sizeof(card.profile_wire)) && !calls && !active_reads);
    }
    fixture();strcpy(selection.product,"OTHER-ID");CHECK(run(true,&selection)==KUI_DISC_VMU_CARD_FAILED && !calls && !active_reads);
    fixture();memset(selection.product,'X',sizeof(selection.product));CHECK(run(true,&selection)==KUI_DISC_VMU_CARD_FAILED && !calls);
    fixture();filesystem.fs_type=0;CHECK(run(true,NULL)==KUI_DISC_VMU_CARD_FAILED && !calls && !active_reads);
    fixture();transport=KUI_STORAGE_SCIF;CHECK(run(true,NULL)==KUI_DISC_VMU_CARD_FAILED && !calls && strstr(card.choice.problem,"SCI"));
    fixture();immediate_cancel=true;CHECK(run(true,NULL)==KUI_DISC_VMU_CARD_CANCELLED && !calls && !active_reads);
    fixture();CHECK(run(true,NULL)==KUI_DISC_VMU_CARD_READY);
    CHECK(card.enabled && !strcmp(card.product,"MK-51006") && card.choice.pair==23u && calls==1u && !exact_calls);
    CHECK(mounted && mounts==1u && unmounts==1u && raw_reads==1u && identity_reads==1u);
    struct kui_vmu_persist_profile decoded;CHECK(!kui_vmu_persist_profile_decode(&decoded,card.profile_wire,sizeof(card.profile_wire)));
    CHECK(!memcmp(decoded.image_id,profile.image_id,16u));
    fixture();CHECK(run(true,&selection)==KUI_DISC_VMU_CARD_READY && card.enabled && exact_calls==1u);
    const int refusals[]={KUI_VMU_GAME_CARD_CHOICE_REQUIRED,KUI_VMU_GAME_CARD_FAILED,KUI_VMU_GAME_CARD_CANCELLED,KUI_VMU_GAME_CARD_UNBOUND};
    const int expected[]={KUI_DISC_VMU_CARD_CHOICE_REQUIRED,KUI_DISC_VMU_CARD_FAILED,KUI_DISC_VMU_CARD_CANCELLED,KUI_DISC_VMU_CARD_FAILED};
    for(unsigned i=0;i<4u;i++) {fixture();select_result=refusals[i];CHECK(run(true,NULL)==expected[i]);
        CHECK(!card.enabled && zero(card.profile_wire,sizeof(card.profile_wire)) && calls==1u);}
    for(unsigned mode=0;mode<5u;mode++) {
        fixture();bad_profile=mode==0u;bad_path=mode==1u;bad_partition=mode==2u;bad_identity=mode==3u;wrong_key=mode==4u;
        CHECK(run(true,&selection)==KUI_DISC_VMU_CARD_FAILED && !card.enabled && zero(card.profile_wire,sizeof(card.profile_wire)));
    }
    fixture();late_cancel=true;CHECK(run(true,NULL)==KUI_DISC_VMU_CARD_CANCELLED && !card.enabled && zero(card.profile_wire,sizeof(card.profile_wire)));
    fixture();move_volume=true;CHECK(run(true,NULL)==KUI_DISC_VMU_CARD_FAILED && !card.enabled && zero(card.profile_wire,sizeof(card.profile_wire)));
    fixture();remount_error=true;CHECK(run(true,NULL)==KUI_DISC_VMU_CARD_FAILED && !card.enabled && zero(card.profile_wire,sizeof(card.profile_wire)));
    printf("PASS physical-disc VMU card preparation: %u checks; Off skips lookup, normalized shared product, exact choice, SCI refusal and cleared failures\n",checks);
    return 0;
}
