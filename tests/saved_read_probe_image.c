/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/saved_read_probe.h"
#include "kui/hash.h"
#include "kui/media.h"
#include "kui/probe.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define RECORD_BYTES 5120u
#define RECORDS 64u
static const char folder[]="0:/KUI/salvage/job-0003";
static const char *const names[]={"header.bin","patches.bin","journal.bin","track01.bin","targets.bin",".kui-readback-unstable"};
static const unsigned expected_order[]={2u,3u,0u,1u};
static struct {
    FILE *image;uint64_t blocks,now;const char *fault;bool running,injected;
    bool close_injected,read_detail,secondary_close_detail,primary_close_detail;
    DWORD patches_cluster,header_cluster;unsigned profile,observations,opens,closes,writes;
    unsigned reads[4],bulk[4],sector[4];uint64_t bytes[4];
    unsigned order_count,seen_profiles;
    uint8_t alternate[RECORD_BYTES];size_t largest_allocation;
    const struct kui_saved_read_probe_status *status;
} test;
static FATFS fs;
static bool fault(const char *name) {return test.running && !strcmp(test.fault,name);}
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8u*i));}
static void log_line(const char *format,...) {
    char line[512];va_list args;va_start(args,format);int n=vsnprintf(line,sizeof(line),format,args);va_end(args);
    assert(n>=0 && (size_t)n<sizeof(line));puts(line);
    char expected[160];
    snprintf(expected,sizeof(expected),"SAVED READ I/O: profile=%u slot=0 offset=0 requested=5120 returned=0 FatFs=%u",expected_order[0],(unsigned)FR_DISK_ERR);
    if(!strcmp(line,expected))test.read_detail=true;
    snprintf(expected,sizeof(expected),"SAVED READ close: profile=%u slot=0 offset=0 FatFs=%u secondary=1",expected_order[0],(unsigned)FR_INT_ERR);
    if(!strcmp(line,expected))test.secondary_close_detail=true;
    snprintf(expected,sizeof(expected),"SAVED READ close: profile=0 slot=0 offset=0 FatFs=%u secondary=0",(unsigned)FR_DISK_ERR);
    if(!strcmp(line,expected))test.primary_close_detail=true;
}
static uint64_t blocks(void *ctx) {(void)ctx;return test.blocks;}
static int read_image(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;
    if(fault("mount-fail")) {test.injected=true;return -1;}
    return fseeko(test.image,(off_t)block*512,SEEK_SET) || fread(data,512,count,test.image)!=count?-1:0;
}
static int write_image(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;++test.writes;assert(!test.running);
    return fseeko(test.image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,test.image)!=count?-1:0;
}
static int sync_image(void *ctx) {(void)ctx;return fflush(test.image) || fsync(fileno(test.image))?-1:0;}
static const struct kui_media_ops media={NULL,blocks,read_image,write_image,sync_image};

void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__wrap_malloc(size_t bytes) {
    if(test.running) {assert(bytes<=32768u);if(bytes>test.largest_allocation)test.largest_allocation=bytes;}
    return __real_malloc(bytes);
}
void *__wrap_calloc(size_t count,size_t bytes) {
    assert(!bytes || count<=SIZE_MAX/bytes);
    if(test.running) {assert(count*bytes<=32768u);if(count*bytes>test.largest_allocation)test.largest_allocation=count*bytes;}
    return __real_calloc(count,bytes);
}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE mode) {
    bool patches=strstr(path,"/patches.bin")!=NULL;
    if(test.running)assert(mode==FA_READ);
    if(patches && test.running) {
        ++test.opens;
        if(fault("open-fail")) {test.injected=true;return FR_DISK_ERR;}
    }
    FRESULT result=__real_f_open(file,path,mode);
    if(result==FR_OK && patches && fault("length-change") && test.opens==3) {
        file->obj.objsize+=RECORD_BYTES;test.injected=true;
    }
    return result;
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    bool patches=file->obj.sclust==test.patches_cluster;
    FRESULT result=__real_f_close(file);
    if(test.running && patches) {
        ++test.closes;
        if(fault("close-fail")) {test.injected=true;return FR_DISK_ERR;}
        if(fault("read-fail") && test.injected) {
            test.close_injected=true;return FR_INT_ERR;
        }
    }
    return result;
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *data,UINT bytes,UINT *got) {
    bool patches=test.running && file->obj.sclust==test.patches_cluster;
    FSIZE_t offset=f_tell(file);
    if(patches) {
        unsigned p=test.profile;assert(p<4);
        ++test.reads[p];test.bytes[p]+=bytes;
        assert(((uintptr_t)data&31u)==(p<2?0u:1u));
        if(bytes==RECORD_BYTES)++test.bulk[p];else {assert(bytes==512);++test.sector[p];}
        if(fault("read-fail") && !test.injected) {test.injected=true;*got=0;return FR_DISK_ERR;}
    }
    FRESULT result=__real_f_read(file,data,bytes,got);
    if(test.running && file->obj.sclust==test.header_cluster && fault("header-change") &&
       test.profile==expected_order[1] && result==FR_OK && *got>100) {
        ((uint8_t *)data)[100]^=1;test.injected=true;
    }
    if(patches && result==FR_OK && *got) {
        if(fault("short-read") && !test.injected) {test.injected=true;--*got;}
        if((fault("bulk-transient") && test.profile==0) && !test.injected && offset==0) {
            test.injected=true;((uint8_t *)data)[2417]=0;
        }
        if(fault("valid-different") && test.profile==2 && offset<RECORD_BYTES) {
            UINT n=*got;if((uint64_t)offset+n>RECORD_BYTES)n=(UINT)(RECORD_BYTES-offset);
            memcpy(data,test.alternate+(size_t)offset,n);test.injected=true;
        }
    }
    return result;
}
static bool cancelled(void *ctx) {
    (void)ctx;
    return fault("cancel-before") || (fault("cancel-mid") && test.reads[expected_order[0]]>=2);
}
static uint64_t now_ms(void *ctx) {
    (void)ctx;test.now+=fault("budget")?60000u:1u;return test.now;
}
static void progress(void *ctx,const struct kui_saved_read_probe_status *status) {
    (void)ctx;test.status=status;test.profile=status->profile;
    assert(status->done<=status->total && status->profile<4);
    for(unsigned p=0;p<4;++p)if(status->tried[p] && !(test.seen_profiles&(1u<<p))) {
        assert(test.order_count<4 && p==expected_order[test.order_count]);
        test.seen_profiles|=1u<<p;++test.order_count;
    }
}
static void observe(void *ctx,unsigned profile,uint32_t slot,const char *problem,const uint8_t *record) {
    (void)ctx;assert(profile<4 && slot<RECORDS && problem && record);++test.observations;
    assert(get32(record+8)==slot || !strcmp(test.fault,"bad-index"));
    if(fault("observe-mutation")) {((uint8_t *)record)[3000]^=1;test.injected=true;}
}
static void file_path(char out[KUI_DEST_PATH_CAP],const char *name) {snprintf(out,KUI_DEST_PATH_CAP,"%s/%s",folder,name);}
static void write_file(const char *path,const void *data,UINT bytes) {
    FIL file;UINT put;assert(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    assert(f_write(&file,data,bytes,&put)==FR_OK && put==bytes && f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static void copy_host(const char *host,const char *name) {
    char source[1024],dest[KUI_DEST_PATH_CAP];snprintf(source,sizeof(source),"%s/%s",host,name);file_path(dest,name);
    FILE *input=fopen(source,"rb");assert(input);FIL output;assert(f_open(&output,dest,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    uint8_t data[8192];size_t n;
    while((n=fread(data,1,sizeof(data),input))!=0) {UINT put;assert(f_write(&output,data,(UINT)n,&put)==FR_OK && put==n);}
    assert(!ferror(input) && !fclose(input) && f_sync(&output)==FR_OK && f_close(&output)==FR_OK);
}
static void mutate(const char *name,unsigned offset,bool recalc) {
    char path[KUI_DEST_PATH_CAP];file_path(path,name);FIL file;UINT got;
    assert(f_open(&file,path,FA_READ)==FR_OK);unsigned n=!strcmp(name,"header.bin")?4096:RECORD_BYTES;
    uint8_t data[RECORD_BYTES];assert(f_read(&file,data,n,&got)==FR_OK && got==n && f_close(&file)==FR_OK);
    data[offset]^=1;
    if(recalc) {
        if(n==RECORD_BYTES)put32(data+28,kui_crc32(0,data+2416,2352));
        put32(data+n-4,kui_crc32(0,data,n-4));
    }
    assert(f_open(&file,path,FA_WRITE)==FR_OK);UINT put;
    assert(f_write(&file,data,n,&put)==FR_OK && put==n && f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static void fingerprint(uint8_t digest[32]) {
    struct kui_sha256 sha;kui_sha256_init(&sha);
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i) {
        char path[KUI_DEST_PATH_CAP];file_path(path,names[i]);FIL file;UINT got;
        assert(f_open(&file,path,FA_READ)==FR_OK);
        uint64_t size=f_size(&file);kui_sha256_update(&sha,names[i],strlen(names[i])+1);kui_sha256_update(&sha,&size,sizeof(size));
        uint8_t data[8192];
        do {assert(f_read(&file,data,sizeof(data),&got)==FR_OK);kui_sha256_update(&sha,data,got);}while(got);
        assert(f_close(&file)==FR_OK);
    }
    kui_sha256_digest(&sha,digest);
}
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    for(unsigned step=0;step<4;++step)assert(kui_saved_read_probe_profile_at_step(step)==expected_order[step]);
    assert(kui_saved_read_probe_profile_at_step(4)==KUI_SAVED_READ_PROFILES);
    assert(kui_saved_read_probe_profile_at_step(UINT32_MAX)==KUI_SAVED_READ_PROFILES);
    struct stat st;assert(!stat(argv[1],&st) && st.st_size%512==0);
    test.image=fopen(argv[1],"r+b");assert(test.image);test.blocks=(uint64_t)st.st_size/512;test.fault=argv[3];
    kui_media_set(&media);assert(kui_mount(&fs,log_line));
    assert(f_mkdir("0:/KUI")==FR_OK && f_mkdir("0:/KUI/salvage")==FR_OK && f_mkdir(folder)==FR_OK);
    copy_host(argv[2],"header.bin");copy_host(argv[2],"patches.bin");
    for(unsigned i=2;i<sizeof(names)/sizeof(names[0]);++i) {char path[KUI_DEST_PATH_CAP];file_path(path,names[i]);write_file(path,names[i],(UINT)strlen(names[i]));}
    char alternate_path[1024];snprintf(alternate_path,sizeof(alternate_path),"%s/alternate.bin",argv[2]);FILE *alt=fopen(alternate_path,"rb");assert(alt);
    assert(fread(test.alternate,1,sizeof(test.alternate),alt)==sizeof(test.alternate) && !fclose(alt));
    if(!strcmp(test.fault,"bad-header"))mutate("header.bin",10,false);
    if(!strcmp(test.fault,"header-reserved"))mutate("header.bin",4000,true);
    if(!strcmp(test.fault,"bad-record") || !strcmp(test.fault,"observe-mutation"))mutate("patches.bin",2417,false);
    if(!strcmp(test.fault,"bad-index"))mutate("patches.bin",11,true);
    if(!strcmp(test.fault,"bad-coordinate"))mutate("patches.bin",16,true);
    if(!strcmp(test.fault,"bad-identity"))mutate("patches.bin",32,true);
    if(!strcmp(test.fault,"bad-sector"))mutate("patches.bin",2416+2076,true);
    char path[KUI_DEST_PATH_CAP];file_path(path,"header.bin");FIL file;
    assert(f_open(&file,path,FA_READ)==FR_OK);test.header_cluster=file.obj.sclust;assert(f_close(&file)==FR_OK);
    file_path(path,"patches.bin");assert(f_open(&file,path,FA_READ)==FR_OK);test.patches_cluster=file.obj.sclust;assert(f_close(&file)==FR_OK);
    if(!strcmp(test.fault,"truncated") || !strcmp(test.fault,"empty")) {
        assert(f_open(&file,path,FA_WRITE)==FR_OK);
        assert(f_lseek(&file,!strcmp(test.fault,"empty")?0:RECORD_BYTES*RECORDS-1)==FR_OK && f_truncate(&file)==FR_OK && f_close(&file)==FR_OK);
    }
    uint8_t before[32],after[32];fingerprint(before);assert(f_mount(NULL,"0:",0)==FR_OK);
    unsigned writes=test.writes;struct kui_saved_read_probe_status status;
    const struct kui_saved_read_probe_ops ops={.cancelled=cancelled,.now_ms=now_ms,.progress=progress,.log=log_line,.observe=observe};
    test.running=true;bool result=kui_saved_read_probe_run(folder,&ops,&status);test.running=false;
    assert(test.writes==writes);
    if(!strcmp(test.fault,"clean") || !strcmp(test.fault,"cd-clean")) {
        assert(result && status.complete && !status.failed && !status.inconsistent && !status.stopped && !status.budget_expired);
        assert(status.records==RECORDS && status.done==4u*RECORDS && status.total==status.done);
        for(unsigned p=0;p<4;++p) {
            assert(status.tried[p] && status.full[p] && !status.invalids[p]);
            assert(test.bytes[p]==RECORD_BYTES*RECORDS);
            assert(test.bulk[p]==((p&1u)?0u:RECORDS));assert(test.sector[p]==((p&1u)?10u*RECORDS:0u));
            assert(!memcmp(status.digest[0],status.digest[p],32));
        }
        assert(test.opens>=4 && test.closes==test.opens && !test.observations && test.largest_allocation && test.order_count==4);
    } else if(!strcmp(test.fault,"bulk-transient") || !strcmp(test.fault,"valid-different")) {
        assert(!result && status.complete && status.inconsistent && !status.failed && test.injected);
        if(!strcmp(test.fault,"bulk-transient")) {
            assert(status.invalids[0] && test.observations && status.full[1] && status.full[2] && status.full[3]);
            assert(status.first_bad_seen && status.first_bad_profile==0 && status.first_bad_slot==0);
        }
        else assert(status.full[0] && status.full[2] && memcmp(status.digest[0],status.digest[2],32));
    } else if(!strncmp(test.fault,"cancel-",7))assert(!result && status.stopped && !status.complete);
    else if(!strcmp(test.fault,"budget"))assert(!result && status.budget_expired && !status.complete);
    else if(!strcmp(test.fault,"header-change") || !strcmp(test.fault,"length-change")) {
        assert(!result && status.inconsistent && status.failed && !status.complete && test.injected);
        assert(status.full[expected_order[0]] && !status.full[expected_order[1]] && test.order_count==2);
    }
    else if(!strcmp(test.fault,"observe-mutation")) {
        assert(!result && status.failed && status.inconsistent && !status.complete && test.injected);
        assert(test.observations==1 && status.tried[expected_order[0]] && !status.tried[expected_order[1]]);
        assert(status.first_bad_seen && status.first_bad_profile==expected_order[0] && status.first_bad_slot==0);
    }
    else if(!strcmp(test.fault,"read-fail")) {
        assert(!result && status.failed && !status.complete && test.injected && test.close_injected);
        assert(!strcmp(status.message,"Saved file read failed or was short; comparison stopped"));
        assert(test.read_detail && test.secondary_close_detail && !status.tried[expected_order[1]] && test.order_count==1);
    }
    else if(!strcmp(test.fault,"close-fail")) {
        assert(!result && status.failed && !status.complete && test.injected);
        assert(!strcmp(status.message,"Saved file close failed; comparison incomplete"));
        assert(test.primary_close_detail && !test.read_detail && !test.order_count);
    }
    else if(!strncmp(test.fault,"bad-",4) && strcmp(test.fault,"bad-header")) {
        assert(!result && status.inconsistent && status.complete && test.observations==4);
    } else assert(!result && status.failed && !status.complete);
    if(strstr(test.fault,"fail") || !strcmp(test.fault,"short-read"))assert(test.injected);
    kui_media_set(&media);assert(kui_mount(&fs,log_line));fingerprint(after);assert(!memcmp(before,after,32));
    assert(f_mount(NULL,"0:",0)==FR_OK && !fclose(test.image));
    printf("PASS saved read probe %s; sources unchanged, no probe writes\n",test.fault);return 0;
}
