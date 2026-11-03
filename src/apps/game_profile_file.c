/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_profile.h"
#include <stdlib.h>
#include <string.h>

bool kui_game_profile_pack_load(struct kui_game_profile_pack *out,kui_log_fn log) {
    if(!out)return false;
    memset(out,0,sizeof(*out));FIL file;FRESULT result=f_open(&file,KUI_GAME_PROFILE_PACK_PATH,FA_READ);
    if(result==FR_NO_FILE || result==FR_NO_PATH)return true;
    if(result!=FR_OK){if(log)log("Game profile pack unavailable: FatFs=%u",(unsigned)result);return false;}
    FSIZE_t bytes=f_size(&file);uint8_t *record=bytes>=36u && bytes<=KUI_GAME_PROFILE_PACK_MAX?malloc((size_t)bytes):NULL;
    UINT got=0u;bool ok=record && f_read(&file,record,(UINT)bytes,&got)==FR_OK && got==bytes;
    if(f_close(&file)!=FR_OK)ok=false;
    if(ok)ok=kui_game_profile_pack_decode(out,record,(size_t)bytes);
    free(record);
    if(!ok){memset(out,0,sizeof(*out));if(log)log("Game profile pack ignored: malformed, unreadable or unsupported; built-ins retained");}
    return ok;
}
