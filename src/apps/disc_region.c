/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_region.h"
#include "platform.h"
#include <string.h>
#if defined(KUI_ON_CONSOLE) && !defined(KUI_DISC_REGION_TEST)
#include <dc/flashrom.h>
#endif

static const char *const paths[2]={KUI_DISC_REGION_PATH_A,KUI_DISC_REGION_PATH_B};
static const uint8_t magic[8]={'K','U','I','D','R','G','0','1'};
struct slot {bool present,valid,enabled;uint64_t sequence;};
static void quiet(const char *format,...) {(void)format;}
static bool stopped(kui_cancel_fn cancel) {return cancel && cancel();}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0u;i<4u;i++) p[i]=(uint8_t)(value>>(8u*i));
}
static bool decode(const uint8_t wire[KUI_DISC_REGION_RECORD_BYTES],struct slot *slot) {
    uint64_t sequence=(uint64_t)get32(wire+16u)|(uint64_t)get32(wire+20u)<<32;
    if(memcmp(wire,magic,sizeof(magic)) || get32(wire+8u)!=1u ||
       get32(wire+12u)!=KUI_DISC_REGION_RECORD_BYTES || !sequence ||
       get32(wire+24u)>1u || get32(wire+60u)!=kui_crc32(0u,wire,60u)) return false;
    for(unsigned i=28u;i<60u;i++) if(wire[i]) return false;
    slot->valid=true;slot->enabled=get32(wire+24u)!=0u;slot->sequence=sequence;
    return true;
}
static void encode(uint8_t wire[KUI_DISC_REGION_RECORD_BYTES],bool enabled,uint64_t sequence) {
    memset(wire,0,KUI_DISC_REGION_RECORD_BYTES);memcpy(wire,magic,sizeof(magic));
    put32(wire+8u,1u);put32(wire+12u,KUI_DISC_REGION_RECORD_BYTES);
    put32(wire+16u,(uint32_t)sequence);put32(wire+20u,(uint32_t)(sequence>>32));
    put32(wire+24u,enabled?1u:0u);put32(wire+60u,kui_crc32(0u,wire,60u));
}
static bool read_slot(unsigned index,struct slot *slot) {
    memset(slot,0,sizeof(*slot));FILINFO info;
    FRESULT result=f_stat(paths[index],&info);
    if(result==FR_NO_FILE || result==FR_NO_PATH) return true;
    if(result!=FR_OK) return false;
    slot->present=true;
    if(info.fattrib&AM_DIR) return true;
    FIL file;
    if(f_open(&file,paths[index],FA_READ)!=FR_OK) return false;
    uint8_t wire[KUI_DISC_REGION_RECORD_BYTES];UINT got=0u;
    bool sized=f_size(&file)==sizeof(wire),ok=true;
    if(sized) ok=f_read(&file,wire,sizeof(wire),&got)==FR_OK && got==sizeof(wire);
    if(f_close(&file)!=FR_OK) ok=false;
    if(ok && sized) (void)decode(wire,slot);
    return ok;
}
static bool read_slots(struct slot slots[2],int *current) {
    if(!read_slot(0u,slots) || !read_slot(1u,slots+1)) return false;
    if(slots[0].valid && slots[1].valid && slots[0].sequence==slots[1].sequence &&
       slots[0].enabled!=slots[1].enabled) return false;
    *current=!slots[0].valid?(slots[1].valid?1:-1):
        slots[1].valid && slots[1].sequence>slots[0].sequence?1:0;
    return *current>=0 || (!slots[0].present && !slots[1].present);
}
static bool parents(void) {
    static const char *const directories[2]={"0:/KUI","0:/KUI/disc"};
    for(unsigned i=0u;i<2u;i++) {
        FILINFO info;FRESULT result=f_stat(directories[i],&info);
        if(result==FR_OK) {if(!(info.fattrib&AM_DIR)) return false;continue;}
        if((result!=FR_NO_FILE && result!=FR_NO_PATH) || f_mkdir(directories[i])!=FR_OK) return false;
    }
    return true;
}
static bool save_mounted(bool enabled,kui_cancel_fn cancel) {
    struct slot slots[2];int current;
    if(!read_slots(slots,&current) || stopped(cancel)) return false;
    if(current>=0 && slots[current].enabled==enabled) return true;
    if(current>=0 && slots[current].sequence==UINT64_MAX) return false;
    if(!parents() || stopped(cancel)) return false;
    uint8_t wire[KUI_DISC_REGION_RECORD_BYTES],saved[KUI_DISC_REGION_RECORD_BYTES];
    encode(wire,enabled,current<0?1u:slots[current].sequence+1u);
    unsigned target=current<0?0u:1u-(unsigned)current;FIL file;UINT done=0u;
    if(f_open(&file,paths[target],FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK) return false;
    bool ok=f_write(&file,wire,sizeof(wire),&done)==FR_OK && done==sizeof(wire);
    if(ok) ok=f_sync(&file)==FR_OK;
    if(f_close(&file)!=FR_OK) ok=false;
    if(ok) {
        if(f_open(&file,paths[target],FA_READ)!=FR_OK) ok=false;
        else {
            UINT got=0u;
            ok=f_size(&file)==sizeof(saved) && f_read(&file,saved,sizeof(saved),&got)==FR_OK &&
                got==sizeof(saved) && !memcmp(saved,wire,sizeof(saved));
            if(f_close(&file)!=FR_OK) ok=false;
        }
    }
    return ok;
}
bool kui_disc_region_load(bool *enabled,kui_log_fn log,kui_cancel_fn cancel) {
    if(!enabled || stopped(cancel)) return false;
    if(!log) log=quiet;
    if(!kui_sd_connect()) {log("Disc region setting: SD unavailable");return false;}
    FATFS fs;bool mounted=kui_mount(&fs,log);
    struct slot slots[2];int current=-1;
    bool ok=mounted && !stopped(cancel) && read_slots(slots,&current);
    bool value=ok?(current<0?true:slots[current].enabled):false;
    bool released=f_mount(NULL,"0:",0)==FR_OK;
    kui_sd_disconnect();
    ok=ok && released && !stopped(cancel);
    if(ok) *enabled=value;
    else log("Disc region setting unavailable, damaged or stopped");
    return ok;
}
bool kui_disc_region_save(bool enabled,kui_log_fn log,kui_cancel_fn cancel) {
    if(stopped(cancel)) return false;
    if(!log) log=quiet;
    if(!kui_sd_connect()) {log("Disc region setting: SD unavailable");return false;}
    FATFS fs;bool mounted=kui_mount(&fs,log);
    bool ok=mounted && !stopped(cancel) && save_mounted(enabled,cancel);
    bool released=f_mount(NULL,"0:",0)==FR_OK;
    kui_sd_disconnect();
    if(!ok || !released) log("Disc region preference save not confirmed; prior slot retained");
    return ok && released;
}
char kui_disc_region_console(void) {
#ifdef KUI_DISC_REGION_TEST
    /* Numeric fixtures match the pinned KOS dc/flashrom.h public constants. */
    extern int kui_disc_region_test_flash(void);
    int region=kui_disc_region_test_flash();
    return region==1?'J':region==2?'U':region==3?'E':0;
#elif defined(KUI_ON_CONSOLE)
    int region=flashrom_get_region();
    return region==FLASHROM_REGION_JAPAN?'J':region==FLASHROM_REGION_US?'U':
        region==FLASHROM_REGION_EUROPE?'E':0;
#else
    return 0;
#endif
}
