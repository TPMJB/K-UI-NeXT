/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/update_zip.h"
#include "kui/media.h"
#include "ff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define INPUT "0:/KUI/updates/WinCE/Toy/package.zip"
#define PARTIAL "0:/KUI/system/update-stage/owned.partial"
static uint8_t *disk;static size_t disk_bytes;static FATFS fs;
static unsigned checks,progress_done;static bool active;static const char *mode;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"ZIP FS line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint64_t blocks(void *ctx) {(void)ctx;return disk_bytes/512u;}
static int reads(void *ctx,uint32_t block,size_t count,uint8_t *out) {
    (void)ctx;if((uint64_t)block+count>blocks(NULL))return -1;
    memcpy(out,disk+(size_t)block*512u,count*512u);return 0;
}
static int writes(void *ctx,uint32_t block,size_t count,const uint8_t *in) {
    (void)ctx;if((uint64_t)block+count>blocks(NULL))return -1;
    memcpy(disk+(size_t)block*512u,in,count*512u);return 0;
}
static int sync_disk(void *ctx) {(void)ctx;return 0;}
static const struct kui_media_ops media={NULL,blocks,reads,writes,sync_disk};
uint32_t kui_clock_fattime(void) {return ((2026u-1980u)<<25)|(11u<<21)|(4u<<16);}
static bool test(const char *name) {return !strcmp(mode,name);}
static bool cancel(void) {return test("cancel-before") || (test("cancel-mid") && progress_done>=32768u);}
static void progress(const struct kui_app_status *s) {progress_done=(unsigned)s->done;}
static void logger(const char *format,...) {(void)format;}
static bool private_path(const char *path) {
    static const char prefix[]="0:/KUI/system/update-stage/";
    return !strncmp(path,prefix,sizeof(prefix)-1u);
}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE flags) {
    if(active && (flags&FA_WRITE))CHECK(private_path(path));
    return __real_f_open(file,path,flags);
}
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *file,const void *data,UINT bytes,UINT *done) {
    if(active && test("write-fault")) {*done=0;return FR_DISK_ERR;}
    if(active && test("short-write")) {*done=0;return FR_OK;}
    return __real_f_write(file,data,bytes,done);
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *data,UINT bytes,UINT *done) {
    if(active && test("read-fault")) {*done=0;return FR_DISK_ERR;}
    return __real_f_read(file,data,bytes,done);
}
FRESULT __real_f_sync(FIL *);
FRESULT __wrap_f_sync(FIL *file) {
    if(active && test("sync-fault"))return FR_DISK_ERR;
    return __real_f_sync(file);
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    FRESULT result=__real_f_close(file);
    if(active && test("close-fault") && progress_done) return FR_DISK_ERR;
    return result;
}
FRESULT __real_f_getfree(const TCHAR *,DWORD *,FATFS **);
FRESULT __wrap_f_getfree(const TCHAR *path,DWORD *clusters,FATFS **volume) {
    FRESULT result=__real_f_getfree(path,clusters,volume);
    if(active && test("space") && result==FR_OK)*clusters=0u;
    return result;
}
FRESULT __real_f_unlink(const TCHAR *);
FRESULT __wrap_f_unlink(const TCHAR *path) {
    if(active)CHECK(private_path(path));
    return __real_f_unlink(path);
}
FRESULT __real_f_rename(const TCHAR *,const TCHAR *);
FRESULT __wrap_f_rename(const TCHAR *from,const TCHAR *to) {
    if(active)CHECK(private_path(from) && private_path(to));
    if(active && test("rename-fault"))return FR_DISK_ERR;
    return __real_f_rename(from,to);
}
static void parents(const char *path) {
    char p[192];CHECK(strlen(path)<sizeof(p));strcpy(p,path);
    for(char *s=p+3u;*s;s++)if(*s=='/') {*s=0;FRESULT r=f_mkdir(p);CHECK(r==FR_OK || r==FR_EXIST);*s='/';}
}
static void put(const char *path,const void *data,size_t bytes) {
    parents(path);FIL file;UINT done=0;
    CHECK(f_open(&file,path,FA_WRITE|FA_CREATE_NEW)==FR_OK);
    CHECK(f_write(&file,data,(UINT)bytes,&done)==FR_OK && done==bytes);CHECK(f_close(&file)==FR_OK);
}
static uint8_t *host(const char *path,size_t *bytes) {
    FILE *f=fopen(path,"rb");CHECK(f && !fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>=0 && !fseek(f,0,SEEK_SET));
    *bytes=(size_t)n;uint8_t *p=malloc(*bytes?*bytes:1u);CHECK(p);
    CHECK(fread(p,1,*bytes,f)==*bytes && !fclose(f));return p;
}
static void equal(const char *path,const void *data,size_t bytes) {
    FIL file;CHECK(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)==bytes);
    uint8_t buffer[8192];size_t at=0;while(at<bytes) {
        UINT n=(UINT)(bytes-at>sizeof(buffer)?sizeof(buffer):bytes-at),got=0;
        CHECK(f_read(&file,buffer,n,&got)==FR_OK && got==n && !memcmp(buffer,(const uint8_t *)data+at,n));at+=n;
    }
    CHECK(f_close(&file)==FR_OK);
}
static void absent(const char *path) {FILINFO info;FRESULT r=f_stat(path,&info);CHECK(r==FR_NO_FILE || r==FR_NO_PATH);}
int main(int argc,char **argv) {
    CHECK(argc==6);mode=argv[4];size_t archive_bytes,payload_bytes;
    disk=host(argv[1],&disk_bytes);uint8_t *archive=host(argv[2],&archive_bytes),*payload=host(argv[3],&payload_bytes);
    kui_media_set(&media);CHECK(f_mount(&fs,"0:",1)==FR_OK);
    put(INPUT,archive,archive_bytes);static const uint8_t sentinel[]="RETAIN ROOT VMU JOURNAL PREFS AND ACTIVATION";
    static const char *const protected_paths[]={"0:/KUI/runtime.kui","0:/KUI/apps/games/retail-boot.kui",
        "0:/KUI/vmu-test/p0001/card.journal","0:/KUI/vmu/cards/v0001/card.vmd","0:/KUI/prefs/vmu.bin",
        "0:/KUI/system/active0.bin","0:/KUI/system/active1.bin"};
    for(unsigned i=0;i<sizeof(protected_paths)/sizeof(protected_paths[0]);i++)put(protected_paths[i],sentinel,sizeof(sentinel));
    parents(KUI_UPDATE_ZIP_STAGE_PATH);
    static const uint8_t previous[]="PREVIOUS CACHE";
    if(test("directory-cache"))CHECK(f_mkdir(KUI_UPDATE_ZIP_STAGE_PATH)==FR_OK);
    else put(KUI_UPDATE_ZIP_STAGE_PATH,previous,sizeof(previous));
    if(test("readonly-cache"))CHECK(f_chmod(KUI_UPDATE_ZIP_STAGE_PATH,AM_RDO,AM_RDO)==FR_OK);
    if(test("directory-partial"))CHECK(f_mkdir(PARTIAL)==FR_OK);
    if(test("readonly-partial")) {put(PARTIAL,previous,sizeof(previous));CHECK(f_chmod(PARTIAL,AM_RDO,AM_RDO)==FR_OK);}
    struct kui_app_status status;char out[192];memset(out,'?',sizeof(out));active=true;
    enum kui_update_result result=kui_update_zip_prepare(test("outside")?"0:/outside.zip":INPUT,out,&status,logger,cancel,progress);
    active=false;CHECK(result==(enum kui_update_result)strtoul(argv[5],NULL,10));
    CHECK(status.complete && status.passed==(result==KUI_UPDATE_OK) && status.message[0]);
    if(!result) {CHECK(!strcmp(out,KUI_UPDATE_ZIP_STAGE_PATH));equal(out,payload,payload_bytes);CHECK(status.done==payload_bytes);}
    else {
        CHECK(!out[0]);
        if(!test("rename-fault") && !test("directory-cache"))equal(KUI_UPDATE_ZIP_STAGE_PATH,previous,sizeof(previous));
        if(test("legacy"))CHECK(strstr(status.message,"Legacy ZIP"));
        if(test("multiple"))CHECK(strstr(status.message,"multiple update packages"));
        if(test("rename-fault"))CHECK(strstr(status.message,"not confirmed"));
    }
    if(!test("directory-partial") && !test("readonly-partial"))absent(PARTIAL);
    equal(INPUT,archive,archive_bytes);
    for(unsigned i=0;i<sizeof(protected_paths)/sizeof(protected_paths[0]);i++)equal(protected_paths[i],sentinel,sizeof(sentinel));
    CHECK(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);
    printf("PASS ZIP %s: %u checks, %s\n",mode,checks,status.message);
    free(payload);free(archive);free(disk);return 0;
}
