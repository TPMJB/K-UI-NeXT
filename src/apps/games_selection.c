/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_selection.h"
#include <string.h>

static const char *const paths[]={KUI_GAMES_SELECTION_PATH_A,KUI_GAMES_SELECTION_PATH_B};
struct slot {bool valid;uint64_t sequence;struct kui_games_selection selection;};
static bool read_slot(unsigned index,struct slot *slot) {
    memset(slot,0,sizeof(*slot));FIL file;FRESULT r=f_open(&file,paths[index],FA_READ);
    if(r==FR_NO_FILE || r==FR_NO_PATH)return true;
    if(r!=FR_OK)return false;
    uint8_t record[KUI_GAMES_SELECTION_BYTES];UINT got=0;bool sized=f_size(&file)==sizeof(record);
    if(sized)r=f_read(&file,record,sizeof(record),&got);
    FRESULT closed=f_close(&file);
    if(r!=FR_OK || closed!=FR_OK || (sized && got!=sizeof(record)))return false;
    if(sized)slot->valid=kui_games_selection_decode(&slot->selection,&slot->sequence,record);
    return true;
}
static bool read_slots(struct slot slots[2],int *current) {
    if(!read_slot(0,&slots[0]) || !read_slot(1,&slots[1]))return false;
    if(slots[0].valid && slots[1].valid && slots[0].sequence==slots[1].sequence &&
       !kui_games_selection_equal(&slots[0].selection,&slots[1].selection))return false;
    *current=!slots[0].valid?(slots[1].valid?1:-1):
        slots[1].valid && slots[1].sequence>slots[0].sequence?1:0;
    return true;
}
bool kui_games_selection_load(struct kui_games_selection *out,kui_log_fn log) {
    if(!out)return false;
    kui_games_selection_default(out);struct slot slots[2];int current;
    if(!read_slots(slots,&current)) {if(log)log("Games selection unavailable; using /Games");return false;}
    if(current>=0)*out=slots[current].selection;
    return true;
}
static bool parents(void) {
    static const char *const names[]={"0:/KUI","0:/KUI/apps","0:/KUI/apps/games"};
    for(unsigned i=0;i<3;i++) {
        FILINFO info;FRESULT r=f_stat(names[i],&info);
        if(r==FR_OK && (info.fattrib&AM_DIR))continue;
        if(r!=FR_NO_FILE && r!=FR_NO_PATH)return false;
        if(f_mkdir(names[i])!=FR_OK)return false;
    }
    return true;
}
bool kui_games_selection_save(const struct kui_games_selection *s,kui_log_fn log) {
    if(!kui_games_selection_valid(s))return false;
    struct slot slots[2];int current;
    if(!read_slots(slots,&current))return false;
    if(current>=0 && kui_games_selection_equal(s,&slots[current].selection))return true;
    if(current>=0 && slots[current].sequence==UINT64_MAX)return false;
    uint8_t record[KUI_GAMES_SELECTION_BYTES];
    if(!kui_games_selection_encode(record,s,current<0?1:slots[current].sequence+1) || !parents())return false;
    unsigned target=current<0?0u:1u-(unsigned)current;FIL file;
    if(f_open(&file,paths[target],FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return false;
    UINT written=0;FRESULT r=f_write(&file,record,sizeof(record),&written);
    bool ok=r==FR_OK && written==sizeof(record);
    if(ok)ok=f_sync(&file)==FR_OK;
    if(f_close(&file)!=FR_OK)ok=false;
    if(ok) {
        if(f_open(&file,paths[target],FA_READ)!=FR_OK)ok=false;
        else {
            uint8_t saved[KUI_GAMES_SELECTION_BYTES];UINT got=0;
            ok=f_size(&file)==sizeof(saved) && f_read(&file,saved,sizeof(saved),&got)==FR_OK &&
                got==sizeof(saved) && !memcmp(saved,record,sizeof(saved));
            if(f_close(&file)!=FR_OK)ok=false;
        }
    }
    if(!ok && log)log("Games selection save not confirmed; previous slot retained");
    return ok;
}
