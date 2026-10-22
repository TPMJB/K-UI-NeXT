/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/system_settings.h"
#include <inttypes.h>
#include <string.h>

static const char *const paths[]={KUI_SYSTEM_SETTINGS_PATH_A,KUI_SYSTEM_SETTINGS_PATH_B};
struct slot {
    struct kui_system_settings settings;
    uint64_t sequence;
    bool valid,exists;
    uint8_t record[KUI_SYSTEM_SETTINGS_RECORD_SIZE];
};
static bool read_slot(unsigned index,struct slot *slot,kui_log_fn log) {
    memset(slot,0,sizeof(*slot));
    FIL file;FRESULT r=f_open(&file,paths[index],FA_READ);
    if(r==FR_NO_FILE || r==FR_NO_PATH) return true;
    if(r!=FR_OK) {
        if(log) log("Cannot read %s: FatFs=%u",paths[index]+2,(unsigned)r);
        return false;
    }
    slot->exists=true;
    UINT got=0;bool sized=f_size(&file)==sizeof(slot->record);
    if(sized) r=f_read(&file,slot->record,sizeof(slot->record),&got);
    FRESULT closed=f_close(&file);
    if(r!=FR_OK || closed!=FR_OK) {
        if(log) log("Cannot read/close %s: FatFs=%u/%u",paths[index]+2,(unsigned)r,(unsigned)closed);
        return false;
    }
    if(sized && got==sizeof(slot->record))
        slot->valid=kui_system_settings_decode(&slot->settings,&slot->sequence,slot->record,sizeof(slot->record));
    if(!slot->valid && log) log("Ignoring invalid system settings: %s",paths[index]+2);
    return true;
}
static int latest(const struct slot slots[2]) {
    if(!slots[0].valid) return slots[1].valid?1:-1;
    if(!slots[1].valid) return 0;
    return slots[1].sequence>slots[0].sequence?1:0;
}
bool kui_system_settings_load(struct kui_system_settings *out,bool legacy_show_memory,kui_log_fn log) {
    if(!out) return false;
    kui_system_settings_default(out);
    struct slot slots[2];
    if(!read_slot(0,&slots[0],log) || !read_slot(1,&slots[1],log)) {
        if(log) log("System settings unavailable; safe defaults remain in memory");
        return false;
    }
    int chosen=latest(slots);
    if(chosen>=0) {
        *out=slots[chosen].settings;
        if(log) log("Loaded %s sequence=%" PRIu64,paths[chosen]+2,slots[chosen].sequence);
        if(log && slots[chosen].record[8]==1)
            log("Legacy system preferences retained; startup chime on / Home until changed");
    } else if(!slots[0].exists && !slots[1].exists) {
        out->show_memory=legacy_show_memory;
        if(log) log("No system settings yet; retaining legacy memory-display choice");
    } else if(log) log("No valid system settings; using defaults");
    return true;
}
static bool make_parents(kui_log_fn log) {
    static const char *const parents[]={"0:/KUI","0:/KUI/apps","0:/KUI/apps/system"};
    for(unsigned i=0;i<sizeof(parents)/sizeof(parents[0]);i++) {
        FILINFO info;FRESULT r=f_stat(parents[i],&info);
        if(r==FR_OK && (info.fattrib&AM_DIR)) continue;
        if(r==FR_OK) {
            if(log) log("System settings path is a file: %s",parents[i]+2);
            return false;
        }
        if(r==FR_NO_FILE || r==FR_NO_PATH) {
            r=f_mkdir(parents[i]);
            if(r==FR_OK) continue;
        }
        if(log) log("Cannot create system settings folder %s: FatFs=%u",parents[i]+2,(unsigned)r);
        return false;
    }
    return true;
}
bool kui_system_settings_save(const struct kui_system_settings *settings,kui_log_fn log) {
    if(!kui_system_settings_valid(settings)) return false;
    struct slot slots[2];
    if(!read_slot(0,&slots[0],log) || !read_slot(1,&slots[1],log)) return false;
    int current=latest(slots);
    if(current>=0 && slots[current].sequence==UINT64_MAX) {
        if(log) log("System settings sequence exhausted; save refused");
        return false;
    }
    uint64_t sequence=current<0?1:slots[current].sequence+1;
    unsigned target=current<0?0:(unsigned)(1-current);
    uint8_t record[KUI_SYSTEM_SETTINGS_RECORD_SIZE];
    if(!kui_system_settings_encode(record,settings,sequence) || !make_parents(log)) return false;
    FIL file;FRESULT r=f_open(&file,paths[target],FA_WRITE|FA_CREATE_ALWAYS);
    if(r!=FR_OK) {if(log) log("Cannot save %s: FatFs=%u",paths[target]+2,(unsigned)r);return false;}
    UINT written=0;r=f_write(&file,record,sizeof(record),&written);
    bool ok=r==FR_OK && written==sizeof(record);
    if(ok) {r=f_sync(&file);ok=r==FR_OK;}
    FRESULT closed=f_close(&file);
    if(!ok || closed!=FR_OK) {
        if(log) log("System settings save failed: FatFs=%u/%u bytes=%u; prior slot retained",
                    (unsigned)r,(unsigned)closed,(unsigned)written);
        return false;
    }
    struct slot saved;
    if(!read_slot(target,&saved,log) || !saved.valid || memcmp(saved.record,record,sizeof(record))) {
        if(log) log("System settings reread failed; prior slot retained");
        return false;
    }
    if(log) log("Saved %s sequence=%" PRIu64,paths[target]+2,sequence);
    return true;
}
