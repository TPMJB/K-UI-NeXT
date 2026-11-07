/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_vmu_card.h"
#include "kui/clock.h"
#include "kui/media.h"
#include "kui/vmu_backing.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct disc_card_platform {FATFS *fs;struct kui_volume volume;struct kui_media_ops raw;};
static int disc_card_read(void *context,uint32_t lba,uint8_t out[512]) {
    struct disc_card_platform *p=context;
    return p->raw.read && !p->raw.read(p->raw.ctx,lba,1u,out)?0:-1;
}
static int disc_card_read_sectors(void *context,uint32_t lba,uint32_t count,uint8_t *out) {
    struct disc_card_platform *p=context;
    return p->raw.read && !p->raw.read(p->raw.ctx,lba,count,out)?0:-1;
}
static bool disc_card_identity(void *context,struct kui_vmu_game_card_raw *out) {
    (void)context;struct kui_vmu_sd_identity id;
    if(!kui_sd_vmu_profile_identity(&id)) return false;
    memcpy(out->cid,id.cid,16u);out->card_sectors=id.sectors;out->high_capacity=id.high_capacity;
    return true;
}
static bool disc_card_raw_begin(void *context,struct kui_vmu_game_card_raw *out) {
    struct disc_card_platform *p=context;
    if(f_mount(NULL,"0:",0)!=FR_OK || !kui_sd_raw_read_ops(&p->raw) || !p->raw.read) return false;
    out->read_sector=disc_card_read;out->read_sectors=disc_card_read_sectors;out->context=p;return true;
}
static bool disc_card_raw_end(void *context) {
    struct disc_card_platform *p=context;p->raw=(struct kui_media_ops){0};
    if(f_mount(p->fs,"0:",1)!=FR_OK) {(void)f_mount(NULL,"0:",0);return false;}
    const struct kui_volume *now=kui_media_volume();
    return now && (p->fs->fs_type==FS_FAT32 || p->fs->fs_type==FS_EXFAT) &&
        now->start==p->volume.start && now->count==p->volume.count;
}
static void disc_card_salt(void *context,uint8_t out[16]) {
    static uint32_t calls;struct disc_card_platform *p=context;
    struct kui_datetime now;int64_t seconds=0;
    if(kui_clock_now(&now)) (void)kui_clock_to_seconds(&now,&seconds);
    uint32_t words[4]={(uint32_t)seconds,(uint32_t)((uint64_t)seconds>>32),++calls,p->fs->last_clst};
    memcpy(out,words,sizeof(words));
}
static int refused(struct kui_disc_vmu_card *out,const char *message) {
    memset(out->profile_wire,0,sizeof(out->profile_wire));out->enabled=false;
    snprintf(out->choice.problem,sizeof(out->choice.problem),"%s",message);
    return KUI_DISC_VMU_CARD_FAILED;
}
static bool product_key(const char *product,char key[KUI_VMU_GAME_CARD_KEY_BYTES]) {
    if(!product) return false;
    /* Bound the source before the shared normalizer's ordinary C-string API. */
    size_t n=0;while(n<32u && product[n]) ++n;
    return n<32u && kui_vmu_game_card_key(product,key);
}
int kui_disc_vmu_card_prepare(const char *product,bool enabled,FATFS *fs,
    const struct kui_vmu_game_selection *selection,struct kui_disc_vmu_card *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!out) return KUI_DISC_VMU_CARD_FAILED;
    memset(out,0,sizeof(*out));
    if(!enabled) return KUI_DISC_VMU_CARD_PHYSICAL_ONLY;
    if(!product_key(product,out->product))
        return refused(out,"Disc has no usable product number for its virtual VMU.");
    if(selection && selection->selected &&
       (!memchr(selection->product,0,sizeof(selection->product)) ||
        memcmp(selection->product,out->product,sizeof(out->product))))
        return refused(out,"The virtual VMU choice belongs to another game.");
    if(cancel && cancel()) return KUI_DISC_VMU_CARD_CANCELLED;
    if(!fs || !log || (fs->fs_type!=FS_FAT32 && fs->fs_type!=FS_EXFAT))
        return refused(out,"A mounted FAT32/exFAT volume is required for virtual VMU preparation.");
    if(kui_storage_active()!=KUI_STORAGE_SCI)
        return refused(out,"Physical-disc virtual VMU currently requires SCI SD storage.");
    const struct kui_volume *mounted=kui_media_volume();
    if(!mounted || !mounted->count ||
       (uint64_t)mounted->start+mounted->count>UINT64_C(0x100000000))
        return refused(out,"Cannot verify the mounted virtual VMU partition.");
    struct disc_card_platform context={fs,*mounted,{0}};
    const struct kui_vmu_game_card_platform platform={&context,disc_card_identity,
        disc_card_raw_begin,disc_card_raw_end,disc_card_salt};
    struct kui_vmu_game_card_work *work=malloc(sizeof(*work));
    if(!work) return refused(out,"Not enough memory to prepare this disc's virtual VMU.");
    int result;
    if(selection && selection->selected) {
        struct kui_vmu_game_candidate_target target={.pair=selection->pair};
        memcpy(target.identity,selection->identity,16u);
        result=kui_vmu_game_card_select_exact(out->product,&target,&selection->storage,
            context.volume.start,(uint64_t)context.volume.start+context.volume.count,
            NULL,0u,&platform,work,out->profile_wire,&out->choice,log,cancel);
    } else result=kui_vmu_game_card_select(out->product,context.volume.start,
        (uint64_t)context.volume.start+context.volume.count,NULL,0u,&platform,work,
        out->profile_wire,&out->choice,log,cancel);
    if(result==KUI_VMU_GAME_CARD_READY) {
        char expected[KUI_VMU_PERSIST_PROFILE_PATH_BYTES]={0};
        snprintf(expected,sizeof(expected),"/KUI/vmu-test/p%04u",out->choice.pair);
        bool valid=out->choice.pair>=1u && out->choice.pair<=9999u &&
            !memcmp(out->choice.key,out->product,sizeof(out->product)) &&
            !kui_vmu_persist_profile_decode(&work->cached,out->profile_wire,sizeof(out->profile_wire)) &&
            work->cached.partition_first==context.volume.start &&
            work->cached.partition_end==(uint64_t)context.volume.start+context.volume.count &&
            !memcmp(work->cached.pair_path,expected,sizeof(expected)) &&
            (!selection || !selection->selected ||
             (out->choice.pair==selection->pair && !memcmp(work->cached.image_id,selection->identity,16u)));
        const struct kui_volume *now=kui_media_volume();
        valid=valid && now && now->start==context.volume.start && now->count==context.volume.count &&
            (fs->fs_type==FS_FAT32 || fs->fs_type==FS_EXFAT);
        free(work);
        if(!valid) return refused(out,"Prepared virtual VMU profile does not match this disc's selected card.");
        if(cancel && cancel()) {memset(out->profile_wire,0,sizeof(out->profile_wire));return KUI_DISC_VMU_CARD_CANCELLED;}
        out->enabled=true;return KUI_DISC_VMU_CARD_READY;
    }
    free(work);memset(out->profile_wire,0,sizeof(out->profile_wire));
    if(result==KUI_VMU_GAME_CARD_CHOICE_REQUIRED) return KUI_DISC_VMU_CARD_CHOICE_REQUIRED;
    if(result==KUI_VMU_GAME_CARD_CANCELLED) return KUI_DISC_VMU_CARD_CANCELLED;
    if(!out->choice.problem[0]) return refused(out,"The disc's virtual VMU could not be prepared.");
    return KUI_DISC_VMU_CARD_FAILED;
}
