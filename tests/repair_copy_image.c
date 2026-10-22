/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/repair_copy.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static struct {
    FILE *image;uint64_t blocks,now;const char *fault;
    bool active,injected,cancelled,candidate,evidence;
    unsigned reads,writes,source_reopens;
    DWORD source_clusters[3],copy_cluster;
    uint8_t optical[3][40u*KUI_RAW_BYTES],alternate[40u*KUI_RAW_BYTES];
    struct kui_repair_status *repair;
    struct kui_forensic_status *forensic;
} test;
static FATFS fs;
static const char *folder="0:/Games/Repair Test";
static const char *files[]={"manifest.json","disc.gdi","checkpoint-a.bin","checkpoint-b.bin","track01.bin","track02.raw","track03.bin"};
static struct kui_capture_plan plan={.count=3,.bytes=(40u+4u+40u)*KUI_RAW_BYTES,.tracks={
    {1,4,0,150,190,340},{2,0,0,340,344,344},{3,4,1,45150,45190,45190}}};
static bool fault(const char *s) {return test.active && !strcmp(test.fault,s);}
static void log_line(const char *format,...) {va_list a;va_start(a,format);vprintf(format,a);va_end(a);puts("");}
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
    FSIZE_t offset=f_tell(f);DWORD cluster=f->obj.sclust;
    FRESULT r=__real_f_read(f,data,bytes,got);
    if(r!=FR_OK || *got!=bytes)return r;
    if(fault("source-read-fail") && cluster==test.source_clusters[2]) {test.injected=true;return FR_DISK_ERR;}
    if((fault("source-unstable") || fault("forensic-unstable")) && test.evidence && cluster==test.source_clusters[2] && bytes==KUI_RAW_BYTES && offset==2u*KUI_RAW_BYTES) {
        ((uint8_t *)data)[37]^=1;test.injected=true;++test.source_reopens;
    }
    if(fault("evidence-readback") && bytes==64u+3u*KUI_RAW_BYTES) {((uint8_t *)data)[80]^=1;test.injected=true;}
    if(fault("copy-readback") && test.copy_cluster && cluster==test.copy_cluster) {((uint8_t *)data)[17]^=1;test.injected=true;}
    return r;
}
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *f,const void *data,UINT bytes,UINT *put) {
    if(test.active && bytes==64 && !memcmp(data,"KUIRDB1",7))test.evidence=true;
    if(test.active && bytes==64 && !memcmp(data,"KUIRPT1",7))test.candidate=true;
    if(fault("evidence-write") && bytes==64 && !memcmp(data,"KUIRDB1",7)) {test.injected=true;*put=0;return FR_DISK_ERR;}
    if(fault("copy-write") && bytes>3u*KUI_RAW_BYTES && bytes%KUI_RAW_BYTES==0) {test.injected=true;*put=0;return FR_DISK_ERR;}
    FRESULT r=__real_f_write(f,data,bytes,put);
    if(test.active && bytes>3u*KUI_RAW_BYTES && bytes%KUI_RAW_BYTES==0)test.copy_cluster=f->obj.sclust;
    return r;
}
FRESULT __real_f_sync(FIL *);
FRESULT __wrap_f_sync(FIL *f) {
    if(fault("candidate-sync") && test.candidate) {test.injected=true;return FR_DISK_ERR;}
    return __real_f_sync(f);
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *f) {
    bool inject=fault("copy-close") && test.copy_cluster && f->obj.sclust==test.copy_cluster;
    FRESULT r=__real_f_close(f);if(inject) {test.injected=true;return FR_DISK_ERR;}return r;
}
FRESULT __real_f_rename(const TCHAR *,const TCHAR *);
FRESULT __wrap_f_rename(const TCHAR *from,const TCHAR *to) {
    if((fault("gdi-publish") && strstr(to,"/disc.gdi")) ||
       (fault("report-publish") && strstr(to,"/evidence.txt"))) {test.injected=true;return FR_DISK_ERR;}
    return __real_f_rename(from,to);
}
static bool cancel(void *ctx) {
    (void)ctx;return test.cancelled || fault("cancel-before") ||
        (fault("cancel-copy") && test.repair && test.repair->done>=2u*KUI_RAW_BYTES) ||
        (fault("cancel-forensic") && test.forensic && test.forensic->done>=2u*KUI_RAW_BYTES);
}
static uint64_t now_ms(void *ctx) {(void)ctx;test.now+=10;return test.now;}
static void repair_progress(void *ctx,const struct kui_repair_status *s) {(void)ctx;assert(s->done<=s->total && s->track<=s->tracks);}
static void forensic_progress(void *ctx,const struct kui_forensic_status *s) {(void)ctx;assert(s->done<=s->total && s->track<=s->tracks);}
static enum kui_read_result optical(void *ctx,uint32_t fad,unsigned count,uint8_t *out) {
    (void)ctx;++test.reads;assert(count==1);
    for(unsigned i=0;i<3;++i)if(fad>=plan.tracks[i].start && fad<plan.tracks[i].end) {
        uint32_t at=fad-plan.tracks[i].start;
        if(fault("optical-fatal") && fad==45152) {test.injected=true;return KUI_READ_FATAL;}
        if(fault("optical-retry") && fad==45152) {test.injected=true;return KUI_READ_RETRY;}
        memcpy(out,test.optical[i]+at*KUI_RAW_BYTES,KUI_RAW_BYTES);
        if(fault("wrong-disc")) {out[37]^=1;test.injected=true;}
        if((fault("wrong-disc-valid") || fault("imported-wrong-disc")) && i==2) {
            memcpy(out,test.alternate+at*KUI_RAW_BYTES,KUI_RAW_BYTES);test.injected=true;
        }
        if(fault("optical-parity") && fad==45152) {out[2248]^=1;test.injected=true;}
        if(fault("optical-disagree") && fad==45152 && test.reads%2) {memcpy(out,test.alternate+at*KUI_RAW_BYTES,KUI_RAW_BYTES);test.injected=true;}
        return KUI_READ_OK;
    }
    assert(!"Optical read outside prepared bounds");return KUI_READ_FATAL;
}
static void remount(void) {assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(&media);assert(kui_mount(&fs,log_line));}
static void source_path(char out[KUI_DEST_PATH_CAP],const char *name) {snprintf(out,KUI_DEST_PATH_CAP,"%s/%s",folder,name);}
static void write_file(const char *path,const void *data,UINT n) {
    FIL f;UINT put;assert(f_open(&f,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    assert(f_write(&f,data,n,&put)==FR_OK && put==n);assert(f_sync(&f)==FR_OK && f_close(&f)==FR_OK);
}
static size_t load(const char *path,void *data,size_t cap) {
    FIL f;UINT got;assert(f_open(&f,path,FA_READ)==FR_OK && f_size(&f)<=cap);
    size_t n=(size_t)f_size(&f);assert(f_read(&f,data,(UINT)n,&got)==FR_OK && got==n);assert(f_close(&f)==FR_OK);return n;
}
static size_t load_host(const char *dir,const char *name,void *data,size_t cap) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s",dir,name);FILE *f=fopen(path,"rb");assert(f);
    size_t n=fread(data,1,cap,f);assert(n<=cap && !ferror(f) && !fclose(f));return n;
}
static void mutate(const char *name,unsigned offset,uint8_t value) {
    char path[KUI_DEST_PATH_CAP];uint8_t data[100000];source_path(path,name);size_t n=load(path,data,sizeof(data));assert(offset<n);data[offset]^=value;write_file(path,data,(UINT)n);
}
static void seed(const char *host) {
    assert(f_mkdir("0:/Games")==FR_OK && f_mkdir(folder)==FR_OK);
    for(unsigned i=0;i<7;++i) {
        char path[KUI_DEST_PATH_CAP];uint8_t data[100000];size_t n=load_host(host,files[i],data,sizeof(data));source_path(path,files[i]);write_file(path,data,(UINT)n);
        if(i>=4)memcpy(test.optical[i-4],data,n);
    }
    assert(load_host(host,"alternate03.bin",test.alternate,sizeof(test.alternate))==40u*KUI_RAW_BYTES);
    bool clean=!strcmp(test.fault,"clean") || !strcmp(test.fault,"wrong-disc") || !strcmp(test.fault,"layout") || !strcmp(test.fault,"forensic-clean");
    if(!clean) {
        mutate("track03.bin",2u*KUI_RAW_BYTES+37,1);
        if(strcmp(test.fault,"optical-disagree"))mutate("track01.bin",2248,1);
    }
    if(!strcmp(test.fault,"audio-unresolved"))mutate("track02.raw",17,1);
    if(!strcmp(test.fault,"valid-wrong-sector")) {
        char path[KUI_DEST_PATH_CAP];uint8_t data[100000];source_path(path,"track03.bin");size_t n=load(path,data,sizeof(data));
        memcpy(data+2u*KUI_RAW_BYTES,test.alternate+2u*KUI_RAW_BYTES,KUI_RAW_BYTES);write_file(path,data,(UINT)n);
    }
    if(!strcmp(test.fault,"forensic-limit"))for(unsigned ti=0;ti<3;ti+=2) {
        char path[KUI_DEST_PATH_CAP];uint8_t data[100000];source_path(path,files[4+ti]);size_t n=load(path,data,sizeof(data));
        for(unsigned sector=0;sector<40;++sector)data[sector*KUI_RAW_BYTES+2248]^=2;
        write_file(path,data,(UINT)n);
    }
    if(!strcmp(test.fault,"unsupported"))mutate("track03.bin",2u*KUI_RAW_BYTES+15,3);
    if(!strcmp(test.fault,"truncated")) {
        char path[KUI_DEST_PATH_CAP];source_path(path,"track03.bin");write_file(path,test.optical[2],40u*KUI_RAW_BYTES-1);
    }
    if(strstr(test.fault,"imported")) {
        for(unsigned i=0;i<4;i+=i?1:2) {char path[KUI_DEST_PATH_CAP];source_path(path,files[i]);assert(f_unlink(path)==FR_OK);}
    }
    if(!strcmp(test.fault,"imported-quotes")) {
        char old[KUI_DEST_PATH_CAP],to[KUI_DEST_PATH_CAP];source_path(old,"track02.raw");source_path(to,"Audio Track.raw");assert(f_rename(old,to)==FR_OK);files[5]="Audio Track.raw";
        const char *gdi="3\r\n1 0 4 2352 track01.bin 0\r\n2 190 0 2352 \"Audio Track.raw\" 0\r\n3 45000 4 2352 track03.bin 0\r\n";
        source_path(old,"disc.gdi");write_file(old,gdi,(UINT)strlen(gdi));
    }
    for(unsigned i=0;i<3;++i) {
        char path[KUI_DEST_PATH_CAP];source_path(path,files[4+i]);FIL f;assert(f_open(&f,path,FA_READ)==FR_OK);test.source_clusters[i]=f.obj.sclust;assert(f_close(&f)==FR_OK);
    }
}
static void snapshot(uint32_t crc[7],size_t size[7]) {
    uint8_t data[100000];for(unsigned i=0;i<7;++i) {
        char path[KUI_DEST_PATH_CAP];source_path(path,files[i]);FILINFO info;FRESULT r=f_stat(path,&info);
        if(r==FR_NO_FILE) {size[i]=SIZE_MAX;crc[i]=0;continue;}
        assert(r==FR_OK);size[i]=load(path,data,sizeof(data));crc[i]=kui_crc32(0,data,size[i]);
    }
}
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned check_evidence(const char *path) {
    uint8_t data[64u+3u*KUI_RAW_BYTES];FIL f;assert(f_open(&f,path,FA_READ)==FR_OK);
    size_t n=(size_t)f_size(&f),offset=0;unsigned records=0;
    while(offset<n) {
        UINT got=0;assert(n-offset>=sizeof(data) && f_read(&f,data,sizeof(data),&got)==FR_OK && got==sizeof(data));
        const uint8_t *p=data;
        assert(!memcmp(p,"KUIRDB1",8) || !memcmp(p,"KUIRPT1",8));assert(get32(p+8)==1 && get32(p+12)==++records);
        assert(get32(p+60)==kui_crc32(0,p,60) && get32(p+48)==kui_crc32(0,p+64,3u*KUI_RAW_BYTES));
        for(unsigned i=0;i<3;++i)assert(get32(p+36+i*4)==kui_crc32(0,p+64+i*KUI_RAW_BYTES,KUI_RAW_BYTES));
        offset+=64u+3u*KUI_RAW_BYTES;
    }
    assert(f_close(&f)==FR_OK);
    return records;
}
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    struct stat st;assert(!lstat(argv[1],&st) && S_ISREG(st.st_mode) && !(st.st_size%512));
    test.image=fopen(argv[1],"r+b");assert(test.image);test.blocks=(uint64_t)st.st_size/512;test.fault=argv[3];
    kui_media_set(&media);assert(kui_mount(&fs,log_line));seed(argv[2]);
    uint32_t before[7],after[7];size_t bsize[7],asize[7];snapshot(before,bsize);assert(f_mount(NULL,"0:",0)==FR_OK);
    unsigned writes=test.writes;test.active=true;
    if(!strncmp(test.fault,"forensic",8) || !strcmp(test.fault,"cancel-forensic")) {
        struct kui_forensic_status status;test.forensic=&status;
        const struct kui_forensic_ops ops={NULL,cancel,now_ms,forensic_progress,log_line};
        enum kui_forensic_result r=kui_recovery_forensic(folder,&ops,&status);test.active=false;remount();
        if(!strcmp(test.fault,"cancel-forensic"))assert(r==KUI_FORENSIC_STOPPED && !status.complete);
        else {
            bool unstable=!strcmp(test.fault,"forensic-unstable"),limited=!strcmp(test.fault,"forensic-limit");
            assert(r==(unstable?KUI_FORENSIC_UNSTABLE:KUI_FORENSIC_STABLE) && status.complete && status.done==plan.bytes);
            assert(status.sampled==(!strcmp(test.fault,"forensic-clean")?0u:limited?64u:2u));
            assert(status.limit_reached==limited && (status.unstable!=0)==unstable);
            assert(check_evidence(status.evidence)==status.sampled);
        }
        assert(!test.reads);
    } else {
        if(!strcmp(test.fault,"layout"))++plan.tracks[2].start;
        const struct kui_capture_ops ops={.read=optical,.cancelled=cancel,.now_ms=now_ms,.log=log_line};
        const struct kui_repair_options options={.attempts=5,.progress=repair_progress};
        struct kui_repair_status status;test.repair=&status;
        enum kui_repair_result result=kui_repair_copy(folder,&plan,&ops,&options,&status);test.active=false;remount();
        bool success=!strcmp(test.fault,"clean") || !strcmp(test.fault,"repair") || !strcmp(test.fault,"imported") ||
            !strcmp(test.fault,"imported-quotes") || !strcmp(test.fault,"repeat");
        bool unresolved=!strcmp(test.fault,"audio-unresolved") || !strcmp(test.fault,"optical-retry") || !strcmp(test.fault,"optical-parity") ||
            !strcmp(test.fault,"optical-disagree") || !strcmp(test.fault,"unsupported") || !strcmp(test.fault,"valid-wrong-sector");
        bool stopped=!strcmp(test.fault,"cancel-before") || !strcmp(test.fault,"cancel-copy");
        assert(result==(success?KUI_REPAIR_COMPLETE:unresolved?KUI_REPAIR_UNRESOLVED:stopped?KUI_REPAIR_STOPPED:KUI_REPAIR_FAILED));
        assert(status.complete==success);
        if(status.job[0]) {
            char path[KUI_DEST_PATH_CAP];FILINFO info;snprintf(path,sizeof(path),"0:%s/disc.gdi",status.job);
            assert(f_stat(path,&info)==(success?FR_OK:FR_NO_FILE));
            if(success) {
                for(unsigned i=0;i<3;++i) {
                    uint8_t data[100000];snprintf(path,sizeof(path),"0:%s/track%02u.%s",status.job,i+1,i==1?"raw":"bin");
                    size_t n=load(path,data,sizeof(data));assert(n==(size_t)(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES);
                    assert(!memcmp(data,test.optical[i],n));
                }
                snprintf(path,sizeof(path),"0:%s/samples.bin",status.job);assert(check_evidence(path)==(status.suspects*2u));
                assert(status.done==plan.bytes && !status.unresolved);
                assert(status.repaired==(!strcmp(test.fault,"clean")?0u:2u));
            }
        }
        if(!strcmp(test.fault,"source-unstable"))assert(status.storage_unstable && test.source_reopens);
        if(!strncmp(test.fault,"wrong-disc",10) || !strcmp(test.fault,"imported-wrong-disc") || !strcmp(test.fault,"layout") || !strcmp(test.fault,"truncated"))assert(!status.job[0]);
        if(!strcmp(test.fault,"cancel-before"))assert(test.writes==writes && !test.reads && !status.job[0]);
        if(!strcmp(test.fault,"repeat")) {
            char job[KUI_DEST_JOB_CAP];snprintf(job,sizeof(job),"%s",status.job);assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;
            assert(kui_repair_copy(folder,&plan,&ops,&options,&status)==KUI_REPAIR_COMPLETE && strcmp(job,status.job));test.active=false;remount();
        }
    }
    if(strstr(test.fault,"write") || strstr(test.fault,"readback") || strstr(test.fault,"sync") || strstr(test.fault,"close") ||
       strstr(test.fault,"publish") || strstr(test.fault,"unstable") || strstr(test.fault,"wrong-disc") ||
       !strcmp(test.fault,"source-read-fail") || !strcmp(test.fault,"optical-fatal") || !strcmp(test.fault,"optical-retry") ||
       !strcmp(test.fault,"optical-parity") || !strcmp(test.fault,"optical-disagree"))assert(test.injected);
    snapshot(after,asize);assert(!memcmp(before,after,sizeof(before)) && !memcmp(bsize,asize,sizeof(bsize)));
    assert(f_mount(NULL,"0:",0)==FR_OK && !fclose(test.image));printf("PASS repair copy %s; originals unchanged\n",argv[3]);return 0;
}
