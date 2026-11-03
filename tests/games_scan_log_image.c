/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/games_scan_log.h"
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const paths[]={"0:/KUI/games-library/scan-a.txt","0:/KUI/games-library/scan-b.txt"};
static const char first[]="build=test-build\nphase=before grouping\nroot=/Games\npath=/Games/Shenmue (2)/disc.gdi\n";
static const char second[]="build=test-build\nphase=after grouping\nroot=/Games\npath=/Games/Shenmue (3)/disc.gdi\n";
static struct {
    FILE *image;
    LBA_t blocks;
    bool active,fail_read,fail_write,short_write,fail_sync,fail_close,fail_verify;
    unsigned files,writes,write_opens,mkdirs,logs;
    struct {FIL *file;bool write;} opened[8];
    char shown[4096];
    size_t shown_bytes;
} test;
static FATFS fs;
static void logger(const char *format,...) {
    va_list args;va_start(args,format);char line[256];int bytes=vsnprintf(line,sizeof(line),format,args);va_end(args);
    assert(bytes>=0 && bytes<(int)sizeof(line));++test.logs;
    assert(test.shown_bytes+(size_t)bytes+1u<sizeof(test.shown));
    memcpy(test.shown+test.shown_bytes,line,(size_t)bytes);test.shown_bytes+=(size_t)bytes;
    test.shown[test.shown_bytes++]='\n';test.shown[test.shown_bytes]=0;
}
DSTATUS disk_initialize(BYTE drive) {return drive || !test.image?STA_NOINIT:0;}
DSTATUS disk_status(BYTE drive) {return disk_initialize(drive);}
static bool range(BYTE drive,LBA_t start,UINT count) {
    return !drive && test.image && count && start<test.blocks && count<=test.blocks-start &&
        !fseeko(test.image,(off_t)start*512,SEEK_SET);
}
DRESULT disk_read(BYTE drive,BYTE *out,LBA_t start,UINT count) {
    if(!range(drive,start,count))return RES_PARERR;
    return fread(out,512,count,test.image)==count?RES_OK:RES_ERROR;
}
DRESULT disk_write(BYTE drive,const BYTE *in,LBA_t start,UINT count) {
    if(!range(drive,start,count))return RES_PARERR;
    return fwrite(in,512,count,test.image)==count?RES_OK:RES_ERROR;
}
DRESULT disk_ioctl(BYTE drive,BYTE operation,void *out) {
    if(drive || !test.image)return RES_NOTRDY;
    switch(operation) {
    case CTRL_SYNC:return !fflush(test.image) && !fsync(fileno(test.image))?RES_OK:RES_ERROR;
    case GET_SECTOR_COUNT:*(LBA_t *)out=test.blocks;return RES_OK;
    case GET_SECTOR_SIZE:*(WORD *)out=512;return RES_OK;
    case GET_BLOCK_SIZE:*(DWORD *)out=1;return RES_OK;
    default:return RES_PARERR;
    }
}
DWORD get_fattime(void) {return ((2026u-1980u)<<25)|(11u<<21)|(3u<<16);}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE flags) {
    if(test.active && (flags&FA_WRITE)) {
        assert(!strcmp(path,paths[0]) || !strcmp(path,paths[1]));++test.write_opens;
    }
    FRESULT result=__real_f_open(file,path,flags);
    if(test.active && result==FR_OK) {
        unsigned i=0;for(;i<8u && test.opened[i].file;i++);assert(i<8u);
        test.opened[i].file=file;test.opened[i].write=(flags&FA_WRITE)!=0;++test.files;
    }
    return result;
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    bool write=false;
    if(test.active) {
        unsigned i=0;for(;i<8u && test.opened[i].file!=file;i++);assert(i<8u && test.files);
        write=test.opened[i].write;test.opened[i].file=NULL;test.opened[i].write=false;--test.files;
    }
    FRESULT result=__real_f_close(file);
    return test.active && test.fail_close && write?FR_DISK_ERR:result;
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *out,UINT bytes,UINT *got) {
    if(test.active && (test.fail_read || (test.fail_verify && test.write_opens))) {*got=0;return FR_DISK_ERR;}
    return __real_f_read(file,out,bytes,got);
}
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *file,const void *data,UINT bytes,UINT *put) {
    if(test.active) {
        ++test.writes;
        if(test.fail_write) {*put=0;return FR_DISK_ERR;}
        if(test.short_write && bytes>1u)bytes/=2u;
    }
    return __real_f_write(file,data,bytes,put);
}
FRESULT __real_f_sync(FIL *);
FRESULT __wrap_f_sync(FIL *file) {
    FRESULT result=__real_f_sync(file);
    return test.active && test.fail_sync?FR_DISK_ERR:result;
}
FRESULT __real_f_mkdir(const TCHAR *);
FRESULT __wrap_f_mkdir(const TCHAR *path) {
    if(test.active) {assert(!strcmp(path,"0:/KUI") || !strcmp(path,"0:/KUI/games-library"));++test.mkdirs;}
    return __real_f_mkdir(path);
}
static void reboot(void) {
    assert(!test.files);assert(f_mount(NULL,"0:",0)==FR_OK);
    memset(&fs,0,sizeof(fs));assert(f_mount(&fs,"0:",1)==FR_OK);
}
static void reset_faults(void) {
    test.fail_read=test.fail_write=test.short_write=test.fail_sync=test.fail_close=test.fail_verify=false;
    test.writes=test.write_opens=0;
}
static void loaded(const char *expected,size_t bytes) {
    char out[KUI_GAMES_SCAN_LOG_TEXT_MAX+1u];memset(out,0xa5,sizeof(out));size_t got=9999;
    assert(kui_games_scan_log_load(out,sizeof(out),&got,logger) && got==bytes &&
        !memcmp(out,expected,bytes) && !out[bytes] && !test.files);
}
static void refused_load(void) {
    char out[32]="unchanged";size_t bytes=123;
    assert(!kui_games_scan_log_load(out,sizeof(out),&bytes,logger) && !strcmp(out,"unchanged") && bytes==123 && !test.files);
}
static size_t raw(unsigned which,uint8_t out[KUI_GAMES_SCAN_LOG_WIRE_MAX+1u]) {
    assert(!test.files);bool active=test.active;test.active=false;FIL file;
    assert(f_open(&file,paths[which],FA_READ)==FR_OK);size_t bytes=(size_t)f_size(&file);UINT got=0;
    assert(bytes<=KUI_GAMES_SCAN_LOG_WIRE_MAX && f_read(&file,out,(UINT)bytes,&got)==FR_OK && got==bytes && f_close(&file)==FR_OK);
    out[bytes]=0;test.active=active;return bytes;
}
static void replace(unsigned which,const uint8_t *data,size_t bytes) {
    assert(!test.files);bool active=test.active;test.active=false;FIL file;UINT put=0;
    assert(f_open(&file,paths[which],FA_WRITE|FA_CREATE_ALWAYS)==FR_OK &&
        f_write(&file,data,(UINT)bytes,&put)==FR_OK && put==bytes && f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
    test.active=active;
}
static void sequence(uint8_t *wire,size_t bytes,const char *hex) {
    char *at=strstr((char *)wire,"Sequence: ");assert(at && strlen(hex)==16u);memcpy(at+10u,hex,16u);
    assert(bytes>=17u && !memcmp(wire+bytes-17u,"\nCRC32: ",8u));
    uint32_t crc=kui_crc32(0,wire,bytes-17u);
    char encoded[9];assert(snprintf(encoded,sizeof(encoded),"%08x",crc)==8);memcpy(wire+bytes-9u,encoded,8u);
}
static void directories(void) {
    bool active=test.active;test.active=false;DIR dir;FILINFO info;
    assert(f_opendir(&dir,"0:/")==FR_OK);
    for(;;) {assert(f_readdir(&dir,&info)==FR_OK);if(!info.fname[0])break;assert(!strcmp(info.fname,"KUI"));}
    assert(f_closedir(&dir)==FR_OK);test.active=active;
}
static void scenario(const char *name) {
    test.active=true;
    if(!strcmp(name,"missing")) {
        loaded("",0u);assert(!test.mkdirs && !test.writes);
        unsigned logs=test.logs;kui_games_scan_log_show(logger);assert(test.logs==logs);
        directories();return;
    }
    if(!strcmp(name,"first-write-recovery")) {
        test.fail_write=true;assert(!kui_games_scan_log_write(first,sizeof(first)-1u,logger) && !test.files);
        reset_faults();reboot();refused_load();
        uint8_t before[KUI_GAMES_SCAN_LOG_WIRE_MAX+1u],after[KUI_GAMES_SCAN_LOG_WIRE_MAX+1u];size_t bytes=raw(0u,before);
        assert(kui_games_scan_log_write(second,sizeof(second)-1u,logger));reboot();loaded(second,sizeof(second)-1u);
        assert(raw(0u,after)==bytes && !memcmp(before,after,bytes));directories();puts("PASS first-write-recovery");return;
    }
    assert(kui_games_scan_log_write(first,sizeof(first)-1u,logger) && !test.files);
    loaded(first,sizeof(first)-1u);reboot();loaded(first,sizeof(first)-1u);
    uint8_t before[KUI_GAMES_SCAN_LOG_WIRE_MAX+1u],after[KUI_GAMES_SCAN_LOG_WIRE_MAX+1u];size_t before_bytes=raw(0u,before);
    if(!strcmp(name,"roundtrip")) {
        assert(strstr((char *)before,"KUI games scan log v1") && strstr((char *)before,"CRC32:"));
        char tiny[4]="old";size_t bytes=77;assert(!kui_games_scan_log_load(tiny,sizeof(tiny),&bytes,logger) && !strcmp(tiny,"old") && bytes==77);
    } else if(!strcmp(name,"maximum")) {
        char text[KUI_GAMES_SCAN_LOG_TEXT_MAX+1u];memset(text,'X',sizeof(text));
        assert(kui_games_scan_log_write(text,KUI_GAMES_SCAN_LOG_TEXT_MAX,logger));reboot();loaded(text,KUI_GAMES_SCAN_LOG_TEXT_MAX);
        size_t bytes=raw(1u,after);assert(bytes<=KUI_GAMES_SCAN_LOG_WIRE_MAX);
        reset_faults();assert(!kui_games_scan_log_write(text,sizeof(text),logger) && !test.writes);
        text[10]=0;assert(!kui_games_scan_log_write(text,20u,logger) && !test.writes);
        text[10]=127;assert(!kui_games_scan_log_write(text,20u,logger) && !test.writes);
    } else if(!strcmp(name,"empty")) {
        assert(kui_games_scan_log_write(NULL,0u,logger));reboot();loaded("",0u);
    } else if(!strcmp(name,"show")) {
        char text[600];memset(text,'a',sizeof(text));text[0]='p';text[1]='a';text[2]='t';text[3]='h';text[4]='=';text[599]='\n';
        assert(kui_games_scan_log_write(text,sizeof(text),logger));test.shown_bytes=0;test.shown[0]=0;
        unsigned writes=test.writes;kui_games_scan_log_show(logger);assert(test.writes==writes && !test.files);
        assert(strstr(test.shown,"Previous Games scan report:\n"));
        char *body=strchr(test.shown,'\n');assert(body);++body;size_t bytes=0;
        for(;*body;body++)if(*body!='\n')assert(*body==text[bytes++]);
        assert(bytes==sizeof(text)-1u);
    } else if(!strcmp(name,"read-fail")) {
        reset_faults();test.fail_read=true;refused_load();assert(!kui_games_scan_log_write(second,sizeof(second)-1u,logger) && !test.writes);
        reset_faults();reboot();loaded(first,sizeof(first)-1u);
    } else if(!strcmp(name,"write-fail") || !strcmp(name,"short-write") || !strcmp(name,"sync-fail") ||
        !strcmp(name,"close-fail") || !strcmp(name,"verify-fail")) {
        reset_faults();test.fail_write=!strcmp(name,"write-fail");test.short_write=!strcmp(name,"short-write");
        test.fail_sync=!strcmp(name,"sync-fail");test.fail_close=!strcmp(name,"close-fail");test.fail_verify=!strcmp(name,"verify-fail");
        assert(!kui_games_scan_log_write(second,sizeof(second)-1u,logger) && !test.files);
        reset_faults();reboot();loaded(first,sizeof(first)-1u);
        assert(raw(0u,after)==before_bytes && !memcmp(before,after,before_bytes));
        assert(kui_games_scan_log_write(second,sizeof(second)-1u,logger));loaded(second,sizeof(second)-1u);
    } else if(!strcmp(name,"overflow")) {
        sequence(before,before_bytes,"ffffffffffffffff");replace(0u,before,before_bytes);reset_faults();
        assert(!kui_games_scan_log_write(second,sizeof(second)-1u,logger) && !test.writes);reboot();loaded(first,sizeof(first)-1u);
    } else {
        assert(kui_games_scan_log_write(second,sizeof(second)-1u,logger));size_t after_bytes=raw(1u,after);
        if(!strcmp(name,"alternate")) {
            assert(strstr((char *)before,"Sequence: 0000000000000001") && strstr((char *)after,"Sequence: 0000000000000002"));
            assert(kui_games_scan_log_write("third",5u,logger));reboot();loaded("third",5u);
            assert(raw(0u,before)>0u && strstr((char *)before,"Sequence: 0000000000000003"));
            assert(raw(1u,before)==after_bytes && !memcmp(before,after,after_bytes));
        } else if(!strcmp(name,"truncated") || !strcmp(name,"corrupt")) {
            if(!strcmp(name,"truncated"))replace(1u,after,32u);
            else {after[after_bytes-20u]^=1u;replace(1u,after,after_bytes);}
            reboot();loaded(first,sizeof(first)-1u);assert(kui_games_scan_log_write(second,sizeof(second)-1u,logger));loaded(second,sizeof(second)-1u);
        } else if(!strcmp(name,"both-corrupt")) {
            before[0]='?';after[0]='?';replace(0u,before,before_bytes);replace(1u,after,after_bytes);reset_faults();reboot();
            refused_load();assert(kui_games_scan_log_write("new",3u,logger));reboot();loaded("new",3u);
            uint8_t check[KUI_GAMES_SCAN_LOG_WIRE_MAX+1u];assert(raw(0u,check)==before_bytes && !memcmp(check,before,before_bytes));
        } else if(!strcmp(name,"equal-conflict")) {
            sequence(after,after_bytes,"0000000000000001");replace(1u,after,after_bytes);reset_faults();reboot();
            refused_load();assert(!kui_games_scan_log_write("new",3u,logger) && !test.writes);
        } else assert(!"unknown scenario");
    }
    assert(!test.files);directories();printf("PASS %s\n",name);
}
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    test.image=fopen(argv[1],"rb+");assert(test.image && !fseeko(test.image,0,SEEK_END));
    off_t bytes=ftello(test.image);assert(bytes>0 && bytes%512==0);test.blocks=(LBA_t)(bytes/512);
    assert(f_mount(&fs,"0:",1)==FR_OK);scenario(argv[2]);assert(!test.files);
    assert(f_mount(NULL,"0:",0)==FR_OK && !fclose(test.image));return 0;
}
