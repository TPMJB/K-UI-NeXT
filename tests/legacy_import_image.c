/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/legacy_import.h"
#include "kui/media.h"
#include <assert.h>
#define DIR HOST_DIR
#include <dirent.h>
#undef DIR
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static struct {
    FILE *image;uint64_t blocks,writes;bool active;const char *fault;
    unsigned optical_reads,source_reads;DWORD source_cluster;
    uint8_t optical[3][40*KUI_RAW_BYTES],alternate[KUI_RAW_BYTES];
} test;
static FATFS fs;
static void log_line(const char *format,...) {(void)format;}
static uint64_t blocks(void *ctx) {(void)ctx;return test.blocks;}
static int read_image(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;return fseeko(test.image,(off_t)block*512,SEEK_SET) || fread(data,512,count,test.image)!=count?-1:0;
}
static int write_image(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;++test.writes;return fseeko(test.image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,test.image)!=count?-1:0;
}
static int sync_image(void *ctx) {(void)ctx;return fflush(test.image) || fsync(fileno(test.image))?-1:0;}
static const struct kui_media_ops media={NULL,blocks,read_image,write_image,sync_image};
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *f,void *data,UINT bytes,UINT *got) {
    FRESULT r=__real_f_read(f,data,bytes,got);
    if(test.active && !strcmp(test.fault,"source-unstable") && f->obj.sclust==test.source_cluster && bytes>0 && r==FR_OK && *got==bytes && ++test.source_reads>=2)((uint8_t *)data)[0]^=1;
    return r;
}
static bool cancelled(void *ctx) {(void)ctx;return !strcmp(test.fault,"cancel");}
static struct kui_capture_plan plan={.count=3,.bytes=(40u+4u+40u)*KUI_RAW_BYTES,.tracks={
    {1,4,0,150,190,340},{2,0,0,340,344,344},{3,4,1,45150,45190,45190}}};
static enum kui_read_result optical(void *ctx,uint32_t fad,unsigned count,uint8_t *out) {
    (void)ctx;++test.optical_reads;assert(count==1);
    if(!strcmp(test.fault,"fatal"))return KUI_READ_FATAL;
    if(!strcmp(test.fault,"transient") && test.optical_reads<=2)return KUI_READ_RETRY;
    if(!strcmp(test.fault,"invalid-transient") && test.optical_reads<=2) {memset(out,0,KUI_RAW_BYTES);return KUI_READ_OK;}
    for(unsigned i=0;i<plan.count;++i)if(fad>=plan.tracks[i].start && fad<plan.tracks[i].end) {
        if(!strcmp(test.fault,"wrong-disc") && fad==45150)memcpy(out,test.alternate,KUI_RAW_BYTES);
        else memcpy(out,test.optical[i]+(fad-plan.tracks[i].start)*KUI_RAW_BYTES,KUI_RAW_BYTES);
        return KUI_READ_OK;
    }
    assert(false);return KUI_READ_FATAL;
}
static void copy_file(const char *fixture,const char *name) {
    char host[1024],target[384];snprintf(host,sizeof(host),"%s/%s",fixture,name);FILE *f=fopen(host,"rb");assert(f);
    snprintf(target,sizeof(target),"0:/Games/Legacy/%s",name);FIL out;assert(f_open(&out,target,FA_WRITE|FA_CREATE_NEW)==FR_OK);
    uint8_t b[4096];size_t n;while((n=fread(b,1,sizeof(b),f))) {UINT written;assert(f_write(&out,b,(UINT)n,&written)==FR_OK && written==n);}
    assert(!ferror(f) && fclose(f)==0 && f_sync(&out)==FR_OK && f_close(&out)==FR_OK);
}
static void host_bytes(const char *fixture,const char *name,uint8_t *out,size_t bytes) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s",fixture,name);FILE *f=fopen(path,"rb");assert(f && fread(out,1,bytes,f)==bytes && fclose(f)==0);
}
int main(int argc,char **argv) {
    assert(argc==4);test.fault=argv[3];test.image=fopen(argv[1],"r+b");assert(test.image);
    struct stat info;assert(!fstat(fileno(test.image),&info));test.blocks=(uint64_t)info.st_size/512;kui_media_set(&media);
    if(!strncmp(test.fault,"cd-",3)) {
        plan=(struct kui_capture_plan){.count=2,.profile=KUI_CAPTURE_PROFILE_CD_TOC,.bytes=8u*KUI_RAW_BYTES,.tracks={{1,0,0,150,154,154},{2,0,0,154,158,158}}};
        if(!strcmp(test.fault,"cd-mode2"))plan.tracks[0].control=4;
        host_bytes(argv[2],!strcmp(test.fault,"cd-mode2")?"optical01.bin":"optical01.raw",test.optical[0],4u*KUI_RAW_BYTES);host_bytes(argv[2],"optical02.raw",test.optical[1],4u*KUI_RAW_BYTES);
    } else {
        host_bytes(argv[2],"optical01.bin",test.optical[0],40u*KUI_RAW_BYTES);host_bytes(argv[2],"optical02.raw",test.optical[1],4u*KUI_RAW_BYTES);
        host_bytes(argv[2],"optical03.bin",test.optical[2],40u*KUI_RAW_BYTES);host_bytes(argv[2],"alternate.bin",test.alternate,KUI_RAW_BYTES);
    }
    assert(kui_mount(&fs,log_line));assert(f_mkdir("0:/Games")==FR_OK);assert(f_mkdir("0:/Games/Legacy")==FR_OK);
    HOST_DIR *dir=opendir(argv[2]);assert(dir);struct dirent *e;
    while((e=readdir(dir)))if(e->d_name[0]!='.' && strncmp(e->d_name,"optical",7) && strcmp(e->d_name,"alternate.bin"))copy_file(argv[2],e->d_name);
    assert(!closedir(dir));FIL src;if(f_open(&src,plan.profile==KUI_CAPTURE_PROFILE_GD?"0:/Games/Legacy/track03.bin":"0:/Games/Legacy/track01.raw",FA_READ)==FR_OK) {test.source_cluster=src.obj.sclust;assert(f_close(&src)==FR_OK);}
    struct kui_legacy_job *job=malloc(sizeof(*job));assert(job);memset(job,0xa5,sizeof(*job));
    const struct kui_capture_ops ops={.read=optical,.cancelled=cancelled};test.active=true;test.writes=0;
    enum kui_legacy_result result=kui_legacy_import("/Games/Legacy",&plan,&ops,job);
    assert(test.writes==0);test.active=false;
    bool invalid=!strcmp(test.fault,"crc-mismatch") || !strcmp(test.fault,"baseline-corrupt") || !strcmp(test.fault,"baseline-nontarget") ||
        !strcmp(test.fault,"wrong-disc") || !strcmp(test.fault,"fatal") || !strcmp(test.fault,"source-unstable") ||
        !strcmp(test.fault,"no-identity") || !strcmp(test.fault,"bad-map") || !strcmp(test.fault,"bad-suspect") ||
        !strcmp(test.fault,"guard") || !strcmp(test.fault,"state-range") || !strcmp(test.fault,"state-cooked") || !strcmp(test.fault,"cd-silence") || !strcmp(test.fault,"cd-repeated");
    if(invalid) {if(result!=KUI_LEGACY_INVALID)fprintf(stderr,"result %d: %s\n",result,job->message);assert(result==KUI_LEGACY_INVALID);}
    else if(!strcmp(test.fault,"cancel"))assert(result==KUI_LEGACY_STOPPED && !test.optical_reads);
    else if(!strcmp(test.fault,"absent"))assert(result==KUI_LEGACY_ABSENT && !test.optical_reads);
    else {
        if(result!=KUI_LEGACY_READY)fprintf(stderr,"result %d: %s\n",result,job->message);
        assert(result==KUI_LEGACY_READY);
        assert(job->source.plan.profile==plan.profile && job->source.plan.count==plan.count);
        if(!strcmp(test.fault,"partial") || !strcmp(test.fault,"prefix-tail"))assert(job->partial && job->available[2]==20 && job->checkpoint_valid[2]);
        if(!strcmp(test.fault,"missing"))assert(job->partial && !job->available[2]);
        if(!strcmp(test.fault,"maps"))assert(job->suspect_records==1 && job->bad_records==1 && kui_legacy_target_is_marked(job,2,2) && kui_legacy_target_is_marked(job,1,1));
        if(!strcmp(test.fault,"baseline"))assert(kui_legacy_target_is_marked(job,2,2));
        if(!strcmp(test.fault,"audio-baseline"))assert(kui_legacy_target_is_marked(job,1,1));
        if(!strcmp(test.fault,"transient") || !strcmp(test.fault,"invalid-transient"))assert(test.optical_reads==4);
        if(!strncmp(test.fault,"cd-",3))assert(!job->boot_identity && test.optical_reads==4);
        if(plan.profile==KUI_CAPTURE_PROFILE_GD)assert(job->boot_identity);
        assert(kui_legacy_identity_check(job,&ops)==KUI_LEGACY_READY);
        test.fault="cancel";assert(kui_legacy_identity_check(job,&ops)==KUI_LEGACY_STOPPED);
        test.fault="fatal";assert(kui_legacy_identity_check(job,&ops)==KUI_LEGACY_INVALID);
        test.fault=argv[3];
    }
    if(!strcmp(test.fault,"fatal"))assert(test.optical_reads==1);
    free(job);assert(f_mount(NULL,"0:",0)==FR_OK);assert(fclose(test.image)==0);puts("PASS legacy import");return 0;
}
