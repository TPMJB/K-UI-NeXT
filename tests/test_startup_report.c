/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual FatFs appends and rotation on a disposable mounted block device. */
#include "kui/startup_report.h"
#include "kui/media.h"
#include "ff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,writes;
static unsigned mount_calls;
static bool observe;
static uint8_t *disk;
static size_t disk_bytes;
static FATFS fs;
enum failure {NONE,OPEN_FAILURE,WRITE_FAILURE,SHORT_WRITE,SYNC_FAILURE,CLOSE_FAILURE,RENAME_FAILURE};
static enum failure fail;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Startup report failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint64_t blocks(void *unused) {(void)unused;return disk_bytes/512u;}
static int read_blocks(void *unused,uint32_t lba,size_t count,uint8_t *out) {
    (void)unused;if((uint64_t)lba+count>blocks(NULL))return -1;
    memcpy(out,disk+(size_t)lba*512u,count*512u);return 0;
}
static int write_blocks(void *unused,uint32_t lba,size_t count,const uint8_t *in) {
    (void)unused;if((uint64_t)lba+count>blocks(NULL))return -1;
    ++writes;memcpy(disk+(size_t)lba*512u,in,count*512u);return 0;
}
static int sync_blocks(void *unused) {(void)unused;return 0;}
static const struct kui_media_ops media={NULL,blocks,read_blocks,write_blocks,sync_blocks};
uint32_t kui_clock_fattime(void) {return ((2026u-1980u)<<25)|(11u<<21)|(4u<<16);}

FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE flags) {
    if(observe) {
        CHECK(!strcmp(path,KUI_STARTUP_REPORT_PATH) || !strcmp(path,KUI_STARTUP_REPORT_PREVIOUS_PATH));
        if(fail==OPEN_FAILURE)return FR_DENIED;
    }
    return __real_f_open(file,path,flags);
}
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *file,const void *data,UINT bytes,UINT *done) {
    if(observe && fail==WRITE_FAILURE) {*done=0;return FR_DISK_ERR;}
    if(observe && fail==SHORT_WRITE)return __real_f_write(file,data,bytes/2u,done);
    return __real_f_write(file,data,bytes,done);
}
FRESULT __real_f_sync(FIL *);
FRESULT __wrap_f_sync(FIL *file) {
    FRESULT result=__real_f_sync(file);
    return observe && fail==SYNC_FAILURE?FR_DISK_ERR:result;
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    FRESULT result=__real_f_close(file);
    return observe && fail==CLOSE_FAILURE?FR_DISK_ERR:result;
}
FRESULT __real_f_rename(const TCHAR *,const TCHAR *);
FRESULT __wrap_f_rename(const TCHAR *from,const TCHAR *to) {
    if(observe) {
        CHECK(!strcmp(from,KUI_STARTUP_REPORT_PATH) && !strcmp(to,KUI_STARTUP_REPORT_PREVIOUS_PATH));
        if(fail==RENAME_FAILURE)return FR_DENIED;
    }
    return __real_f_rename(from,to);
}
FRESULT __real_f_unlink(const TCHAR *);
FRESULT __wrap_f_unlink(const TCHAR *path) {
    if(observe)CHECK(!strcmp(path,KUI_STARTUP_REPORT_PREVIOUS_PATH));
    return __real_f_unlink(path);
}
FRESULT __real_f_mount(FATFS *,const TCHAR *,BYTE);
FRESULT __wrap_f_mount(FATFS *volume,const TCHAR *path,BYTE opt) {
    CHECK(!observe);++mount_calls;return __real_f_mount(volume,path,opt);
}
static void write_file(const char *path,const void *data,size_t bytes) {
    FIL file;UINT done=0;CHECK(bytes<=UINT32_MAX);
    CHECK(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    CHECK(f_write(&file,data,(UINT)bytes,&done)==FR_OK && done==bytes);
    CHECK(f_close(&file)==FR_OK);
}
static void same_file(const char *path,const void *data,size_t bytes) {
    FIL file;UINT got=0;uint8_t *actual=malloc(bytes+1u);CHECK(actual);
    CHECK(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)==bytes);
    CHECK(f_read(&file,actual,(UINT)(bytes+1u),&got)==FR_OK && got==bytes);
    CHECK(!memcmp(actual,data,bytes) && f_close(&file)==FR_OK);free(actual);
}
static void absent(const char *path) {
    FILINFO info;FRESULT result=f_stat(path,&info);CHECK(result==FR_NO_FILE || result==FR_NO_PATH);
}
static void remove_file(const char *path) {
    FRESULT result=f_unlink(path);CHECK(result==FR_OK || result==FR_NO_FILE || result==FR_NO_PATH);
}
static bool append(const void *data,size_t bytes,enum failure failure) {
    fail=failure;observe=true;unsigned mounted=mount_calls;
    bool result=kui_startup_report_append_mounted(data,bytes);
    observe=false;fail=NONE;CHECK(mount_calls==mounted);return result;
}
static const char protected_data[]="Retained runtime/settings/save data must remain byte-identical.\n";
static const char *const protected_paths[]={"0:/KUI/runtime.kui","0:/KUI/settings.bin","0:/KUI/card.vmd"};
static void preserved(void) {
    for(unsigned i=0;i<sizeof(protected_paths)/sizeof(protected_paths[0]);i++)
        same_file(protected_paths[i],protected_data,sizeof(protected_data));
}
static void cases(void) {
    CHECK(KUI_STARTUP_REPORT_RECORD_MAX<=KUI_STARTUP_REPORT_MAX_BYTES);
    for(unsigned i=0;i<sizeof(protected_paths)/sizeof(protected_paths[0]);i++)
        write_file(protected_paths[i],protected_data,sizeof(protected_data));
    unsigned before=writes;
    CHECK(!append(NULL,1u,NONE));CHECK(!append("",0u,NONE));
    CHECK(!append("x",KUI_STARTUP_REPORT_RECORD_MAX+1u,NONE));CHECK(writes==before);
    absent(KUI_STARTUP_REPORT_PATH);absent(KUI_STARTUP_REPORT_PREVIOUS_PATH);
    static const char first[]="BOOT first\nUpdates: using the retained root baseline\n";
    static const char second[]="BOOT chained\nUpdates: bank refused: wrong loader cohort\n";
    CHECK(append(first,sizeof(first)-1u,NONE));CHECK(append(second,sizeof(second)-1u,NONE));
    char joined[sizeof(first)+sizeof(second)];
    memcpy(joined,first,sizeof(first)-1u);memcpy(joined+sizeof(first)-1u,second,sizeof(second)-1u);
    same_file(KUI_STARTUP_REPORT_PATH,joined,sizeof(first)+sizeof(second)-2u);
    absent(KUI_STARTUP_REPORT_PREVIOUS_PATH);preserved();

    size_t max=KUI_STARTUP_REPORT_MAX_BYTES;
    uint8_t *full=malloc(max),*older=malloc(max);CHECK(full && older);
    for(size_t i=0;i<max;i++) {full[i]=(uint8_t)(i*37u+19u);older[i]=(uint8_t)(i*13u+7u);}
    write_file(KUI_STARTUP_REPORT_PATH,full,max);
    write_file(KUI_STARTUP_REPORT_PREVIOUS_PATH,older,max);
    CHECK(append(second,sizeof(second)-1u,NONE));
    same_file(KUI_STARTUP_REPORT_PREVIOUS_PATH,full,max);
    same_file(KUI_STARTUP_REPORT_PATH,second,sizeof(second)-1u);preserved();

    /* Reaching the bound exactly appends, while the next byte rotates. */
    write_file(KUI_STARTUP_REPORT_PATH,full,max-(sizeof(first)-1u));
    CHECK(append(first,sizeof(first)-1u,NONE));
    memcpy(full+max-(sizeof(first)-1u),first,sizeof(first)-1u);
    same_file(KUI_STARTUP_REPORT_PATH,full,max);
    CHECK(append("!",1u,NONE));same_file(KUI_STARTUP_REPORT_PREVIOUS_PATH,full,max);
    same_file(KUI_STARTUP_REPORT_PATH,"!",1u);preserved();

    for(enum failure failure=OPEN_FAILURE;failure<=CLOSE_FAILURE;failure++) {
        write_file(KUI_STARTUP_REPORT_PATH,first,sizeof(first)-1u);
        CHECK(!append(second,sizeof(second)-1u,failure));preserved();
        /* Every error closes its file, so a fresh caller can still append. */
        CHECK(append("retry\n",6u,NONE));
    }
    write_file(KUI_STARTUP_REPORT_PATH,full,max);
    CHECK(!append("!",1u,RENAME_FAILURE));same_file(KUI_STARTUP_REPORT_PATH,full,max);preserved();
    remove_file(KUI_STARTUP_REPORT_PATH);remove_file(KUI_STARTUP_REPORT_PREVIOUS_PATH);
    free(full);free(older);
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));
    long bytes=ftell(input);CHECK(bytes>0 && !(bytes%512) && !fseek(input,0,SEEK_SET));
    disk_bytes=(size_t)bytes;disk=malloc(disk_bytes);CHECK(disk);
    CHECK(fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));
    kui_media_set(&media);CHECK(f_mount(&fs,"0:",1)==FR_OK);
    FRESULT directory=f_mkdir("0:/KUI");CHECK(directory==FR_OK || directory==FR_EXIST);
    cases();CHECK(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);free(disk);
    printf("PASS startup report: %u checks; real filesystem append/rotation, failures, protected files\n",checks);
    return 0;
}
