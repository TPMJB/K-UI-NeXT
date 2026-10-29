/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_emulation_settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,calls,writes;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
struct fake_file {bool present,directory;size_t size;uint8_t bytes[128];};
static struct fake_file slots[2];
static bool parents_present,readback_corrupt,wrote;
static int fail_call=-1,write_cut=-1;
static bool fault(void) {return fail_call>=0 && calls++==(unsigned)fail_call;}
static int index_of(const char *path) {
    if(!strcmp(path,KUI_VMU_EMULATION_SETTINGS_PATH_A)) return 0;
    if(!strcmp(path,KUI_VMU_EMULATION_SETTINGS_PATH_B)) return 1;
    return -1;
}
FRESULT f_stat(const TCHAR *path,FILINFO *info) {
    if(fault())return FR_DISK_ERR;
    memset(info,0,sizeof(*info));int index=index_of(path);
    if(index<0) {
        CHECK(!strcmp(path,"0:/KUI") || !strcmp(path,"0:/KUI/vmu"));
        if(!parents_present)return FR_NO_PATH;
        info->fattrib=AM_DIR;return FR_OK;
    }
    if(!slots[index].present)return FR_NO_FILE;
    info->fattrib=slots[index].directory?AM_DIR:0;info->fsize=slots[index].size;return FR_OK;
}
FRESULT f_mkdir(const TCHAR *path) {
    CHECK(!strcmp(path,"0:/KUI") || !strcmp(path,"0:/KUI/vmu"));
    if(fault())return FR_DISK_ERR;
    parents_present=true;return FR_OK;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    if(fault())return FR_DISK_ERR;
    int index=index_of(path);CHECK(index>=0);
    if(slots[index].directory)return FR_DENIED;
    if(mode&(FA_CREATE_ALWAYS|FA_WRITE)) {
        CHECK((mode&(FA_CREATE_ALWAYS|FA_WRITE))==(FA_CREATE_ALWAYS|FA_WRITE));
        slots[index].present=true;slots[index].size=0;++writes;wrote=true;
    } else if(!slots[index].present)return FR_NO_FILE;
    memset(file,0,sizeof(*file));file->obj.sclust=(DWORD)index+1;file->obj.objsize=slots[index].size;
    return FR_OK;
}
FRESULT f_read(FIL *file,void *out,UINT bytes,UINT *got) {
    *got=0;if(fault())return FR_DISK_ERR;
    unsigned index=file->obj.sclust-1;CHECK(index<2 && file->fptr+bytes<=slots[index].size);
    memcpy(out,slots[index].bytes+(size_t)file->fptr,bytes);file->fptr+=bytes;*got=bytes;
    if(readback_corrupt && wrote && bytes)((uint8_t *)out)[0]^=1u;
    return FR_OK;
}
FRESULT f_write(FIL *file,const void *data,UINT bytes,UINT *done) {
    *done=0;if(fault())return FR_DISK_ERR;
    unsigned index=file->obj.sclust-1;CHECK(index<2 && bytes<=sizeof(slots[index].bytes));
    UINT count=write_cut>=0 && (unsigned)write_cut<bytes?(UINT)write_cut:bytes;
    memcpy(slots[index].bytes,data,count);slots[index].size=count;file->obj.objsize=count;
    *done=count;return write_cut>=0?FR_DISK_ERR:FR_OK;
}
FRESULT f_sync(FIL *file) {(void)file;return fault()?FR_DISK_ERR:FR_OK;}
FRESULT f_close(FIL *file) {(void)file;return fault()?FR_DISK_ERR:FR_OK;}
static void reset(void) {
    memset(slots,0,sizeof(slots));parents_present=readback_corrupt=wrote=false;
    fail_call=write_cut=-1;calls=writes=0;
}
static bool loaded(void) {bool enabled=false;CHECK(kui_vmu_emulation_settings_load(&enabled,NULL));return enabled;}
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(i*8));}
static void reseal(struct fake_file *slot){put32(slot->bytes+60,kui_crc32(0,slot->bytes,60));}
static void seed(void) {
    reset();CHECK(kui_vmu_emulation_settings_save(false,NULL));
    CHECK(kui_vmu_emulation_settings_save(true,NULL));
    CHECK(kui_vmu_emulation_settings_save(false,NULL));CHECK(!loaded());
}
static void basic(void) {
    reset();bool out=false;CHECK(kui_vmu_emulation_settings_load(&out,NULL) && out && !writes);
    CHECK(!kui_vmu_emulation_settings_load(NULL,NULL));
    CHECK(kui_vmu_emulation_settings_save(false,NULL));CHECK(!loaded());
    unsigned before=writes;CHECK(kui_vmu_emulation_settings_save(false,NULL) && writes==before);
    CHECK(kui_vmu_emulation_settings_save(true,NULL) && loaded());
    CHECK(kui_vmu_emulation_settings_save(false,NULL) && !loaded());
    CHECK(slots[0].present && slots[1].present);
    /* Torn newest slot falls back only to another valid complete slot. */
    slots[0].bytes[40]^=1;CHECK(loaded());slots[1].bytes[40]^=1;
    out=false;CHECK(!kui_vmu_emulation_settings_load(&out,NULL) && !out);
    out=true;CHECK(!kui_vmu_emulation_settings_load(&out,NULL) && out);
    CHECK(!kui_vmu_emulation_settings_save(false,NULL));
    reset();slots[0].present=true;slots[0].directory=true;out=false;
    CHECK(!kui_vmu_emulation_settings_load(&out,NULL) && !out);
}
static void validation(void) {
    seed();struct fake_file good=slots[0];
    /* Every bit corruption is detected, including reserved and live bytes. */
    for(unsigned i=0;i<64;i++) {
        reset();slots[0]=good;slots[0].bytes[i]^=1;
        bool out=true;CHECK(!kui_vmu_emulation_settings_load(&out,NULL) && out);
    }
    static const unsigned invalid_fields[]={0,8,12,24,28,59};
    for(unsigned i=0;i<sizeof(invalid_fields)/sizeof(*invalid_fields);i++) {
        reset();slots[0]=good;slots[0].bytes[invalid_fields[i]]^=0x80;reseal(slots);
        bool out=false;CHECK(!kui_vmu_emulation_settings_load(&out,NULL) && !out);
    }
    reset();slots[0]=good;memset(slots[0].bytes+16,0,8);reseal(slots);
    bool out=true;CHECK(!kui_vmu_emulation_settings_load(&out,NULL) && out);
    reset();slots[0]=good;slots[1]=good;put32(slots[1].bytes+24,1);reseal(slots+1);
    CHECK(!kui_vmu_emulation_settings_load(&out,NULL));
    reset();slots[0]=good;memset(slots[0].bytes+16,0xff,8);reseal(slots);
    CHECK(!loaded());unsigned before=writes;CHECK(!kui_vmu_emulation_settings_save(true,NULL) && writes==before);
    reset();slots[0]=good;slots[0].size=63;CHECK(!kui_vmu_emulation_settings_load(&out,NULL));
    slots[0].size=65;CHECK(!kui_vmu_emulation_settings_load(&out,NULL));
}
static void interruptions(void) {
    seed();struct fake_file before[2];memcpy(before,slots,sizeof(before));
    /* Active A(sequence3, Off) is never opened for write while replacing B. */
    for(int cut=0;cut<=64;cut++) {
        memcpy(slots,before,sizeof(slots));write_cut=cut;wrote=false;
        CHECK(!kui_vmu_emulation_settings_save(true,NULL));
        CHECK(!memcmp(slots,before,sizeof(slots[0])));
        write_cut=-1;CHECK(loaded()==(cut==64));
    }
    /* Fail each file API operation; the current authoritative slot survives. */
    for(int call=0;call<24;call++) {
        memcpy(slots,before,sizeof(slots));fail_call=call;calls=0;wrote=false;
        (void)kui_vmu_emulation_settings_save(true,NULL);
        CHECK(!memcmp(slots,before,sizeof(slots[0])));
        fail_call=-1;bool out=false;CHECK(kui_vmu_emulation_settings_load(&out,NULL));
    }
    memcpy(slots,before,sizeof(slots));readback_corrupt=true;wrote=false;
    CHECK(!kui_vmu_emulation_settings_save(true,NULL));
    CHECK(!memcmp(slots,before,sizeof(slots[0])));readback_corrupt=false;CHECK(loaded());
    /* An I/O error refuses even when the other slot would be valid. */
    for(int call=0;call<8;call++) {
        memcpy(slots,before,sizeof(slots));fail_call=call;calls=0;
        bool out=true;CHECK(!kui_vmu_emulation_settings_load(&out,NULL) && out);
    }
    fail_call=-1;
}
int main(void){basic();validation();interruptions();printf("VMU emulation settings: %u checks passed\n",checks);return 0;}
