/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_launch_history.h"
#include "kui/media.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>

/* GUI scratch only. The sole worker's explicit request/end delimit lifetime;
 * epoch equality is additional invalidation, never cross-job permission. */
static struct {
    bool requested,performed,valid;
    uint64_t epoch;
    struct kui_vmu_history_view view;
} launch;
void kui_vmu_launch_history_request(bool recover) {
    memset(&launch,0,sizeof(launch));launch.requested=recover;
}
bool kui_vmu_launch_history_performed(void) {return launch.performed;}
void kui_vmu_launch_history_end(void) {
    bool performed=launch.performed;
    memset(&launch,0,sizeof(launch));launch.performed=performed;
}
void kui_vmu_launch_history_begin_mounted(kui_log_fn log,kui_cancel_fn cancel) {
    if(!launch.requested)return;
    launch.requested=false;launch.performed=true;launch.valid=false;
    int result=kui_vmu_history_resume_mounted(&launch.view,log,cancel);
    if(result<0) {
        log("VMU previous-session history refused (%d): %s",result,launch.view.notice);
        return;
    }
    if(launch.view.known && launch.view.card.selected) {
        launch.epoch=kui_media_mutation_epoch();launch.valid=true;
        log("VMU history: %s",launch.view.notice);
    }
}
static bool same_storage(const struct kui_vmu_game_candidates_storage *a,
    const struct kui_vmu_game_candidates_storage *b) {
    return !memcmp(a->cid,b->cid,16u) && a->card_sectors==b->card_sectors &&
        a->partition_first==b->partition_first && a->partition_end==b->partition_end &&
        a->high_capacity==b->high_capacity;
}
static bool same_card(const struct kui_vmu_game_selection *a,const struct kui_vmu_game_selection *b) {
    return a->selected && b->selected && a->pair==b->pair &&
        !memcmp(a->product,b->product,sizeof(a->product)) &&
        !memcmp(a->identity,b->identity,16u) && same_storage(&a->storage,&b->storage);
}
int kui_vmu_launch_history_reuse(const struct kui_vmu_game_selection *card,
    const struct kui_vmu_history_platform *platform,struct kui_vmu_history_view *out,kui_cancel_fn cancel) {
    bool valid=launch.valid;launch.valid=false; /* one attempt, including refusal */
    if(!valid || !card || !out || !platform || !platform->storage ||
       (cancel && cancel()) || launch.epoch!=kui_media_mutation_epoch() ||
       !same_card(card,&launch.view.card))return 0;
    struct kui_vmu_game_candidates_storage now={0};
    if(!platform->storage(platform->context,&now) || !same_storage(&now,&card->storage))return 0;
    char path[96];int n=snprintf(path,sizeof(path),"0:/KUI/vmu-test/p%04u/identity.bin",(unsigned)card->pair);
    FIL file;UINT got=0u;uint8_t identity[16];
    if(n<=0 || n>=(int)sizeof(path) || f_open(&file,path,FA_READ)!=FR_OK)return 0;
    bool current=f_size(&file)==sizeof(identity) &&
        f_read(&file,identity,sizeof(identity),&got)==FR_OK && got==sizeof(identity);
    if(f_close(&file)!=FR_OK)current=false;
    if(!current || (cancel && cancel()) || memcmp(identity,card->identity,sizeof(identity)) ||
       launch.epoch!=kui_media_mutation_epoch())return 0;
    *out=launch.view;out->changed=false;
    return 1;
}
