/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/salvage_completion.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct kui_capture_plan plan={.count=3,.bytes=6u*KUI_RAW_BYTES,.tracks={
    {1,4,0,150,152,302},{2,0,0,302,304,304},{3,4,1,45150,45152,45152}}};
static struct {
    FILE *image;uint64_t blocks;unsigned optical_reads;
    bool repairing,cancelled,cancel_read,cancel_catalogue,checking,injected;
    const char *kind;
    uint8_t original[3][2*KUI_RAW_BYTES];
} test;
static FATFS fs;
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *file,const void *data,UINT bytes,UINT *put) {
    if(test.checking && !test.injected && !strcmp(test.kind,"report-write")) {
        test.injected=true;*put=0;return FR_DISK_ERR;
    }
    return __real_f_write(file,data,bytes,put);
}
FRESULT __real_f_sync(FIL *);
FRESULT __wrap_f_sync(FIL *file) {
    if(test.checking && !test.injected && !strcmp(test.kind,"report-sync")) {test.injected=true;return FR_DISK_ERR;}
    return __real_f_sync(file);
}
FRESULT __real_f_rename(const TCHAR *,const TCHAR *);
FRESULT __wrap_f_rename(const TCHAR *from,const TCHAR *to) {
    if(test.checking && !test.injected && !strcmp(test.kind,"report-rename")) {test.injected=true;return FR_DISK_ERR;}
    return __real_f_rename(from,to);
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *data,UINT bytes,UINT *got) {
    FRESULT r=__real_f_read(file,data,bytes,got);
    if(test.checking && !test.injected && !strcmp(test.kind,"report-readback") && r==FR_OK &&
       *got>=25 && !memcmp(data,"KUI_SALVAGE_COMPLETION_V1",25)) {test.injected=true;((uint8_t *)data)[4]^=1;}
    return r;
}
static void log_line(const char *format,...) {
    va_list args;va_start(args,format);vprintf(format,args);va_end(args);puts("");
}
static uint64_t blocks(void *ctx) {(void)ctx;return test.blocks;}
static int read_blocks(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;return fseeko(test.image,(off_t)block*512,SEEK_SET) || fread(data,512,count,test.image)!=count?-1:0;
}
static int write_blocks(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;return fseeko(test.image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,test.image)!=count?-1:0;
}
static int sync_blocks(void *ctx) {(void)ctx;return fflush(test.image) || fsync(fileno(test.image))?-1:0;}
static bool cancelled(void *ctx) {(void)ctx;return test.cancelled;}
static void salvage_progress(void *ctx,const struct kui_salvage_status *status) {
    (void)ctx;test.repairing=status->pass>0;
}
static enum kui_read_result optical(void *ctx,uint32_t fad,unsigned count,uint8_t *data) {
    (void)ctx;++test.optical_reads;
    for(unsigned i=0;i<plan.count;++i)if(fad>=plan.tracks[i].start && fad<plan.tracks[i].end) {
        assert(count<=plan.tracks[i].end-fad);
        if(!test.repairing && fad<=151 && fad+count>151)return KUI_READ_RETRY;
        memcpy(data,test.original[i]+(fad-plan.tracks[i].start)*KUI_RAW_BYTES,count*KUI_RAW_BYTES);return KUI_READ_OK;
    }
    assert(false);return KUI_READ_FATAL;
}
static void check_progress(void *ctx,const struct kui_salvage_check_status *status) {
    (void)ctx;assert(status->done<=status->total);
    if(test.cancel_read && status->done)test.cancelled=true;
    if(test.cancel_catalogue && strstr(status->message,"Comparing recovered tracks"))test.cancelled=true;
}
static void mount_fs(void) {assert(kui_mount(&fs,log_line));}
static void unmount_fs(void) {assert(f_mount(NULL,"0:",0)==FR_OK);}
static void write_file(const char *path,const void *data,UINT bytes) {
    FIL file;UINT put;assert(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    assert(f_write(&file,data,bytes,&put)==FR_OK && put==bytes);
    assert(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static void catalogue(bool wrong,bool invalid) {
    char data[1024];size_t used=(size_t)snprintf(data,sizeof(data),"%s\nG\t3\tIndependent completion fixture\n",
        invalid?"INVALID_HEADER":"DREAMSHELL_REDUMP_CRC_V1");
    for(unsigned i=0;i<plan.count;++i)used+=(size_t)snprintf(data+used,sizeof(data)-used,
        "T\t%u\t%u\t%08x\n",i+1,2u*KUI_RAW_BYTES,
        kui_crc32(0,test.original[i],2u*KUI_RAW_BYTES)^(wrong?1u:0u));
    memcpy(data+used,"E\n",2);used+=2;write_file("0:/KUI/redump.db",data,(UINT)used);
}
static void check_report(const struct kui_salvage_check_status *status,bool complete) {
    mount_fs();
    if(status->report[0]) {
        char path[KUI_DEST_PATH_CAP+3];snprintf(path,sizeof(path),"0:%s",status->report);
        FIL file;assert(f_open(&file,path,FA_READ)==FR_OK);char report[4096];UINT got=0;
        assert(f_size(&file)<sizeof(report));assert(f_read(&file,report,sizeof(report)-1,&got)==FR_OK);
        report[got]=0;assert(f_close(&file)==FR_OK);
        assert(strstr(status->report,complete?".txt":".part"));
        if(complete) {
            assert(strstr(report,"header_identity_sha256 ") && strstr(report,"TRACK 03 ") && strstr(report,"COMPLETE\n"));
            assert(strstr(report,status->saved_verified?"saved_track_bytes_verified YES":"saved_track_bytes_verified NO"));
        }
    } else assert(!complete);
    unmount_fs();
}
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    const char *kind=argv[3];test.kind=kind;test.image=fopen(argv[1],"r+b");assert(test.image);
    assert(!fseeko(test.image,0,SEEK_END));test.blocks=(uint64_t)ftello(test.image)/512;
    const struct kui_media_ops media={NULL,blocks,read_blocks,write_blocks,sync_blocks};kui_media_set(&media);
    for(unsigned i=0;i<plan.count;++i) {
        char path[512];snprintf(path,sizeof(path),"%s/track%02u.%s",argv[2],i+1,i==1?"raw":"bin");
        FILE *input=fopen(path,"rb");assert(input && fread(test.original[i],1,sizeof(test.original[i]),input)==sizeof(test.original[i]) && !fclose(input));
    }
    if(!strncmp(kind,"cd-",3)) {plan.profile=KUI_CAPTURE_PROFILE_CD_TOC;for(unsigned i=0;i<3;++i) {plan.tracks[i].control=0;plan.tracks[i].session=0;plan.tracks[i].start=150+2*i;plan.tracks[i].end=plan.tracks[i].toc_end=152+2*i;}}
    struct kui_capture_ops capture={.read=optical,.cancelled=cancelled,.log=log_line};
    const struct kui_salvage_options options={.passes=1,.progress=salvage_progress};
    struct kui_salvage_status receipt;
    assert(kui_salvage_run(&plan,&capture,&options,KUI_SALVAGE_NEW,&receipt)==KUI_SALVAGE_RESOLVED);
    /* Fast collection defers the whole failed two-sector range, including its
     * readable neighbour; targeted recovery restores both before completion. */
    assert(receipt.complete && receipt.targets==2 && receipt.recovered==2 && !receipt.remaining);
    for(unsigned i=0;i<plan.count;++i)assert(receipt.final_crc[i]==kui_crc32(0,test.original[i],sizeof(test.original[i])));
    unsigned optical_before=test.optical_reads;
    bool verify=strcmp(kind,"lookup") && strcmp(kind,"no-database") && strcmp(kind,"catalogue-mismatch") &&
        strcmp(kind,"cancel-catalogue") && strcmp(kind,"data-changed-no-readback");
    mount_fs();
    if(strcmp(kind,"no-database"))catalogue(!strcmp(kind,"catalogue-mismatch"),!strcmp(kind,"catalogue-error"));
    if(!strcmp(kind,"data-changed") || !strcmp(kind,"length-changed") || !strcmp(kind,"data-changed-no-readback")) {
        char path[KUI_DEST_PATH_CAP];snprintf(path,sizeof(path),"0:%s/track01.bin",receipt.job);
        if(strncmp(kind,"data-changed",12)==0)test.original[0][2*KUI_RAW_BYTES-1]^=1;
        write_file(path,test.original[0],!strcmp(kind,"length-changed")?KUI_RAW_BYTES:2*KUI_RAW_BYTES);
    }
    if(!strcmp(kind,"identity-changed")) {
        char path[KUI_DEST_PATH_CAP];snprintf(path,sizeof(path),"0:%s/header.bin",receipt.job);
        uint8_t bytes[4096];FIL file;UINT got;
        assert(f_open(&file,path,FA_READ)==FR_OK && f_read(&file,bytes,sizeof(bytes),&got)==FR_OK && got==sizeof(bytes) && f_close(&file)==FR_OK);
        bytes[42]^=1;write_file(path,bytes,sizeof(bytes));
    }
    unmount_fs();
    if(!strcmp(kind,"invalid-receipt"))receipt.remaining=1;
    if(!strcmp(kind,"invalid-job"))memcpy(receipt.job,"/KUI/salvage/job-0001/../../Games",31);
    test.cancel_read=!strcmp(kind,"cancel-readback");test.cancel_catalogue=!strcmp(kind,"cancel-catalogue");
    const struct kui_salvage_check_ops check={.cancelled=cancelled,.progress=check_progress,.log=log_line};
    struct kui_salvage_check_status status;
    struct kui_capture_plan selected=plan;
    if(!strcmp(kind,"cd-profile-changed"))selected.profile=KUI_CAPTURE_PROFILE_GD;
    if(!strcmp(kind,"plan-changed")) {++selected.tracks[0].start;++selected.tracks[0].end;}
    test.checking=true;
    enum kui_salvage_check_result result=kui_salvage_check_complete(&selected,&receipt,verify,&check,&status);
    test.checking=false;
    assert(test.optical_reads==optical_before);
    if(!strcmp(kind,"data-changed") || !strcmp(kind,"length-changed") || !strcmp(kind,"identity-changed") ||
       !strcmp(kind,"invalid-receipt") || !strcmp(kind,"invalid-job") || !strcmp(kind,"plan-changed") || !strcmp(kind,"cd-profile-changed") || !strncmp(kind,"report-",7)) {
        assert(result==KUI_SALVAGE_CHECK_FAILED && !status.complete && !status.saved_verified && !status.catalogue_checked && !status.report_written);
        if(!strncmp(kind,"report-",7))assert(test.injected);
    }
    else if(!strncmp(kind,"cancel-",7))
        assert(result==KUI_SALVAGE_CHECK_STOPPED && !status.complete && !status.saved_verified && !status.catalogue_checked);
    else {
        assert(result==KUI_SALVAGE_CHECK_COMPLETE && status.complete && status.saved_verified==verify && status.catalogue_checked && status.report_written);
        if(!strcmp(kind,"no-database"))assert(status.catalogue.result==KUI_KNOWN_NO_DATABASE);
        else if(!strcmp(kind,"catalogue-mismatch"))assert(status.catalogue.result==KUI_KNOWN_NO_MATCH);
        else if(!strcmp(kind,"catalogue-error"))assert(status.catalogue.result==KUI_KNOWN_ERROR);
        else assert(status.catalogue.result==KUI_KNOWN_FULL_MATCH);
        if(verify)assert(status.done==plan.bytes && !status.verifying_files);
    }
    check_report(&status,status.complete);
    if(!strcmp(kind,"repeat")) {
        char first[KUI_DEST_PATH_CAP];strcpy(first,status.report);test.checking=true;
        assert(kui_salvage_check_complete(&plan,&receipt,verify,&check,&status)==KUI_SALVAGE_CHECK_COMPLETE);
        test.checking=false;assert(strcmp(first,status.report));check_report(&status,true);
    }
    kui_media_set(NULL);assert(!fclose(test.image));printf("PASS salvage completion %s\n",kind);return 0;
}
