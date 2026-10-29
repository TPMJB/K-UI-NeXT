/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/apps.h"
#include "kui/vmu_library.h"
#include "kui/games_storage.h"
#include <stdio.h>
#include <string.h>

void kui_vmu_collection_rename_run(const char *path,const char *label,const uint8_t identity[16],
    unsigned page,struct kui_vmu_backup_view *out,kui_log_fn log,kui_cancel_fn cancel,
    kui_app_progress_fn progress) {
    if(!out) return;
    memset(out,0,sizeof(*out));out->page=page;
    bool opened=false,saved=false,ok=false;
    snprintf(out->status.message,sizeof(out->status.message),"Saving the virtual VMU name...");
    if(progress) progress(&out->status);
    if(cancel && cancel()) {out->status.stopped=true;goto done;}
    opened=kui_games_storage_open(log)==KUI_GAMES_STORAGE_OK;
    if(!opened) goto done;
    int result=kui_vmu_library_rename(path,label,identity,log,cancel);
    if(result!=KUI_VMU_LIBRARY_OK) {
        if(result==KUI_VMU_LIBRARY_CANCELLED) out->status.stopped=true;
        goto done;
    }
    saved=true;
    ok=kui_vmu_library_list(page,out,log,cancel)==KUI_VMU_LIBRARY_OK;
done:
    if(opened && !kui_games_storage_release()) ok=false;
    out->status.complete=true;out->status.passed=ok;
    if(ok) snprintf(out->status.message,sizeof(out->status.message),"Virtual VMU name saved. Game saves are unchanged.");
    else {
        ++out->status.errors;
        snprintf(out->status.message,sizeof(out->status.message),"%s",saved?
            "Name saved; refresh the virtual VMU list to confirm it.":
            "Name update not confirmed; refresh the cards before retrying.");
    }
    if(progress) progress(&out->status);
}
