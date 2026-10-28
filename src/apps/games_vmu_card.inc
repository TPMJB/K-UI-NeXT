/* SPDX-License-Identifier: GPL-3.0-only */
/* Gives each game its own virtual card: the virtual-A2 module's P2 profile
 * slot is filled with the game's pair before handoff. The stage admits that
 * pair at boot exactly as it admits an installed one. */
#include "kui/vmu_game_card_service.h"
#include "kui/vmu_backing.h"
#include "kui/clock.h"

struct games_card_platform {FATFS *fs;struct kui_volume volume;struct kui_media_ops raw;};
static int games_card_read(void *context,uint32_t lba,uint8_t out[512]) {
    struct games_card_platform *p=context;
    return p->raw.read && !p->raw.read(p->raw.ctx,lba,1u,out)?0:-1;
}
/* CMD10 while the mounted volume is idle, as the import tool pins its card. */
static bool games_card_identity(void *context,struct kui_vmu_game_card_raw *out) {
    struct kui_vmu_sd_identity id;(void)context;
    if(!kui_sd_vmu_profile_identity(&id)) return false;
    memcpy(out->cid,id.cid,16u);out->card_sectors=id.sectors;out->high_capacity=id.high_capacity;
    return true;
}
static bool games_card_raw_begin(void *context,struct kui_vmu_game_card_raw *out) {
    struct games_card_platform *p=context;
    if(f_mount(NULL,"0:",0)!=FR_OK || !kui_sd_raw_read_ops(&p->raw) || !p->raw.read) return false;
    out->read_sector=games_card_read;out->context=p;
    return true;
}
static bool games_card_raw_end(void *context) {
    struct games_card_platform *p=context;p->raw=(struct kui_media_ops){0};
    if(f_mount(p->fs,"0:",1)!=FR_OK) {(void)f_mount(NULL,"0:",0);return false;}
    const struct kui_volume *now=kui_media_volume();
    return (p->fs->fs_type==FS_FAT32 || p->fs->fs_type==FS_EXFAT) &&
        now->start==p->volume.start && now->count==p->volume.count;
}
static void games_card_salt(void *context,uint8_t out[16]) {
    static uint32_t calls;struct games_card_platform *p=context;
    struct kui_datetime now;int64_t seconds=0;
    if(kui_clock_now(&now)) (void)kui_clock_to_seconds(&now,&seconds);
    uint32_t words[4]={(uint32_t)seconds,(uint32_t)((uint64_t)seconds>>32),++calls,p->fs->last_clst};
    memcpy(out,words,sizeof(words));
}
/* Called once the virtual-A2 module will really run this game. UNBOUND
 * (no usable product number) keeps an installed card; the build template
 * refuses, since the stage could never admit it. */
static bool games_card_prepare(const struct kui_retail_manifest *map,struct kui_runtime_image *package,
    FATFS *fs,const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    static char refusal[128];
    size_t slot=kui_vmu_game_card_profile_slot(package->data,package->info.payload_bytes);
    if(slot==SIZE_MAX) {*problem="virtual-A2 module has no card profile slot; reinstall the matching candidate";return false;}
    struct kui_vmu_game_card_work *work=malloc(sizeof(*work));
    struct kui_vmu_fs_range *forbidden=map->extent_count?malloc(sizeof(*forbidden)*map->extent_count):NULL;
    uint8_t *wire=malloc(KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);
    if(!work || !wire || (map->extent_count && !forbidden)) {
        free(work);free(forbidden);free(wire);*problem="not enough memory for the virtual card";return false;
    }
    for(uint32_t i=0;i<map->extent_count;i++) {
        const struct kui_retail_extent *e=&map->slots[map->track_count+i].extent;
        forbidden[i]=(struct kui_vmu_fs_range){e->card_lba,e->blocks};
    }
    struct games_card_platform context={fs,*kui_media_volume(),{0}};
    const struct kui_vmu_game_card_platform platform={&context,games_card_identity,games_card_raw_begin,
        games_card_raw_end,games_card_salt};
    struct kui_vmu_game_card_choice choice;
    int result=kui_vmu_game_card_select(map->product,map->partition_start,map->partition_end,
        forbidden,map->extent_count,&platform,work,wire,&choice,log,cancel);
    bool ok=false;
    if(result==KUI_VMU_GAME_CARD_READY) {
        memcpy((uint8_t *)package->data+slot,wire,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);ok=true;
    } else if(result==KUI_VMU_GAME_CARD_UNBOUND) {
        if(kui_vmu_persist_profile_decode(&work->cached,(const uint8_t *)package->data+slot,
           KUI_VMU_PERSIST_PROFILE_WIRE_BYTES) || kui_vmu_game_card_template(work->cached.cid))
            *problem="this game has no product number for its own virtual card, and no card is installed";
        else {log("Virtual card: product \"%s\" names no game card; using the installed card",map->product);ok=true;}
    } else if(result==KUI_VMU_GAME_CARD_CANCELLED) *problem="cancelled while preparing the virtual card";
    else {
        snprintf(refusal,sizeof(refusal),"virtual card: %s",choice.problem);
        *problem=refusal;
    }
    free(work);free(forbidden);free(wire);
    return ok;
}
