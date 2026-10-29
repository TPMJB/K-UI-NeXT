/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_emulation_settings.h"
#include <string.h>

static const char *const paths[2]={KUI_VMU_EMULATION_SETTINGS_PATH_A,KUI_VMU_EMULATION_SETTINGS_PATH_B};
static const uint8_t magic[8]={'K','U','I','V','M','U','E','1'};
struct setting_slot {bool present,valid,enabled;uint64_t sequence;};
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(n>>(8u*i));}
static bool decode(const uint8_t record[KUI_VMU_EMULATION_SETTINGS_BYTES],struct setting_slot *slot) {
    uint64_t sequence=(uint64_t)get32(record+16u)|(uint64_t)get32(record+20u)<<32;
    if(memcmp(record,magic,sizeof(magic)) || get32(record+8u)!=1u ||
       get32(record+12u)!=KUI_VMU_EMULATION_SETTINGS_BYTES || !sequence ||
       get32(record+24u)>1u || get32(record+60u)!=kui_crc32(0,record,60u)) return false;
    for(unsigned i=28u;i<60u;i++) if(record[i]) return false;
    slot->valid=true;slot->enabled=get32(record+24u)!=0;slot->sequence=sequence;
    return true;
}
static void encode(uint8_t record[KUI_VMU_EMULATION_SETTINGS_BYTES],bool enabled,uint64_t sequence) {
    memset(record,0,KUI_VMU_EMULATION_SETTINGS_BYTES);memcpy(record,magic,sizeof(magic));
    put32(record+8u,1u);put32(record+12u,KUI_VMU_EMULATION_SETTINGS_BYTES);
    put32(record+16u,(uint32_t)sequence);put32(record+20u,(uint32_t)(sequence>>32));
    put32(record+24u,enabled?1u:0u);put32(record+60u,kui_crc32(0,record,60u));
}
static bool read_slot(unsigned index,struct setting_slot *slot) {
    memset(slot,0,sizeof(*slot));FILINFO info;
    FRESULT r=f_stat(paths[index],&info);
    if(r==FR_NO_FILE || r==FR_NO_PATH) return true;
    if(r!=FR_OK) return false;
    slot->present=true;
    if(info.fattrib&AM_DIR) return true;
    FIL file;
    if(f_open(&file,paths[index],FA_READ)!=FR_OK) return false;
    uint8_t record[KUI_VMU_EMULATION_SETTINGS_BYTES];UINT got=0;
    bool sized=f_size(&file)==sizeof(record),ok=true;
    if(sized) ok=f_read(&file,record,sizeof(record),&got)==FR_OK && got==sizeof(record);
    if(f_close(&file)!=FR_OK) ok=false;
    if(!ok) return false;
    if(sized) (void)decode(record,slot);
    return true;
}
static bool read_slots(struct setting_slot slots[2],int *current) {
    if(!read_slot(0,slots) || !read_slot(1,slots+1)) return false;
    if(slots[0].valid && slots[1].valid && slots[0].sequence==slots[1].sequence &&
       slots[0].enabled!=slots[1].enabled) return false;
    *current=!slots[0].valid?(slots[1].valid?1:-1):
        slots[1].valid && slots[1].sequence>slots[0].sequence?1:0;
    return *current>=0 || (!slots[0].present && !slots[1].present);
}
bool kui_vmu_emulation_settings_load(bool *enabled,kui_log_fn log) {
    if(!enabled) return false;
    struct setting_slot slots[2];int current;
    if(!read_slots(slots,&current)) {
        if(log) log("VMU emulation preference unavailable or damaged");
        return false;
    }
    *enabled=current<0?true:slots[current].enabled;
    return true;
}
static bool parents(void) {
    static const char *const names[2]={"0:/KUI","0:/KUI/vmu"};
    for(unsigned i=0;i<2u;i++) {
        FILINFO info;FRESULT r=f_stat(names[i],&info);
        if(r==FR_OK) {if(!(info.fattrib&AM_DIR)) return false;continue;}
        if((r!=FR_NO_FILE && r!=FR_NO_PATH) || f_mkdir(names[i])!=FR_OK) return false;
    }
    return true;
}
bool kui_vmu_emulation_settings_save(bool enabled,kui_log_fn log) {
    struct setting_slot slots[2];int current;
    if(!read_slots(slots,&current)) return false;
    if(current>=0 && slots[current].enabled==enabled) return true;
    if(current>=0 && slots[current].sequence==UINT64_MAX) return false;
    if(!parents()) return false;
    uint8_t record[KUI_VMU_EMULATION_SETTINGS_BYTES],saved[KUI_VMU_EMULATION_SETTINGS_BYTES];
    encode(record,enabled,current<0?1u:slots[current].sequence+1u);
    unsigned target=current<0?0u:1u-(unsigned)current;FIL file;UINT done=0;
    if(f_open(&file,paths[target],FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK) return false;
    bool ok=f_write(&file,record,sizeof(record),&done)==FR_OK && done==sizeof(record);
    if(ok) ok=f_sync(&file)==FR_OK;
    if(f_close(&file)!=FR_OK) ok=false;
    if(ok) {
        if(f_open(&file,paths[target],FA_READ)!=FR_OK) ok=false;
        else {
            UINT got=0;
            ok=f_size(&file)==sizeof(saved) && f_read(&file,saved,sizeof(saved),&got)==FR_OK &&
                got==sizeof(saved) && !memcmp(saved,record,sizeof(saved));
            if(f_close(&file)!=FR_OK) ok=false;
        }
    }
    if(!ok && log) log("VMU emulation preference save not confirmed; previous slot retained");
    return ok;
}
