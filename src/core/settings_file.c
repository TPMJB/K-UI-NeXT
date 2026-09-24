/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/settings.h"
#include <inttypes.h>
#include <string.h>

static const char *const paths[]={KUI_SETTINGS_PATH_A,KUI_SETTINGS_PATH_B};
struct slot {struct kui_settings settings;uint64_t sequence;bool valid;};
/* Invalid records are recoverable. An I/O error is different: we cannot know
 * which slot is newest, so a save must stop before opening either for write. */
static bool read_slot(unsigned index,struct slot *slot,kui_log_fn log) {
    memset(slot,0,sizeof(*slot));
    FIL file;FRESULT r=f_open(&file,paths[index],FA_READ);
    if(r==FR_NO_FILE || r==FR_NO_PATH) return true;
    if(r!=FR_OK) {
        if(log) log("Cannot read %s: FatFs=%u",paths[index]+2,(unsigned)r);
        return false;
    }
    uint8_t record[KUI_SETTINGS_RECORD_SIZE];UINT got=0;
    bool sized=f_size(&file)==sizeof(record);
    if(sized) r=f_read(&file,record,sizeof(record),&got);
    FRESULT closed=f_close(&file);
    if(r!=FR_OK || closed!=FR_OK) {
        if(log) log("Cannot read/close %s: FatFs=%u/%u",paths[index]+2,(unsigned)r,(unsigned)closed);
        return false;
    }
    if(sized && got==sizeof(record))
        slot->valid=kui_settings_decode(&slot->settings,&slot->sequence,record,sizeof(record));
    if(!slot->valid && log) log("Ignoring invalid settings record: %s",paths[index]+2);
    return true;
}
static int latest(const struct slot slots[2]) {
    if(!slots[0].valid) return slots[1].valid?1:-1;
    if(!slots[1].valid) return 0;
    return slots[1].sequence>slots[0].sequence?1:0;
}
bool kui_settings_load(struct kui_settings *out,kui_log_fn log) {
    if(!out) return false;
    kui_settings_default(out);
    struct slot slots[2];
    if(!read_slot(0,&slots[0],log) || !read_slot(1,&slots[1],log)) {
        if(log) log("Settings unavailable; defaults remain in memory");
        return false;
    }
    int chosen=latest(slots);
    if(chosen<0) {
        if(log) log("No valid saved settings; using defaults");
    } else {
        *out=slots[chosen].settings;
        if(log) log("Loaded %s sequence=%" PRIu64,paths[chosen]+2,slots[chosen].sequence);
    }
    return true;
}
bool kui_settings_save(const struct kui_settings *settings,kui_log_fn log) {
    if(!settings) return false;
    struct slot slots[2];
    if(!read_slot(0,&slots[0],log) || !read_slot(1,&slots[1],log)) return false;
    int current=latest(slots);
    uint64_t sequence=current<0?1:slots[current].sequence+1;
    if(!sequence) {if(log) log("Settings sequence exhausted; save refused");return false;}
    unsigned target=current<0?0:(unsigned)(1-current);
    uint8_t record[KUI_SETTINGS_RECORD_SIZE];
    if(!kui_settings_encode(record,settings,sequence)) return false;
    FRESULT r=f_mkdir("0:/KUI");
    if(r!=FR_OK && r!=FR_EXIST) {
        if(log) log("Cannot create /KUI for settings: FatFs=%u",(unsigned)r);
        return false;
    }
    FIL file;r=f_open(&file,paths[target],FA_WRITE|FA_CREATE_ALWAYS);
    if(r!=FR_OK) {if(log) log("Cannot save %s: FatFs=%u",paths[target]+2,(unsigned)r);return false;}
    UINT written=0;r=f_write(&file,record,sizeof(record),&written);
    bool ok=r==FR_OK && written==sizeof(record);
    if(ok) {r=f_sync(&file);ok=r==FR_OK;}
    FRESULT closed=f_close(&file);
    ok=ok && closed==FR_OK;
    if(!ok) {
        if(log) log("Settings save failed: FatFs=%u/%u bytes=%u; prior slot retained",
                    (unsigned)r,(unsigned)closed,(unsigned)written);
        return false;
    }
    struct slot saved;
    if(!read_slot(target,&saved,log) || !saved.valid || saved.sequence!=sequence ||
       saved.settings.crc_only!=settings->crc_only || saved.settings.end_readback!=settings->end_readback ||
       saved.settings.show_memory!=settings->show_memory) {
        if(log) log("Settings reread failed; prior slot retained");
        return false;
    }
    if(log) log("Saved %s sequence=%" PRIu64,paths[target]+2,sequence);
    return true;
}
