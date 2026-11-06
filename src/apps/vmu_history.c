/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_history.h"
#include "kui/vmu_launch_history.h"
#include "kui/vmu_backing.h"
#include "kui/games_storage.h"
#include "kui/media.h"
#include "kui/clock.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>

static bool storage(void *unused,struct kui_vmu_game_candidates_storage *out) {
    (void)unused;struct kui_vmu_sd_identity identity={0};const struct kui_volume *volume=kui_media_volume();
    if(!out || !volume || !volume->count || !kui_sd_vmu_profile_identity(&identity) || !identity.sectors)return false;
    memset(out,0,sizeof(*out));memcpy(out->cid,identity.cid,16u);
    out->card_sectors=identity.sectors;out->high_capacity=identity.high_capacity?1u:0u;
    out->partition_first=volume->start;out->partition_end=(uint64_t)volume->start+volume->count;
    return out->partition_first<out->partition_end && out->partition_end<=out->card_sectors;
}
static uint64_t seconds(void) {
    struct kui_datetime now;int64_t value=0;
    return kui_clock_now(&now) && kui_clock_to_seconds(&now,&value) && value>0?(uint64_t)value:0u;
}
static void salt(void *unused,uint8_t out[16]) {
    (void)unused;static uint32_t serial;uint64_t now=seconds();
    uint32_t words[4]={(uint32_t)now,(uint32_t)(now>>32),++serial,UINT32_C(0x48495354)};
    memcpy(out,words,sizeof(words));
}
static const struct kui_vmu_history_platform platform={NULL,storage,salt};
bool kui_vmu_history_card_pin_mounted(const char *product,unsigned pair,
    const uint8_t identity[16],struct kui_vmu_game_selection *out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!identity || !pair || pair>9999u || !kui_vmu_game_card_key(product,out->product))return false;
    bool any=false;for(unsigned i=0;i<16u;i++)any|=identity[i]!=0;
    if(!any || !storage(NULL,&out->storage)){memset(out,0,sizeof(*out));return false;}
    out->pair=pair;memcpy(out->identity,identity,16u);out->selected=true;return true;
}
int kui_vmu_history_checkpoint_mounted(const struct kui_vmu_game_selection *card,
    enum kui_vmu_history_reason reason,struct kui_vmu_history_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!card)return KUI_VMU_HISTORY_ARGUMENT;
    const struct kui_vmu_history_request request={.action=KUI_VMU_HISTORY_CHECKPOINT,.card=*card};
    return kui_vmu_history_apply(&request,reason,seconds(),&platform,out,log,cancel);
}
int kui_vmu_history_launch_mounted(const struct kui_vmu_game_selection *card,
    struct kui_vmu_history_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    return kui_vmu_history_session_begin(card,seconds(),&platform,out,log,cancel);
}
int kui_vmu_history_resume_mounted(struct kui_vmu_history_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    return kui_vmu_history_session_resume(seconds(),&platform,out,log,cancel);
}
void kui_vmu_history_resume_run(struct kui_vmu_history_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out)return;
    memset(out,0,sizeof(*out));bool opened=false;int result=KUI_VMU_HISTORY_IO;
    if(!log)return;
    if(cancel && cancel()){result=KUI_VMU_HISTORY_CANCELLED;goto done;}
    opened=kui_games_storage_open(log)==KUI_GAMES_STORAGE_OK;
    if(opened)result=kui_vmu_history_session_resume(seconds(),&platform,out,log,cancel);
done:
    if(opened && !kui_games_storage_release()) {
        out->known=false;result=KUI_VMU_HISTORY_IO;
        snprintf(out->notice,sizeof(out->notice),"Previous-session storage close was not confirmed; refresh before retrying.");
    }
    if(result<0 && !out->notice[0])snprintf(out->notice,sizeof(out->notice),"Previous-session history could not be checked; receipt retained.");
    if(result<0)log("VMU previous-session history refused (%d): %s",result,out->notice);
    else if(out->known)log("VMU history: %s",out->notice);
}
void kui_vmu_history_run(const struct kui_vmu_history_request *request,
    struct kui_vmu_history_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    if(!out)return;
    memset(out,0,sizeof(*out));
    if(!request || !log){snprintf(out->notice,sizeof(out->notice),"Invalid history request.");return;}
    bool opened=false;int result=KUI_VMU_HISTORY_IO;
    if(cancel && cancel()){result=KUI_VMU_HISTORY_CANCELLED;goto done;}
    opened=kui_games_storage_open(log)==KUI_GAMES_STORAGE_OK;
    if(!opened)goto done;
    result=kui_vmu_history_apply(request,KUI_VMU_HISTORY_MANUAL,seconds(),&platform,out,log,cancel);
done:
    if(opened && !kui_games_storage_release()) {
        out->known=false;result=KUI_VMU_HISTORY_IO;
        snprintf(out->notice,sizeof(out->notice),"History storage close was not confirmed; refresh before retrying.");
    }
    if(result<0 && !out->notice[0])snprintf(out->notice,sizeof(out->notice),"%s",
        result==KUI_VMU_HISTORY_CANCELLED?"History request stopped. Original card kept.":
        result==KUI_VMU_HISTORY_STALE?"The card or SD identity changed; reopen VMU history.":
        "History could not be verified. Original card kept.");
    if(result<0)log("VMU history refused (%d): %s",result,out->notice);
}
