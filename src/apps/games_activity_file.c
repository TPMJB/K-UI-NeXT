/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_activity.h"
#include "ff.h"
#include <stdlib.h>
#include <string.h>
static const char *const paths[]={"0:/KUI/games-library/activity-a.bin","0:/KUI/games-library/activity-b.bin"};
struct slot {bool exists,valid;uint64_t sequence;};
static bool read_slot(unsigned which,struct slot *slot,struct kui_games_activity *table,uint8_t *wire) {
    memset(slot,0,sizeof(*slot));FIL file;FRESULT r=f_open(&file,paths[which],FA_READ);
    if(r==FR_NO_FILE || r==FR_NO_PATH)return true;
    if(r!=FR_OK)return false;
    slot->exists=true;FSIZE_t size=f_size(&file);bool sized=size>=40 && size<=KUI_GAMES_ACTIVITY_WIRE_MAX;UINT got=0;
    if(sized)r=f_read(&file,wire,(UINT)size,&got);
    if(f_close(&file)!=FR_OK || r!=FR_OK || (sized && got!=size))return false;
    if(sized)slot->valid=kui_games_activity_decode(table,&slot->sequence,wire,(size_t)size);
    return true;
}
static bool load_table(struct kui_games_activity *out,uint64_t *sequence,int *current,kui_log_fn log) {
    struct kui_games_activity *candidate=malloc(sizeof(*candidate));uint8_t *wire=malloc(KUI_GAMES_ACTIVITY_WIRE_MAX);
    if(!candidate || !wire) {free(candidate);free(wire);return false;}
    struct slot slots[2];memset(out,0,sizeof(*out));*sequence=0;*current=-1;bool ok=true;
    for(unsigned i=0;i<2;i++) {
        if(!read_slot(i,&slots[i],candidate,wire)) {ok=false;break;}
        if(!slots[i].valid)continue;
        if(*current>=0 && slots[i].sequence==*sequence && memcmp(out,candidate,sizeof(*out))) {ok=false;break;}
        if(*current<0 || slots[i].sequence>*sequence) {*out=*candidate;*sequence=slots[i].sequence;*current=(int)i;}
    }
    if(ok && *current<0 && (slots[0].exists || slots[1].exists))ok=false;
    free(candidate);free(wire);
    if(!ok && log)log("Favorites/recent history unavailable; existing files retained");
    return ok;
}
bool kui_games_activity_load(struct kui_games_activity *out,kui_log_fn log) {
    if(!out)return false;
    struct kui_games_activity *draft=malloc(sizeof(*draft));if(!draft)return false;
    uint64_t sequence;int current;bool ok=load_table(draft,&sequence,&current,log);
    if(ok)*out=*draft;
    free(draft);return ok;
}
static bool parents(void) {
    static const char *const paths_to_make[]={"0:/KUI","0:/KUI/games-library"};
    for(unsigned i=0;i<2;i++) {
        FILINFO info;FRESULT r=f_stat(paths_to_make[i],&info);
        if(r==FR_OK && (info.fattrib&AM_DIR))continue;
        if(r!=FR_NO_FILE && r!=FR_NO_PATH)return false;
        if(f_mkdir(paths_to_make[i])!=FR_OK)return false;
    }
    return true;
}
bool kui_games_activity_save(const struct kui_games_activity *table,kui_log_fn log) {
    struct kui_games_activity *previous=malloc(sizeof(*previous));uint8_t *wire=malloc(KUI_GAMES_ACTIVITY_WIRE_MAX);
    if(!previous || !wire) {free(previous);free(wire);return false;}
    uint64_t sequence=0;int current=-1;size_t bytes=0;
    bool ok=load_table(previous,&sequence,&current,log) && sequence!=UINT64_MAX &&
        kui_games_activity_encode(wire,KUI_GAMES_ACTIVITY_WIRE_MAX,&bytes,table,sequence+1) && parents();
    free(previous);
    unsigned target=current<0?0u:1u-(unsigned)current;FIL file;
    if(ok) {
        ok=f_open(&file,paths[target],FA_WRITE|FA_CREATE_ALWAYS)==FR_OK;
        if(ok) {
            UINT put=0;ok=f_write(&file,wire,(UINT)bytes,&put)==FR_OK && put==bytes;
            if(f_sync(&file)!=FR_OK)ok=false;
            if(f_close(&file)!=FR_OK)ok=false;
        }
    }
    if(ok) {
        ok=f_open(&file,paths[target],FA_READ)==FR_OK;
        if(ok) {
            ok=f_size(&file)==bytes;uint8_t chunk[512];size_t at=0;
            while(ok && at<bytes) {
                UINT got=0,want=(UINT)(bytes-at>sizeof(chunk)?sizeof(chunk):bytes-at);
                ok=f_read(&file,chunk,want,&got)==FR_OK && got==want && !memcmp(chunk,wire+at,want);at+=want;
            }
            if(f_close(&file)!=FR_OK)ok=false;
        }
    }
    free(wire);
    if(!ok && log)log("Favorites/recent history save failed; prior slot retained");
    return ok;
}
