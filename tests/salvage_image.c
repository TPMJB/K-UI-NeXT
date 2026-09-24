/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/salvage.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const struct kui_capture_plan plan={.count=3,.bytes=84u*KUI_RAW_BYTES,.tracks={
    {1,4,0,150,190,340},{2,0,0,340,344,344},{3,4,1,45150,45190,45190}}};
static uint8_t original[3][40*KUI_RAW_BYTES];
static struct {
    FILE *image;uint64_t blocks,now;const char *test,*fault;
    bool capturing,injected,repairing,healthy,cancelled;unsigned audio_calls,reads,writes;
    unsigned last_event;bool patch_started;uint32_t repair_order[64];unsigned repair_count;
} test;
static FATFS fs;
static const char *export_dir;
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
static void log_line(const char *fmt,...) {va_list args;va_start(args,fmt);vprintf(fmt,args);va_end(args);puts("");}
static bool fault(const char *name) {return test.capturing && test.fault && !strcmp(test.fault,name) && !test.injected;}
static uint64_t blocks(void *ctx) {(void)ctx;return test.blocks;}
static int image_read(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;return fseeko(test.image,(off_t)block*512,SEEK_SET) || fread(data,512,count,test.image)!=count?-1:0;
}
static int image_write(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;++test.writes;return fseeko(test.image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,test.image)!=count?-1:0;
}
static int image_sync(void *ctx) {(void)ctx;return fflush(test.image) || fsync(fileno(test.image))?-1:0;}
static const struct kui_media_ops media={NULL,blocks,image_read,image_write,image_sync};
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *file,const void *data,UINT bytes,UINT *written) {
    const uint8_t *p=data;
    if(test.capturing && bytes==512 && !memcmp(p,"KUISJNL1",8)) {
        unsigned type=get32(p+16);test.last_event=type;
        const char *kind=type==1?"queue-write":type==2?"commit-write":type==3?"baseline-write":type==4?"ready-write":type==5?"repaired-write":"complete-write";
        if(fault(kind) || (fault("queue-error-cancel") && type==1)) {test.injected=true;if(!strcmp(test.fault,"queue-error-cancel"))test.cancelled=true;*written=0;return FR_DISK_ERR;}
        if(fault("torn-commit") && type==2) {test.injected=true;FRESULT r=__real_f_write(file,data,81,written);assert(r==FR_OK);return FR_DISK_ERR;}
        if(fault("torn-repaired") && type==5) {test.injected=true;FRESULT r=__real_f_write(file,data,81,written);assert(r==FR_OK);return FR_DISK_ERR;}
    }
    if(test.capturing && bytes==8192 && !memcmp(p,"KUISPAT1",8)) {
        test.patch_started=true;
        if(fault("backup-write")) {test.injected=true;*written=0;return FR_DISK_ERR;}
    }
    if(fault("patch-write") && test.repairing && bytes==KUI_RAW_BYTES && test.patch_started) {
        test.injected=true;FRESULT r=__real_f_write(file,data,317,written);assert(r==FR_OK);return FR_DISK_ERR;
    }
    return __real_f_write(file,data,bytes,written);
}
FRESULT __real_f_sync(FIL *);
FRESULT __wrap_f_sync(FIL *file) {
    if((fault("queue-sync") && test.last_event==1) || (fault("baseline-sync") && test.last_event==3) ||
       (fault("backup-sync") && test.patch_started) || (fault("complete-sync") && test.last_event==6)) {
        test.injected=true;return FR_DISK_ERR;
    }
    return __real_f_sync(file);
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *data,UINT bytes,UINT *got) {
    if(fault("readback-fail") && test.repairing && test.patch_started && bytes==KUI_RAW_BYTES) {test.injected=true;*got=0;return FR_DISK_ERR;}
    FRESULT result=__real_f_read(file,data,bytes,got);
    if(fault("header-readback") && bytes==4096 && result==FR_OK) {test.injected=true;((uint8_t *)data)[42]^=1;}
    if(fault("journal-readback") && bytes==512 && result==FR_OK) {test.injected=true;((uint8_t *)data)[100]^=1;}
    if(fault("backup-substitute") && bytes==8192 && result==FR_OK && get32((uint8_t *)data+12)==1) {
        uint8_t *p=data;test.injected=true;p[64+KUI_RAW_BYTES+3]^=1;
        put32(p+28,kui_crc32(0,p+64+KUI_RAW_BYTES,KUI_RAW_BYTES));put32(p+8188,kui_crc32(0,p,8188));
    }
    if(fault("readback-mismatch") && test.repairing && test.patch_started && bytes==KUI_RAW_BYTES && result==FR_OK && *got==bytes) {test.injected=true;((uint8_t *)data)[3]^=1;}
    return result;
}
FRESULT __real_f_rename(const TCHAR *,const TCHAR *);
FRESULT __wrap_f_rename(const TCHAR *old,const TCHAR *next) {
    if((fault("backup-publish") && strstr(next,"patch-")) || (fault("final-publish") && strstr(next,"salvage.json"))) {test.injected=true;return FR_DISK_ERR;}
    return __real_f_rename(old,next);
}
static bool cancelled(void *ctx) {(void)ctx;return test.cancelled;}
static uint64_t now_ms(void *ctx) {(void)ctx;return ++test.now;}
static void progress(void *ctx,const struct kui_salvage_status *status) {
    (void)ctx;assert(status->done<=status->total && status->recovered+status->remaining==status->targets);
    if(!strcmp(test.test,"cancel") && !test.repairing && status->done>=32u*KUI_RAW_BYTES)test.cancelled=true;
}
static enum kui_read_result optical(void *ctx,uint32_t fad,unsigned count,uint8_t *data) {
    (void)ctx;++test.reads;
    unsigned ti=3;for(unsigned i=0;i<3;++i)if(fad>=plan.tracks[i].start && fad<plan.tracks[i].end)ti=i;
    assert(ti<3 && count<=plan.tracks[ti].end-fad);
    bool damaged=(fad<=152 && fad+count>152) || (fad<=342 && fad+count>342);
    if(damaged && !test.healthy && !test.repairing) {
        if(!strcmp(test.test,"fatal"))return KUI_READ_FATAL;
        if(!strcmp(test.test,"bad-parity")) {
            memcpy(data,original[ti]+(fad-plan.tracks[ti].start)*KUI_RAW_BYTES,count*KUI_RAW_BYTES);
            if(ti==0)data[(152-fad)*KUI_RAW_BYTES+2200]^=1;else return KUI_READ_RETRY;
            return KUI_READ_OK;
        }
        return KUI_READ_RETRY;
    }
    memcpy(data,original[ti]+(fad-plan.tracks[ti].start)*KUI_RAW_BYTES,count*KUI_RAW_BYTES);
    if(test.repairing && !strcmp(test.test,"five-passes") && (fad==152 || fad==342)) {assert(test.repair_count<64);test.repair_order[test.repair_count++]=fad;}
    if(test.repairing && fad==342 && (!strcmp(test.test,"audio-mismatch") || !strcmp(test.test,"five-passes"))) {data[3]^=(uint8_t)(++test.audio_calls&1);}
    if(test.repairing && fad==152 && !strcmp(test.test,"wrong-fad")) {memcpy(data,original[0],KUI_RAW_BYTES);}
    if(test.repairing && fad==152 && (!strcmp(test.test,"bad-ecc") || !strcmp(test.test,"five-passes")))data[2200]^=1;
    if(test.repairing && fad==152 && !strcmp(test.test,"repair-fatal"))return KUI_READ_FATAL;
    if(test.repairing && fad==152 && !strcmp(test.test,"repair-stop")) {test.cancelled=true;return KUI_READ_RETRY;}
    return KUI_READ_OK;
}
static void remount(void) {assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(&media);assert(kui_mount(&fs,log_line));}
static void write_file(const char *path,const void *data,UINT bytes) {
    FIL file;UINT put;assert(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    assert(f_write(&file,data,bytes,&put)==FR_OK && put==bytes && f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static size_t load(const char *path,void *data,size_t cap) {
    FIL file;UINT got;assert(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)<=cap);
    size_t size=f_size(&file);assert(f_read(&file,data,(UINT)size,&got)==FR_OK && got==size && f_close(&file)==FR_OK);return size;
}
static void compare_complete(const char *job) {
    remount();uint8_t data[131072];char path[512];
    for(unsigned i=0;i<3;++i) {
        snprintf(path,sizeof(path),"0:%s/track%02u.%s",job,i+1,i==1?"raw":"bin");
        unsigned size=(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES;
        assert(load(path,data,sizeof(data))==size && !memcmp(data,original[i],size));
    }
    snprintf(path,sizeof(path),"0:%s/salvage.json",job);size_t n=load(path,data,sizeof(data)-1);data[n]=0;
    for(unsigned i=0;i<3;++i) {char crc[16];snprintf(crc,sizeof(crc),"%08x",kui_crc32(0,original[i],(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES));assert(strstr((char *)data,crc));}
    if(export_dir) {
        const char *names[]={"header.bin","journal.bin","salvage.gdi","salvage.json","track01.bin","track02.raw","track03.bin","patch-0000.bin","patch-0001.bin"};
        for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i) {
            snprintf(path,sizeof(path),"0:%s/%s",job,names[i]);FILINFO info;if(f_stat(path,&info)==FR_NO_FILE)continue;
            size_t size=load(path,data,sizeof(data));char host[1024];snprintf(host,sizeof(host),"%s/%s",export_dir,names[i]);
            FILE *out=fopen(host,"wb");assert(out && fwrite(data,1,size,out)==size && !fclose(out));
        }
    }
    assert(f_mount(NULL,"0:",0)==FR_OK);
}
int main(int argc,char **argv) {
    if(argc!=4 && argc!=5)return 2;
    if(argc==5)export_dir=argv[4];
    test.test=argv[3];struct stat info;assert(!lstat(argv[1],&info) && S_ISREG(info.st_mode) && !(info.st_size%512));
    test.image=fopen(argv[1],"r+b");assert(test.image);test.blocks=(uint64_t)info.st_size/512;
    for(unsigned i=0;i<3;++i) {char path[1024];snprintf(path,sizeof(path),"%s/track%02u.%s",argv[2],i+1,i==1?"raw":"bin");FILE *in=fopen(path,"rb");assert(in);unsigned size=(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES;assert(fread(original[i],1,size,in)==size && !fclose(in));}
    kui_media_set(&media);
    struct kui_capture_ops ops={.read=optical,.cancelled=cancelled,.now_ms=now_ms,.log=log_line};
    struct kui_salvage_options options={.zero_fill=strcmp(test.test,"zero-off")!=0,.passes=1,.progress=progress};
    if(!strcmp(test.test,"five-passes"))options.passes=5;
    if(!strcmp(test.test,"invalid-limit"))options.passes=51;
    struct kui_salvage_status status;
    bool first_fault=!strncmp(test.test,"queue-",6) || !strcmp(test.test,"commit-write") || !strncmp(test.test,"baseline-",9) || !strcmp(test.test,"ready-write") || !strcmp(test.test,"torn-commit") || !strcmp(test.test,"header-readback") || !strcmp(test.test,"journal-readback");
    bool repair_fault=!strncmp(test.test,"backup-",7) || !strncmp(test.test,"patch-",6) || !strncmp(test.test,"readback-",9) || !strcmp(test.test,"repaired-write") || !strcmp(test.test,"torn-repaired") || !strncmp(test.test,"complete-",9) || !strcmp(test.test,"final-publish");
    test.fault=first_fault?test.test:NULL;test.healthy=!strcmp(test.test,"healthy");test.capturing=true;
    enum kui_salvage_result result=kui_salvage_run(&plan,&ops,&options,KUI_SALVAGE_NEW,&status);
    if(!strcmp(test.test,"invalid-limit")) {assert(result==KUI_SALVAGE_FAILED && !test.reads && !test.writes);goto end;}
    char job[KUI_DEST_JOB_CAP];strcpy(job,status.job);options.job=job;
    if(!strcmp(test.test,"fatal") || !strcmp(test.test,"zero-off")) {
        assert(result==KUI_SALVAGE_FAILED && !status.complete && !status.targets);goto end;
    }
    if(!strcmp(test.test,"healthy")) {assert(result==KUI_SALVAGE_RESOLVED && status.complete && !status.targets);test.capturing=false;compare_complete(job);goto end;}
    if(first_fault || !strcmp(test.test,"cancel")) {
        assert(result==(first_fault?KUI_SALVAGE_FAILED:KUI_SALVAGE_STOPPED));
        assert(!status.complete);if(first_fault)assert(test.injected);
        test.fault=NULL;test.cancelled=false;test.capturing=false;
        test.test="resume-first";test.capturing=true;
        result=kui_salvage_run(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status);
    }
    assert(result==KUI_SALVAGE_UNRESOLVED && status.first_pass_complete && !status.complete && status.targets==2 && status.remaining==2);
    test.capturing=false;
    if(!strcmp(test.test,"latest-wrong-disc")) {
        remount();uint8_t header[4096];char source[512];snprintf(source,sizeof(source),"0:%s/header.bin",job);
        assert(load(source,header,sizeof(header))==sizeof(header));header[32]^=1;put32(header+4092,kui_crc32(0,header,4092));
        assert(f_mkdir("0:/KUI/salvage/job-0002")==FR_OK);write_file("0:/KUI/salvage/job-0002/header.bin",header,sizeof(header));assert(f_mount(NULL,"0:",0)==FR_OK);
    }
    char latest[KUI_DEST_JOB_CAP];assert(kui_salvage_latest(&plan,&ops,latest) && !strcmp(latest,job));
    if(!strcmp(test.test,"corrupt-journal") || !strcmp(test.test,"corrupt-header") || !strcmp(test.test,"wrong-disc") || !strcmp(test.test,"placeholder-changed")) {
        remount();char path[512];uint8_t data[131072];
        if(!strcmp(test.test,"wrong-disc"))original[2][42]^=1;
        else {
            const char *name=!strcmp(test.test,"corrupt-journal")?"journal.bin":!strcmp(test.test,"corrupt-header")?"header.bin":"track01.bin";
            snprintf(path,sizeof(path),"0:%s/%s",job,name);size_t n=load(path,data,sizeof(data));
            size_t at=!strcmp(test.test,"placeholder-changed")?2*KUI_RAW_BYTES:0;data[at]^=1;write_file(path,data,(UINT)n);
        }
        assert(f_mount(NULL,"0:",0)==FR_OK);test.repairing=true;test.capturing=true;
        result=kui_salvage_run(&plan,&ops,&options,KUI_SALVAGE_RECOVER,&status);assert(result==KUI_SALVAGE_FAILED && !status.complete);goto end;
    }
    test.repairing=true;test.capturing=true;test.fault=repair_fault?test.test:NULL;test.injected=false;test.last_event=0;
    result=kui_salvage_run(&plan,&ops,&options,KUI_SALVAGE_RECOVER,&status);
    if(!strcmp(test.test,"five-passes")) {
        assert(result==KUI_SALVAGE_UNRESOLVED && status.remaining==2 && status.pass==5 && test.repair_count==15);
        for(unsigned i=0;i<5;++i) {const uint32_t forward[3]={152,342,342},reverse[3]={342,342,152};assert(!memcmp(test.repair_order+i*3,(i&1)?reverse:forward,sizeof(forward)));}goto end;
    }
    if(!strcmp(test.test,"audio-mismatch") || !strcmp(test.test,"wrong-fad") || !strcmp(test.test,"bad-ecc")) {
        assert(result==KUI_SALVAGE_UNRESOLVED && status.remaining==1 && status.recovered==1 && !status.complete);goto end;
    }
    if(!strcmp(test.test,"repair-fatal") || !strcmp(test.test,"repair-stop")) {assert(result==(!strcmp(test.test,"repair-stop")?KUI_SALVAGE_STOPPED:KUI_SALVAGE_FAILED) && !status.complete && !status.recovered);goto end;}
    if(repair_fault) {
        assert(test.injected && result==KUI_SALVAGE_FAILED && !status.complete);
        test.fault=NULL;test.injected=false;test.last_event=0;test.patch_started=false;
        result=kui_salvage_run(&plan,&ops,&options,KUI_SALVAGE_RECOVER,&status);
    }
    assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.targets==2 && status.recovered==2 && !status.remaining);
    test.capturing=false;compare_complete(job);
    if(!strcmp(test.test,"corrupt-patch")) {
        remount();uint8_t patch[8192];char path[512];snprintf(path,sizeof(path),"0:%s/patch-0000.bin",job);assert(load(path,patch,sizeof(patch))==sizeof(patch));patch[64+KUI_RAW_BYTES+17]^=1;write_file(path,patch,sizeof(patch));assert(f_mount(NULL,"0:",0)==FR_OK);test.capturing=true;
        assert(kui_salvage_run(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status)==KUI_SALVAGE_FAILED && !status.complete);goto end;
    }
    unsigned reads=test.reads;test.capturing=true;
    assert(kui_salvage_run(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status)==KUI_SALVAGE_RESOLVED && status.recovered==2);
    assert(test.reads==reads+2);test.capturing=false;compare_complete(job);
end:
    test.capturing=false;assert(f_mount(NULL,"0:",0)==FR_OK && !fclose(test.image));
    printf("PASS salvage %s\n",argv[3]);return 0;
}
