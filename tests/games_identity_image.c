/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/games_identity.h"
#include "kui/games_storage.h"
#include "kui/hash.h"
#include "kui/media.h"
#include "kui/storage.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static struct {
    FILE *image;
    uint64_t blocks;
    unsigned handles,opens,reads,cancel_polls,cancel_after,fail_read,short_read;
    bool connected,active,cancel_before,fail_close,fail_connect;
} test;
static void log_line(const char *format,...) {
    va_list args;va_start(args,format);vprintf(format,args);va_end(args);putchar('\n');
}
static uint64_t blocks(void *context) {(void)context;return test.blocks;}
static int read_image(void *context,uint32_t block,size_t count,uint8_t *out) {
    (void)context;return fseeko(test.image,(off_t)block*512,SEEK_SET) || fread(out,512,count,test.image)!=count?-1:0;
}
static int write_image(void *context,uint32_t block,size_t count,const uint8_t *data) {
    (void)context;assert(!test.active);
    return fseeko(test.image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,test.image)!=count?-1:0;
}
static int sync_image(void *context) {(void)context;return fflush(test.image)||fsync(fileno(test.image))?-1:0;}
static const struct kui_media_ops media={NULL,blocks,read_image,write_image,sync_image};
unsigned kui_storage_active(void) {return KUI_STORAGE_SCIF;}
bool kui_sd_connect(void) {
    assert(!test.connected);
    if(test.fail_connect)return false;
    test.connected=true;kui_media_set(&media);return true;
}
void kui_sd_disconnect(void) {assert(test.connected && !test.handles);test.connected=false;kui_media_set(NULL);}
static bool cancelled(void) {
    ++test.cancel_polls;
    return test.cancel_before || (test.cancel_after && test.cancel_polls>=test.cancel_after);
}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE mode) {
    if(test.active) {assert(!(mode&FA_WRITE));++test.opens;}
    FRESULT result=__real_f_open(file,path,mode);if(result==FR_OK)++test.handles;return result;
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    FRESULT result=__real_f_close(file);assert(test.handles);--test.handles;
    if(test.active && test.fail_close) {test.fail_close=false;return FR_DISK_ERR;}
    return result;
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *data,UINT count,UINT *got) {
    if(test.active) {
        ++test.reads;
        if(test.fail_read && test.reads>=test.fail_read) {*got=0;return FR_DISK_ERR;}
    }
    FRESULT result=__real_f_read(file,data,count,got);
    if(test.active && test.short_read && test.reads>=test.short_read && result==FR_OK && *got)--*got;
    return result;
}
static void seed(const char *host) {
    char input_path[1024],line[600];assert(snprintf(input_path,sizeof(input_path),"%s/manifest.txt",host)<(int)sizeof(input_path));
    FILE *list=fopen(input_path,"r");assert(list);
    while(fgets(line,sizeof(line),list)) {
        line[strcspn(line,"\n")]=0;assert((line[0]=='D' || line[0]=='F') && line[1]==' ');
        char target[600];assert(snprintf(target,sizeof(target),"0:/%s",line+2)<(int)sizeof(target));
        if(line[0]=='D') {assert(f_mkdir(target)==FR_OK);continue;}
        assert(snprintf(input_path,sizeof(input_path),"%s/%s",host,line+2)<(int)sizeof(input_path));
        FILE *input=fopen(input_path,"rb");assert(input);
        FIL output;assert(f_open(&output,target,FA_WRITE|FA_CREATE_NEW)==FR_OK);
        uint8_t bytes[4096];size_t count;
        while((count=fread(bytes,1,sizeof(bytes),input))) {
            UINT put=0;assert(f_write(&output,bytes,(UINT)count,&put)==FR_OK && put==count);
        }
        assert(!ferror(input) && !fclose(input));assert(f_close(&output)==FR_OK);
    }
    assert(!fclose(list));
}
static struct kui_games_identity verify(const char *path) {
    struct kui_games_identity result;unsigned opens=test.opens;
    assert(kui_games_identity_verify(path,KUI_ZSO_LZ4,&result,log_line,cancelled));
    assert(!test.handles);assert(kui_games_storage_release() && !test.connected);
    assert(test.opens-opens<12u); /* streaming handles, never an open per sector */
    return result;
}
static void refuses(const char *path,enum kui_zso_codec codec) {
    struct kui_games_identity result,before;memset(&result,0xa5,sizeof(result));before=result;
    assert(!kui_games_identity_verify(path,codec,&result,log_line,cancelled));
    assert(!memcmp(&result,&before,sizeof(result)) && !test.handles);
    assert(kui_games_storage_release() && !test.connected);
}
static void check(void) {
    test.active=true;kui_games_storage_hold(true);
    struct kui_games_identity original=verify("/Games/Original/disc.gdi");
    assert(original.bytes==68u*2048u+4u*2352u && original.disc==1 && original.representation==2);
    assert(!strcmp(original.product,"KUI-TEST") && !strcmp(original.version,"V1.000") && !strcmp(original.region,"JUE"));
    struct kui_games_identity cooked=verify("/Games/Cooked/disc.gdi"),renamed=verify("/Games/Renamed/selected.gdi");
    assert(!memcmp(original.digest,cooked.digest,32) && !memcmp(original.digest,renamed.digest,32));
    assert(original.bytes==cooked.bytes && original.bytes==renamed.bytes);
    assert(cooked.representation==1 && renamed.representation==2);
    struct kui_games_identity mixed=verify("/Games/Mixed/disc.gdi");
    assert(mixed.representation==3 && mixed.bytes==original.bytes && !memcmp(mixed.digest,original.digest,32));
    struct kui_games_identity quiet;
    assert(kui_games_identity_verify("/Games/Original/disc.gdi",KUI_ZSO_LZ4,&quiet,NULL,NULL));
    assert(!memcmp(quiet.digest,original.digest,32) && !test.handles && kui_games_storage_release());
    const char *changed[]={"Changed","Audio","Revision","Disc2","Unused"};
    for(unsigned i=0;i<sizeof(changed)/sizeof(changed[0]);i++) {
        char path[128];snprintf(path,sizeof(path),"/Games/%s/disc.gdi",changed[i]);
        struct kui_games_identity other=verify(path);assert(memcmp(original.digest,other.digest,32));
        if(!strcmp(changed[i],"Revision"))assert(!strcmp(other.version,"V1.001"));
        if(!strcmp(changed[i],"Disc2"))assert(other.disc==2);
    }
    struct kui_games_identity iso=verify("/Games/ISO/disc.iso"),cue=verify("/Games/ISO/disc.cue");
    assert(iso.bytes==64u*2048u && iso.representation==1 && cue.representation==1 &&
        !memcmp(iso.digest,cue.digest,32) && memcmp(iso.digest,original.digest,32));
    refuses("/Games/Bad/disc.gdi",KUI_ZSO_LZ4);refuses("/Games/Truncated/disc.gdi",KUI_ZSO_LZ4);
    refuses("/Games/ISO/disc.zso",KUI_ZSO_LZ4);refuses("/Games/Original/disc.gdi",KUI_ZSO_LZO1X);
    const char *unsafe[]={"0:/Games/Original/disc.gdi","/Games/Original/../Original/disc.gdi",
        "/Games//Original/disc.gdi","/Games/Original/", "/Games/Original/disc.gdi/"};
    for(unsigned i=0;i<sizeof(unsafe)/sizeof(unsafe[0]);i++)refuses(unsafe[i],KUI_ZSO_LZ4);
    test.cancel_before=true;unsigned opens=test.opens;refuses("/Games/Original/disc.gdi",KUI_ZSO_LZ4);assert(test.opens==opens);test.cancel_before=false;
    test.cancel_polls=0;test.cancel_after=60;refuses("/Games/Original/disc.gdi",KUI_ZSO_LZ4);test.cancel_after=0;
    test.reads=0;test.fail_read=12;refuses("/Games/Original/disc.gdi",KUI_ZSO_LZ4);test.fail_read=0;
    test.reads=0;test.short_read=12;refuses("/Games/Original/disc.gdi",KUI_ZSO_LZ4);test.short_read=0;
    test.fail_close=true;refuses("/Games/Original/disc.gdi",KUI_ZSO_LZ4);
    test.fail_connect=true;refuses("/Games/Original/disc.gdi",KUI_ZSO_LZ4);test.fail_connect=false;
    char hex[65];kui_hex(original.digest,32,hex);printf("DIGEST original %s\n",hex);kui_hex(iso.digest,32,hex);printf("DIGEST iso %s\n",hex);
    puts("PASS represented identity: Original/cooked/renamed equality, all data/audio/revision changes, ISO/CUE equality, cancellation/fault cleanup; no image writes");
}
int main(int argc,char **argv) {
    assert(argc==4);test.image=fopen(argv[1],"r+b");assert(test.image);
    struct stat info;assert(!fstat(fileno(test.image),&info) && info.st_size>0 && !(info.st_size%512));test.blocks=(uint64_t)info.st_size/512u;
    if(!strcmp(argv[3],"seed")) {
        kui_media_set(&media);FATFS fs;assert(kui_mount(&fs,log_line));seed(argv[2]);assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);
    } else {assert(!strcmp(argv[3],"check"));check();}
    assert(!test.handles && !fclose(test.image));return 0;
}
