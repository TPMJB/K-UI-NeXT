/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_launcher.h"
#include "kui/disc_launch.h"
#include "kui/disc_vmu_card.h"
#include "kui/disc_region.h"
#include "kui/vmu_emulation_settings.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool preference(bool *enabled,kui_log_fn log,kui_cancel_fn cancel) {
    if(cancel && cancel()) return false;
    if(!kui_sd_connect()) return false;
    FATFS fs;
    bool mounted=kui_mount(&fs,log);
    bool loaded=mounted && kui_vmu_emulation_settings_load(enabled,log);
    bool released=f_mount(NULL,"0:",0)==FR_OK;
    kui_sd_disconnect();
    return loaded && released && !(cancel && cancel());
}
static bool failure(struct kui_app_status *out,const char *message,kui_cancel_fn cancel,
    kui_app_progress_fn progress) {
    *out=(struct kui_app_status){.complete=true};
    out->stopped=cancel && cancel();out->errors=out->stopped?0u:1u;
    snprintf(out->message,sizeof(out->message),"%s",message);
    if(progress) progress(out);
    return false;
}
void kui_disc_launcher_vmu_info(struct kui_vmu_game_info *info,struct kui_app_status *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!info || !out || !log || !cancel) return;
    memset(info,0,sizeof(*info));*out=(struct kui_app_status){0};
    bool enabled=false;
    if(!preference(&enabled,log,cancel)) {
        snprintf(info->notice,sizeof(info->notice),"Cannot read Virtual VMU setting; check Diagnostics.");
        (void)failure(out,info->notice,cancel,progress);return;
    }
    /* Off does not identify a product, inspect a card catalogue, prepare a
     * profile or read any card/journal. The later boot uses the baseline. */
    if(!enabled) {
        info->known=true;
        snprintf(info->label,sizeof(info->label),"Physical VMUs");
        snprintf(info->notice,sizeof(info->notice),"Virtual VMU is Off. Direct boot uses physical VMUs.");
    } else {
        struct kui_disc_boot_plan plan;
        if(!kui_disc_launcher_identity(&plan,out,log,cancel,progress)) {
            snprintf(info->notice,sizeof(info->notice),"%.127s",out->message);return;
        }
        kui_vmu_game_info_run(plan.metadata.product,true,info,log,cancel);
        if(cancel()) {(void)failure(out,"Disc card check stopped.",cancel,progress);return;}
        if(!info->known) {(void)failure(out,info->notice,cancel,progress);return;}
    }
    out->complete=out->passed=true;out->errors=0u;
    snprintf(out->message,sizeof(out->message),"%s",info->notice[0]?info->notice:info->label);
    if(progress) progress(out);
}
static bool selected_card(const char *product,uint8_t profile[4096],void *context,
    kui_log_fn log,kui_cancel_fn cancel) {
    const struct kui_vmu_game_selection *selection=context;
    memset(profile,0,4096u);
    if(selection && selection->product[0]) {
        char key[KUI_VMU_GAME_CARD_KEY_BYTES];
        if(!memchr(selection->product,0,sizeof(selection->product)) ||
           !kui_vmu_game_card_key(product,key) || memcmp(key,selection->product,sizeof(key))) {
            log("Physical disc virtual VMU: disc product changed after card review");
            return false;
        }
    }
    if(cancel() || !kui_sd_connect()) return false;
    FATFS fs;bool mounted=kui_mount(&fs,log);
    struct kui_disc_vmu_card *card=calloc(1u,sizeof(*card));
    int result=KUI_DISC_VMU_CARD_FAILED;
    if(mounted && card) result=kui_disc_vmu_card_prepare(product,true,&fs,selection,card,log,cancel);
    bool released=f_mount(NULL,"0:",0)==FR_OK;
    kui_sd_disconnect();
    bool ok=result==KUI_DISC_VMU_CARD_READY && released && !cancel();
    if(ok) memcpy(profile,card->profile_wire,4096u);
    else if(card && card->choice.problem[0]) log("Physical disc virtual VMU: %s",card->choice.problem);
    if(card) {memset(card,0,sizeof(*card));free(card);}
    return ok;
}
bool kui_disc_launcher_prepare_vmu(const struct kui_vmu_game_selection *selection,
    struct kui_runtime_image *image,struct kui_app_status *out,kui_log_fn log,
    kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!image || !out || !log || !cancel) return false;
    *image=(struct kui_runtime_image){0};
    struct kui_disc_launcher_region region={0};
    if(!kui_disc_region_load(&region.region_free,log,cancel))
        return failure(out,"Region-free preference unavailable; see GD Play Options.",cancel,progress);
    if(!region.region_free) {
        region.console=kui_disc_region_console();
        if(region.console!='J' && region.console!='U' && region.console!='E')
            return failure(out,"Console region is unavailable; enable Region-free in GD Play Options.",cancel,progress);
    }
    bool enabled=false;
    if(!preference(&enabled,log,cancel))
        return failure(out,"Virtual VMU preference unavailable; see Diagnostics.",cancel,progress);
    if(!enabled) return kui_disc_launcher_prepare_module(KUI_DISC_LAUNCH_PACKAGE,NULL,
        NULL,&region,image,out,log,cancel,progress);
    return kui_disc_launcher_prepare_module(KUI_DISC_LAUNCH_VMU_PACKAGE,selected_card,
        (void *)selection,&region,image,out,log,cancel,progress);
}
