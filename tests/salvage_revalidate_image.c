/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/salvage_revalidate.h"
#include "kui/recovery_guard.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char folder[]="0:/KUI/salvage/job-0003";
static const char *const source_names[]={"header.bin","journal.bin","patches.bin","track01.bin","track02.raw","track03.bin","targets.cache"};
static const char canary_path[]="0:/KUI/probes/salvage-recheck-0001.idx";
static const char guard_name[]=".kui-readback-unstable";
enum tag { UNKNOWN,HEADER,JOURNAL,PATCHES,TRACK1,TRACK2,TRACK3,CACHE,INDEX,GUARD,CANARY };
static struct {
    FILE *image;uint64_t blocks,now;const char *fault;bool running,injected,guard_unlinked,mark_failed;
    unsigned optical_reads,source_reads,source_writes,source_unlinks,index_creates,index_unlinks;
    unsigned zero_reads[12],opens[12],closes[12];
    struct {FIL *file;enum tag tag;} handles[32];
    struct kui_salvage_revalidate_status last;
} test;
static FATFS fs;
static struct kui_capture_plan plan;
static bool fault(const char *name) {return test.running && !strcmp(test.fault,name);}
static bool source(enum tag tag) {return tag>=HEADER && tag<=CACHE;}
static void log_line(const char *format,...) {va_list args;va_start(args,format);vprintf(format,args);va_end(args);putchar('\n');}
static enum tag path_tag(const char *path) {
    if(!strcmp(path,canary_path))return CANARY;
    for(unsigned i=0;i<sizeof(source_names)/sizeof(source_names[0]);++i) {
        char full[KUI_DEST_PATH_CAP];snprintf(full,sizeof(full),"%s/%s",folder,source_names[i]);
        if(!strcmp(path,full))return (enum tag)(HEADER+i);
    }
    if(strstr(path,".kui-readback-unstable"))return GUARD;
    if(strstr(path,"/KUI/probes/salvage-recheck-"))return INDEX;
    return UNKNOWN;
}
static enum tag file_tag(FIL *file) {
    for(unsigned i=0;i<32;++i)if(test.handles[i].file==file)return test.handles[i].tag;
    return UNKNOWN;
}
static void remember(FIL *file,enum tag tag) {
    unsigned i=0;while(i<32 && test.handles[i].file && test.handles[i].file!=file)++i;
    assert(i<32);test.handles[i].file=file;test.handles[i].tag=tag;
}
static void forget(FIL *file) {for(unsigned i=0;i<32;++i)if(test.handles[i].file==file)test.handles[i].file=NULL;}
static uint64_t blocks(void *ctx) {(void)ctx;return test.blocks;}
static int read_image(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;if(fault("mount-fail")) {test.injected=true;return -1;}
    return fseeko(test.image,(off_t)block*512,SEEK_SET) || fread(data,512,count,test.image)!=count?-1:0;
}
static int write_image(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;return fseeko(test.image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,test.image)!=count?-1:0;
}
static int sync_image(void *ctx) {
    (void)ctx;
    if(fault("guard-sync-fail") && test.guard_unlinked && !test.injected) {test.injected=true;return -1;}
    return fflush(test.image) || fsync(fileno(test.image))?-1:0;
}
static const struct kui_media_ops media={NULL,blocks,read_image,write_image,sync_image};

FRESULT __real_f_mount(FATFS *,const TCHAR *,BYTE);
FRESULT __wrap_f_mount(FATFS *filesystem,const TCHAR *path,BYTE immediate) {
    FRESULT result=__real_f_mount(filesystem,path,immediate);
    if(!filesystem && fault("final-unmount-fail") && test.guard_unlinked && !test.injected) {
        test.injected=true;return FR_DISK_ERR;
    }
    return result;
}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE mode) {
    enum tag tag=path_tag(path);
    if(test.running && source(tag))assert(mode==FA_READ);
    if(test.running && tag==CANARY && (mode&FA_WRITE))assert(mode&FA_CREATE_NEW);
    if(test.running && tag==INDEX && (mode&FA_WRITE)) {
        assert(mode&FA_CREATE_NEW);++test.index_creates;
    }
    if(fault("open-fail") && tag==PATCHES && !test.injected) {test.injected=true;return FR_DISK_ERR;}
    if(fault("no-guard-track-repeat-mark-fail") && tag==GUARD && (mode&FA_WRITE) && !test.mark_failed) {
        test.mark_failed=true;return FR_DISK_ERR;
    }
    FRESULT result=__real_f_open(file,path,mode);
    if(result==FR_OK) {assert(!test.running || tag!=CANARY || !(mode&FA_WRITE));remember(file,tag);if(test.running)++test.opens[tag];}
    return result;
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    enum tag tag=file_tag(file);FRESULT result=__real_f_close(file);forget(file);
    if(test.running)++test.closes[tag];
    if(!test.injected && ((fault("close-fail") && tag==TRACK3) || (fault("index-close-fail") && tag==INDEX))) {
        test.injected=true;return FR_DISK_ERR;
    }
    return result;
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *data,UINT bytes,UINT *got) {
    enum tag tag=file_tag(file);FSIZE_t at=f_tell(file);
    if(test.running && source(tag))++test.source_reads;
    if(!test.injected && ((fault("read-fail") && tag==PATCHES) || (fault("index-read-fail") && tag==INDEX))) {
        test.injected=true;*got=0;return FR_DISK_ERR;
    }
    FRESULT result=__real_f_read(file,data,bytes,got);
    if(test.running && result==FR_OK && *got) {
        if(at==0)++test.zero_reads[tag];
        bool repeat=(fault("header-repeat") && tag==HEADER) || (fault("journal-repeat") && tag==JOURNAL) ||
            (fault("patch-repeat") && tag==PATCHES) ||
            ((fault("track-repeat") || fault("no-guard-track-repeat") || fault("no-guard-track-repeat-mark-fail")) && tag==TRACK3);
        if(repeat && !test.injected && at==0 && test.zero_reads[tag]>=2) {
            ((uint8_t *)data)[*got>17?17:0]^=1;test.injected=true;
        }
        if(fault("short-read") && tag==TRACK3 && !test.injected) {--*got;test.injected=true;}
    }
    return result;
}
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *file,const void *data,UINT bytes,UINT *put) {
    enum tag tag=file_tag(file);
    if(test.running && source(tag)) {++test.source_writes;assert(false);}
    if(test.running && tag==CANARY)assert(false);
    if(!test.injected && tag==INDEX && (fault("index-write-fail") || fault("index-short-write"))) {
        test.injected=true;*put=0;return fault("index-write-fail")?FR_DISK_ERR:FR_OK;
    }
    return __real_f_write(file,data,bytes,put);
}
FRESULT __real_f_stat(const TCHAR *,FILINFO *);
FRESULT __wrap_f_stat(const TCHAR *path,FILINFO *info) {
    if(fault("stat-fail") && path_tag(path)==GUARD && !test.injected) {test.injected=true;return FR_DISK_ERR;}
    return __real_f_stat(path,info);
}
FRESULT __real_f_unlink(const TCHAR *);
FRESULT __wrap_f_unlink(const TCHAR *path) {
    enum tag tag=path_tag(path);
    if(test.running) {
        if(source(tag)) {++test.source_unlinks;assert(false);}
        assert(tag!=CANARY);
        if(!test.injected && ((fault("guard-unlink-fail") && tag==GUARD) || (fault("index-unlink-fail") && tag==INDEX))) {
            test.injected=true;return FR_DISK_ERR;
        }
        if(tag==GUARD)assert(test.last.phase==KUI_SALVAGE_REVALIDATE_FINAL && !test.last.failed && !test.last.stopped);
    }
    FRESULT result=__real_f_unlink(path);
    if(test.running && result==FR_OK) {
        if(tag==GUARD)test.guard_unlinked=true;
        if(tag==INDEX)++test.index_unlinks;
    }
    return result;
}
static bool cancelled(void *ctx) {
    (void)ctx;
    return fault("cancel-before") || (fault("cancel-journal") && test.last.phase==KUI_SALVAGE_REVALIDATE_JOURNAL) ||
        (fault("cancel-candidates") && test.last.phase==KUI_SALVAGE_REVALIDATE_CANDIDATES) ||
        (fault("cancel-tracks") && test.last.phase==KUI_SALVAGE_REVALIDATE_TRACKS) ||
        (fault("cancel-final") && test.last.phase==KUI_SALVAGE_REVALIDATE_FINAL);
}
static uint64_t now_ms(void *ctx) {(void)ctx;return ++test.now;}
static enum kui_read_result optical_read(void *ctx,uint32_t fad,unsigned n,uint8_t *out) {
    (void)ctx;(void)fad;(void)n;(void)out;++test.optical_reads;assert(false);return KUI_READ_FATAL;
}
static void progress(void *ctx,const struct kui_salvage_revalidate_status *status) {
    (void)ctx;test.last=*status;
    assert(status->track<=status->tracks);
    if(status->bytes_total)assert(status->bytes_done<=status->bytes_total);
}
static void file_path(char path[KUI_DEST_PATH_CAP],const char *name) {snprintf(path,KUI_DEST_PATH_CAP,"%s/%s",folder,name);}
static void write_file(const char *path,const void *bytes,UINT length) {
    FIL file;UINT put;assert(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    assert(f_write(&file,bytes,length,&put)==FR_OK && put==length && f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static void read_marker(const char *path,uint8_t out[512]) {
    FIL file;UINT got;assert(f_open(&file,path,FA_READ)==FR_OK);
    assert(f_size(&file)==512 && f_read(&file,out,512,&got)==FR_OK && got==512 && f_close(&file)==FR_OK);
}
static void copy_host(const char *directory,const char *name) {
    char host[1024],path[KUI_DEST_PATH_CAP];snprintf(host,sizeof(host),"%s/%s",directory,name);
    FILE *input=fopen(host,"rb");if(!input)return;file_path(path,name);
    FIL output;assert(f_open(&output,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    uint8_t data[8192];size_t n;
    while((n=fread(data,1,sizeof(data),input))) {UINT put;assert(f_write(&output,data,(UINT)n,&put)==FR_OK && put==n);}
    assert(!ferror(input) && !fclose(input) && f_sync(&output)==FR_OK && f_close(&output)==FR_OK);
}
static void hash_file(struct kui_sha256 *sha,const char *path) {
    FILINFO info;FRESULT r=f_stat(path,&info);bool exists=r==FR_OK;assert(r==FR_OK || r==FR_NO_FILE);
    kui_sha256_update(sha,path,strlen(path)+1);kui_sha256_update(sha,&exists,sizeof(exists));if(!exists)return;
    FIL file;assert(f_open(&file,path,FA_READ)==FR_OK);uint64_t size=f_size(&file);kui_sha256_update(sha,&size,sizeof(size));
    uint8_t data[8192];UINT got;
    do {assert(f_read(&file,data,sizeof(data),&got)==FR_OK);kui_sha256_update(sha,data,got);}while(got);
    assert(f_close(&file)==FR_OK);
}
static void fingerprint(uint8_t digest[32],bool guards) {
    struct kui_sha256 sha;kui_sha256_init(&sha);
    if(guards) {
        char path[KUI_DEST_PATH_CAP];file_path(path,guard_name);hash_file(&sha,path);
        file_path(path,".kui-readback-unstable.part");hash_file(&sha,path);
    } else {
        for(unsigned i=0;i<sizeof(source_names)/sizeof(source_names[0]);++i) {char path[KUI_DEST_PATH_CAP];file_path(path,source_names[i]);hash_file(&sha,path);}
        hash_file(&sha,canary_path);
    }
    kui_sha256_digest(&sha,digest);
}
static void prepare_guard(void) {
    char path[KUI_DEST_PATH_CAP],part[KUI_DEST_PATH_CAP];file_path(path,guard_name);file_path(part,".kui-readback-unstable.part");
    assert(kui_recovery_guard_mark(folder,log_line));
    if(!strcmp(test.fault,"no-guard") || strstr(test.fault,"no-guard-track-repeat")==test.fault)assert(f_unlink(path)==FR_OK);
    if(!strcmp(test.fault,"part-only") || !strcmp(test.fault,"torn-part") || !strcmp(test.fault,"foreign-part")) {
        assert(f_rename(path,part)==FR_OK);
        if(!strcmp(test.fault,"torn-part"))write_file(part,"torn",4);
    }
    if(!strcmp(test.fault,"foreign-guard") || !strcmp(test.fault,"foreign-part")) {
        assert(f_mkdir("0:/KUI/salvage/job-0004")==FR_OK && kui_recovery_guard_mark("0:/KUI/salvage/job-0004",log_line));
        uint8_t data[512];read_marker("0:/KUI/salvage/job-0004/.kui-readback-unstable",data);
        write_file(!strcmp(test.fault,"foreign-part")?part:path,data,sizeof(data));
    }
    if(!strcmp(test.fault,"guard-crc")) {uint8_t data[512];read_marker(path,data);data[100]^=1;write_file(path,data,sizeof(data));}
}
static void prepare_plan(bool cd) {
    plan=(struct kui_capture_plan){.count=3,.bytes=(16u+4u+1024u)*KUI_RAW_BYTES,.profile=cd?KUI_CAPTURE_PROFILE_CD_TOC:KUI_CAPTURE_PROFILE_GD,
        .tracks={{1,4,0,150,166,600},{2,0,0,600,604,1825},{3,4,1,45150,46174,549300}}};
    if(cd) {
        plan.tracks[0].toc_end=166;plan.tracks[1]=(struct kui_capture_track){2,0,0,166,170,170};
        plan.tracks[2]=(struct kui_capture_track){3,4,0,170,1194,1194};
    }
}
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    test.fault=argv[3];prepare_plan(!strcmp(test.fault,"cd-clean"));
    struct stat st;assert(!stat(argv[1],&st) && st.st_size%512==0);
    test.image=fopen(argv[1],"r+b");assert(test.image);test.blocks=(uint64_t)st.st_size/512;
    kui_media_set(&media);assert(kui_mount(&fs,log_line));
    assert(f_mkdir("0:/KUI")==FR_OK && f_mkdir("0:/KUI/salvage")==FR_OK && f_mkdir(folder)==FR_OK && f_mkdir("0:/KUI/probes")==FR_OK);
    for(unsigned i=0;i<sizeof(source_names)/sizeof(source_names[0]);++i)copy_host(argv[2],source_names[i]);
    write_file(canary_path,"EXISTING PROBE MUST NOT BE REUSED",32);prepare_guard();
    uint8_t before[32],after[32],guard_before[32],guard_after[32];fingerprint(before,false);fingerprint(guard_before,true);
    assert(f_mount(NULL,"0:",0)==FR_OK);
    struct kui_salvage_revalidate_status status;
    const struct kui_capture_ops ops={.read=optical_read,.cancelled=cancelled,.now_ms=now_ms,.log=log_line};
    test.running=true;bool result=kui_salvage_revalidate(&plan,&ops,folder,progress,&status);test.running=false;
    bool good=!strcmp(test.fault,"clean") || !strcmp(test.fault,"cd-clean") || !strcmp(test.fault,"clean-zero-repairs") || !strcmp(test.fault,"no-guard") ||
        !strcmp(test.fault,"part-only") || !strcmp(test.fault,"torn-part");
    if(good) {
        if(!result)fprintf(stderr,"Unexpected revalidation failure: %s\n",status.message);
        assert(result && status.complete && !status.failed && !status.stopped && status.guard_cleared);
        unsigned repaired=!strcmp(test.fault,"clean-zero-repairs")?0u:173u;
        assert(status.targets==1000 && status.recovered==repaired && status.candidates==repaired);
        assert(status.bytes_total && status.bytes_done==status.bytes_total && test.index_creates && test.index_unlinks==test.index_creates);
    } else {
        if(result)fprintf(stderr,"Unexpected acceptance: %s\n",test.fault);
        assert(!result && !status.complete && !status.guard_cleared);
        if(!strncmp(test.fault,"cancel-",7))assert(status.stopped && !status.failed);
        else assert(status.failed);
    }
    assert(!test.optical_reads && !test.source_writes && !test.source_unlinks);
    if(!strcmp(test.fault,"guard-sync-fail") || !strcmp(test.fault,"final-unmount-fail")) {
        assert(test.injected && test.guard_unlinked);
        assert(!kui_recovery_guard_check(folder,log_line) && !kui_recovery_guard_clear(folder,log_line));
    }
    kui_media_set(&media);assert(kui_mount(&fs,log_line));fingerprint(after,false);assert(!memcmp(before,after,32));
    if(good)assert(kui_recovery_guard_check(folder,log_line));
    else {
        assert(!kui_recovery_guard_check(folder,log_line));
        if(!strcmp(test.fault,"no-guard-track-repeat"))assert(kui_recovery_guard_validate(folder,log_line));
        else if(!strcmp(test.fault,"no-guard-track-repeat-mark-fail")) {
            assert(test.mark_failed && !kui_recovery_guard_validate(folder,log_line) && !kui_recovery_guard_clear(folder,log_line));
        } else if(strcmp(test.fault,"guard-sync-fail") && strcmp(test.fault,"final-unmount-fail")) {
            fingerprint(guard_after,true);assert(!memcmp(guard_before,guard_after,32));
        }
    }
    if(strstr(test.fault,"repeat") || strstr(test.fault,"-fail") || !strcmp(test.fault,"short-read") || !strcmp(test.fault,"index-short-write"))assert(test.injected);
    assert(f_mount(NULL,"0:",0)==FR_OK && !fclose(test.image));
    printf("PASS salvage revalidate %s; source files unchanged\n",test.fault);return 0;
}
