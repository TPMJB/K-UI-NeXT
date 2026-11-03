/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/problem_report.h"
#include "kui/game_launch_report.h"
#include "kui/runtime.h"
#include "kui/screenshot.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Real FatFs, private formatter and a bounded 96 MiB RAM disk. Faults are
 * injected above FatFs, leaving its actual allocation/directory operations
 * intact. Filesystem sources and production formatter configuration unchanged. */
#define TEST_SECTORS 196608u
static uint8_t *disk;
static FATFS fs;
static DWORD stamp;
static const char *host_root,*format_name,*fault_name;
static bool active,injected,cancelled;
static unsigned case_count,source_reads,module_reads,screenshot_reads;
static char renamed_stage[128],renamed_final[128];
struct open_file {FIL *file;char path[192];BYTE mode;};
static struct open_file open_files[16];
static unsigned open_count;
static const char session[]="Frozen support log: preparation does not establish gameplay.\n";
static uint8_t long_log[2303];
static const char *const source_paths[]={
    "0:/KUI/runtime.kui","0:/KUI/apps/games/retail-boot.kui",
    "0:/KUI/apps/games/retail-native-dual.kui","0:/KUI/apps/games/ce-probe.kui",
    KUI_GAME_LAUNCH_REPORT_PATH_A,KUI_GAME_LAUNCH_REPORT_PATH_B,
    "0:/KUI/screenshots/KUI-000010.bmp","0:/KUI/screenshots/KUI-000002.BMP",
    "0:/KUI/screenshots/KUI-000003.bmp","0:/KUI/screenshots/KUI-000004.bmp",
    "0:/KUI/screenshots/KUI-000020.part","0:/KUI/screenshots/private.bmp",
    "0:/KUI/ftp-password.txt","0:/Games/Secret/1ST_READ.BIN",
    "0:/KUI/vmu/private-card.vmd","0:/KUI/backups/system/secret-flash.bin"
};
struct original {uint8_t *bytes;size_t size;};
static struct original originals[sizeof(source_paths)/sizeof(source_paths[0])];
static const unsigned source_count=sizeof(source_paths)/sizeof(source_paths[0]);

DSTATUS disk_initialize(BYTE drive){return drive==0 && disk?0:STA_NOINIT;}
DSTATUS disk_status(BYTE drive){return disk_initialize(drive);}
DRESULT disk_read(BYTE drive,BYTE *out,LBA_t sector,UINT count) {
    if(drive || !out || !count || sector>=TEST_SECTORS || count>TEST_SECTORS-sector)return RES_PARERR;
    memcpy(out,disk+(size_t)sector*512u,(size_t)count*512u);return RES_OK;
}
DRESULT disk_write(BYTE drive,const BYTE *data,LBA_t sector,UINT count) {
    if(drive || !data || !count || sector>=TEST_SECTORS || count>TEST_SECTORS-sector)return RES_PARERR;
    memcpy(disk+(size_t)sector*512u,data,(size_t)count*512u);return RES_OK;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *out) {
    if(drive || !disk)return RES_NOTRDY;
    if(command==CTRL_SYNC)return RES_OK;
    if(command==GET_SECTOR_COUNT){*(LBA_t *)out=TEST_SECTORS;return RES_OK;}
    if(command==GET_BLOCK_SIZE){*(DWORD *)out=1u;return RES_OK;}
    return RES_PARERR;
}
DWORD get_fattime(void){return stamp;}
static bool fault(const char *name){return active && !injected && !strcmp(fault_name,name);}
static bool report_path(const char *path){return !strncmp(path,KUI_PROBLEM_REPORT_ROOT "/",strlen(KUI_PROBLEM_REPORT_ROOT)+1u);}
static bool module_path(const char *path) {
    static const char *const allowed[]={"0:/KUI/runtime.kui","0:/KUI/apps/games/retail-boot.kui",
        "0:/KUI/apps/games/retail-native-dual.kui","0:/KUI/apps/games/retail-native-dual-a1.kui",
        "0:/KUI/apps/games/retail-native-standalone.kui","0:/KUI/apps/games/ce-probe.kui",
        "0:/KUI/apps/games/ce-cdda-armada.kui","0:/KUI/apps/games/ce-cdda-bam4.kui",
        "0:/KUI/apps/games/ce-cdda-worms.kui","0:/KUI/apps/games/ce-cdda-wwp.kui",
        "0:/KUI/apps/games/retail-physical.kui"};
    for(unsigned i=0;i<sizeof(allowed)/sizeof(allowed[0]);i++)if(!strcmp(path,allowed[i]))return true;
    return false;
}
static struct open_file *opened(FIL *file) {
    for(unsigned i=0;i<open_count;i++)if(open_files[i].file==file)return &open_files[i];
    assert(!"Unknown active FatFs handle");return NULL;
}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE mode) {
    if(active) {
        if(mode&FA_WRITE)assert(report_path(path));
        else if(!report_path(path)) {
            bool screenshot=!strcmp(path,"0:/KUI/screenshots/KUI-000003.bmp");
            assert(module_path(path) || !strcmp(path,KUI_GAME_LAUNCH_REPORT_PATH_A) ||
                !strcmp(path,KUI_GAME_LAUNCH_REPORT_PATH_B) || screenshot);
            ++source_reads;if(screenshot)++screenshot_reads;
        }
    }
    FRESULT r=__real_f_open(file,path,mode);
    if(active && r==FR_OK) {
        assert(open_count<16u);struct open_file *o=&open_files[open_count++];
        o->file=file;o->mode=mode;snprintf(o->path,sizeof(o->path),"%s",path);
    }
    return r;
}
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *file,const void *data,UINT bytes,UINT *written) {
    struct open_file *o=active?opened(file):NULL;
    if(active)assert((o->mode&FA_WRITE) && report_path(o->path));
    if(fault("write")){injected=true;*written=0;return FR_DISK_ERR;}
    if(fault("short-write")){injected=true;return __real_f_write(file,data,bytes-1u,written);}
    if(fault("manifest-write") && strstr(o->path,"/manifest.json.part")){
        injected=true;*written=0u;return FR_DISK_ERR;
    }
    FRESULT r=__real_f_write(file,data,bytes,written);
    if(fault("cancel-during")){injected=cancelled=true;}
    return r;
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *out,UINT bytes,UINT *got) {
    struct open_file *o=active?opened(file):NULL;
    bool verify=active && report_path(o->path) && !(o->mode&FA_WRITE);
    if(active && module_path(o->path)) {assert(bytes<=64u);++module_reads;}
    if(fault("source-read") && !strcmp(o->path,KUI_GAME_LAUNCH_REPORT_PATH_A)){
        injected=true;*got=0u;return FR_DISK_ERR;
    }
    if(verify && fault("read")){injected=true;*got=0u;return FR_DISK_ERR;}
    FRESULT r=__real_f_read(file,out,bytes,got);
    if(verify && fault("short-read")){injected=true;if(*got)--*got;}
    if(verify && fault("readback-corrupt")){injected=true;assert(*got);((uint8_t *)out)[0]^=1u;}
    return r;
}
FRESULT __real_f_sync(FIL *);
FRESULT __wrap_f_sync(FIL *file) {
    FRESULT r=__real_f_sync(file);
    if(fault("sync")){injected=true;return FR_DISK_ERR;}
    return r;
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    bool writing=false,verifying=false;
    if(active) {
        struct open_file *o=opened(file);writing=(o->mode&FA_WRITE)!=0u;
        verifying=!writing && report_path(o->path);
        unsigned at=(unsigned)(o-open_files);open_files[at]=open_files[--open_count];
    }
    FRESULT r=__real_f_close(file);
    if(writing && fault("close")){injected=true;return FR_DISK_ERR;}
    if(verifying && fault("verify-close")){injected=true;return FR_DISK_ERR;}
    return r;
}
FRESULT __real_f_rename(const TCHAR *,const TCHAR *);
FRESULT __wrap_f_rename(const TCHAR *from,const TCHAR *to) {
    if(active){assert(report_path(from) && report_path(to));}
    const char *name=strrchr(from,'/');
    bool final=active && name && !strncmp(name+1u,"REPORT-",7u);
    if(final) {
        snprintf(renamed_stage,sizeof(renamed_stage),"%s",from);
        snprintf(renamed_final,sizeof(renamed_final),"%s",to);
        if(fault("final-rename")){injected=true;return FR_DISK_ERR;}
    } else if(fault("file-rename")){injected=true;return FR_DISK_ERR;}
    FRESULT r=__real_f_rename(from,to);
    if(final && fault("uncertain-rename")){assert(r==FR_OK);injected=true;return FR_DISK_ERR;}
    if(final && fault("cancel-after-rename")){assert(r==FR_OK);injected=cancelled=true;}
    return r;
}
static bool cancel(void){return cancelled;}
static void log_line(const char *format,...){(void)format;}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8u*i));}
static void folder(const char *path){FRESULT r=f_mkdir(path);assert(r==FR_OK || r==FR_EXIST);}
static void write_file(const char *path,const void *data,size_t bytes) {
    FIL file;UINT written=0u;assert(f_open(&file,path,FA_WRITE|FA_CREATE_NEW)==FR_OK);
    assert(f_write(&file,data,(UINT)bytes,&written)==FR_OK && written==bytes);
    assert(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static uint8_t *read_file(const char *path,size_t *bytes) {
    FIL file;assert(f_open(&file,path,FA_READ)==FR_OK);*bytes=(size_t)f_size(&file);
    uint8_t *data=calloc(*bytes+1u,1u);assert(data);UINT got=0u;
    assert(f_read(&file,data,(UINT)*bytes,&got)==FR_OK && got==*bytes && f_close(&file)==FR_OK);return data;
}
static bool exists(const char *path){FILINFO info;return f_stat(path,&info)==FR_OK;}
static void remount(BYTE expected) {
    assert(!active && !open_count);assert(f_mount(NULL,"0:",0)==FR_OK);memset(&fs,0,sizeof(fs));
    assert(f_mount(&fs,"0:",1)==FR_OK && fs.fs_type==expected);
}
static void format(BYTE fmt,BYTE expected) {
    memset(disk,0,(size_t)TEST_SECTORS*512u);uint8_t work[8192];
    MKFS_PARM p={.fmt=(BYTE)(fmt|FM_SFD),.n_fat=1,.align=1,.n_root=512,.au_size=fmt==FM_FAT32?512u:4096u};
    assert(f_mkfs("0:",&p,work,sizeof(work))==FR_OK);remount(expected);
    stamp=(DWORD)((46u<<25)|(11u<<21)|(1u<<16));
}
static void seed(void) {
    static const char *const folders[]={"0:/KUI","0:/KUI/apps","0:/KUI/apps/games","0:/KUI/screenshots",
        "0:/Games","0:/Games/Secret","0:/KUI/vmu","0:/KUI/backups","0:/KUI/backups/system"};
    for(unsigned i=0;i<sizeof(folders)/sizeof(folders[0]);i++)folder(folders[i]);
    uint8_t module[80]={0};memcpy(module,"KUIRUN1\0",8u);
    put32(module+8u,1u);put32(module+12u,64u);put32(module+16u,16u);
    put32(module+20u,KUI_RUNTIME_ADDRESS);put32(module+24u,KUI_RUNTIME_ADDRESS);put32(module+28u,64u);
    put32(module+32u,UINT32_C(0x11223344));memcpy(module+40u,"123456789abc",12u);
    memcpy(module+64u,"MODULE-PRIVATE!!",16u);put32(module+60u,kui_crc32(0,module,60u));
    write_file(source_paths[0],module,sizeof(module));
    module[0]='X';write_file(source_paths[1],module,sizeof(module));module[0]='K';
    memcpy(module+40u,"abcdef123456",12u);put32(module+60u,kui_crc32(0,module,60u));
    write_file(source_paths[2],module,sizeof(module));write_file(source_paths[3],module,60u);
    write_file(source_paths[4],"Launch A: preparation only\n",27u);
    write_file(source_paths[5],"Launch B: source bytes are not included\n",39u);
    uint8_t bmp[70]={0};memcpy(bmp,"BM",2u);put32(bmp+2u,sizeof(bmp));put32(bmp+10u,54u);
    put32(bmp+14u,40u);put32(bmp+18u,2u);put32(bmp+22u,2u);bmp[26]=1u;bmp[28]=24u;
    for(unsigned i=6u;i<=8u;i++) {
        stamp=(DWORD)((46u<<25)|(11u<<21)|((i==6u?1u:3u)<<16));
        memset(bmp+54u,(int)(i+13u),16u);write_file(source_paths[i],bmp,sizeof(bmp));
    }
    write_file(source_paths[9],"tiny",4u);write_file(source_paths[10],"PARTIAL SCREENSHOT",18u);
    write_file(source_paths[11],"PRIVATE IMAGE NEVER INCLUDED",27u);
    folder("0:/KUI/screenshots/KUI-999998.bmp");
    static const char *const secrets[]={"FTP PASSWORD SECRET","GAME EXECUTABLE SECRET","VMU SAVE SECRET","SETTINGS FLASH SECRET"};
    for(unsigned i=12u;i<source_count;i++)write_file(source_paths[i],secrets[i-12u],strlen(secrets[i-12u]));
    for(unsigned i=0;i<source_count;i++)originals[i].bytes=read_file(source_paths[i],&originals[i].size);
}
static void originals_unchanged(void) {
    for(unsigned i=0;i<source_count;i++) {
        size_t size;uint8_t *copy=read_file(source_paths[i],&size);
        assert(size==originals[i].size && !memcmp(copy,originals[i].bytes,size));free(copy);
    }
}
static void originals_free(void){for(unsigned i=0;i<source_count;i++){free(originals[i].bytes);originals[i].bytes=NULL;}}
static void activate(const char *fault) {
    assert(!open_count);fault_name=fault;injected=cancelled=false;source_reads=module_reads=screenshot_reads=0u;
    renamed_stage[0]=renamed_final[0]=0;active=true;
}
static bool export(bool screenshot,struct kui_problem_report_view *view) {
    const struct kui_problem_report_options options={.include_latest_screenshot=screenshot};
    return kui_problem_report_export_mounted(&options,"3.0.0","aabbccddeeff",long_log,sizeof(long_log),view,log_line,cancel);
}
static void compare_file(const char *folder_path,const char *name,const void *expected,size_t bytes) {
    char path[192];snprintf(path,sizeof(path),"%s/%s",folder_path,name);
    size_t size;uint8_t *copy=read_file(path,&size);assert(size==bytes && !memcmp(copy,expected,size));free(copy);
}
static void host_export(const char *folder_path,const char *name,const void *data,size_t bytes) {
    char path[512];snprintf(path,sizeof(path),"%s/%s-%s-%s",host_root,format_name,folder_path+strlen(KUI_PROBLEM_REPORT_ROOT)+1u,name);
    FILE *out=fopen(path,"wb");assert(out);assert(fwrite(data,1,bytes,out)==bytes && !fclose(out));
}
static void check_report(const struct kui_problem_report_view *view,bool screenshot,bool save_host) {
    assert(view->status.complete && view->status.passed && !view->status.errors && !view->status.stopped);
    assert(view->files==(screenshot?6u:5u) && view->unavailable==0u && exists(view->path));
    compare_file(view->path,"session-log.txt",long_log,sizeof(long_log));
    compare_file(view->path,"last-launch-a.txt",originals[4].bytes,originals[4].size);
    compare_file(view->path,"last-launch-b.txt",originals[5].bytes,originals[5].size);
    if(screenshot)compare_file(view->path,"screenshot.bmp",originals[8].bytes,originals[8].size);
    char path[192];snprintf(path,sizeof(path),"%s/module-headers.txt",view->path);
    size_t size;uint8_t *data=read_file(path,&size);
    assert(strstr((char *)data,"Header checks only; payloads are not reread."));
    assert(strstr((char *)data,"/KUI/runtime.kui | build=123456789abc payload=16 memory=64 crc32=11223344"));
    assert(strstr((char *)data,"retail-native-dual.kui | build=abcdef123456"));
    assert(strstr((char *)data,"retail-boot.kui | invalid runtime header"));
    assert(strstr((char *)data,"retail-native-dual-a1.kui | not installed"));
    assert(strstr((char *)data,"ce-probe.kui | runtime file I/O failed"));
    assert(!strstr((char *)data,"MODULE-PRIVATE"));free(data);
    snprintf(path,sizeof(path),"%s/manifest.json",view->path);data=read_file(path,&size);
    assert(strstr((char *)data,"\"schema\":\"kui.support-report.v1\""));
    assert(strstr((char *)data,"\"launch_reports_describe_preparation\":true"));
    unsigned rows=0u;uint64_t total=size;const char *cursor=(char *)data;
    while((cursor=strstr(cursor,"{\"name\":\""))!=NULL) {
        char name[48];unsigned bytes,crc;
        assert(sscanf(cursor,"{\"name\":\"%47[^\"]\",\"bytes\":%u,\"crc32\":\"%x\"}",name,&bytes,&crc)==3);
        bool allowed=!strcmp(name,"session-log.txt") || !strcmp(name,"module-headers.txt") ||
            !strcmp(name,"last-launch-a.txt") || !strcmp(name,"last-launch-b.txt") || (screenshot && !strcmp(name,"screenshot.bmp"));
        assert(allowed);snprintf(path,sizeof(path),"%s/%s",view->path,name);
        size_t actual;uint8_t *included=read_file(path,&actual);
        assert(actual==bytes && kui_crc32(0,included,actual)==crc);
        assert(!strstr((char *)included,"FTP PASSWORD SECRET") && !strstr((char *)included,"VMU SAVE SECRET") &&
            !strstr((char *)included,"GAME EXECUTABLE SECRET") && !strstr((char *)included,"SETTINGS FLASH SECRET"));
        if(save_host)host_export(view->path,name,included,actual);
        free(included);total+=actual;++rows;cursor+=9u;
    }
    assert(rows+1u==view->files && total==view->bytes && view->status.done==total && view->status.total==total);
    if(save_host)host_export(view->path,"manifest.json",data,size);
    free(data);
    DIR dir;FILINFO info;assert(f_opendir(&dir,view->path)==FR_OK);unsigned files=0u;
    for(;;){assert(f_readdir(&dir,&info)==FR_OK);if(!info.fname[0])break;assert(!(info.fattrib&AM_DIR));assert(!strstr(info.fname,".part"));++files;}
    assert(f_closedir(&dir)==FR_OK && files==view->files);
}
static void normal(BYTE fmt,BYTE expected,const char *label) {
    format_name=label;format(fmt,expected);seed();struct kui_problem_report_view first,second;
    activate("none");assert(export(true,&first));active=false;assert(!open_count && module_reads==4u && screenshot_reads==1u);
    assert(!strcmp(first.path,KUI_PROBLEM_REPORT_ROOT "/REPORT-000001"));check_report(&first,true,true);originals_unchanged();
    char path[192];snprintf(path,sizeof(path),"%s/manifest.json",first.path);size_t prior_size;uint8_t *prior=read_file(path,&prior_size);
    remount(expected);activate("none");assert(export(false,&second));active=false;
    assert(!open_count && !screenshot_reads && !strcmp(second.path,KUI_PROBLEM_REPORT_ROOT "/REPORT-000002"));check_report(&second,false,true);
    compare_file(first.path,"manifest.json",prior,prior_size);free(prior);
    folder(KUI_PROBLEM_REPORT_ROOT "/REPORT-000009.part");
    static const char marker[]="Interrupted prior report must survive";
    write_file(KUI_PROBLEM_REPORT_ROOT "/REPORT-000009.part/kept.txt",marker,sizeof(marker));
    remount(expected);activate("none");assert(export(true,&second));active=false;
    assert(!strcmp(second.path,KUI_PROBLEM_REPORT_ROOT "/REPORT-000010"));check_report(&second,true,false);
    compare_file(KUI_PROBLEM_REPORT_ROOT "/REPORT-000009.part","kept.txt",marker,sizeof(marker));originals_unchanged();
    assert(f_mount(NULL,"0:",0)==FR_OK);originals_free();case_count+=3u;
    printf("PASS problem reports actual FatFs %s: exact selected inputs, header identities, independent CRCs, allowlist, remount numbering and retained reports/partials\n",label);
}
static void failures(BYTE fmt,BYTE expected,const char *label) {
    static const char *const cases[]={"short-write","write","sync","close","read","short-read","readback-corrupt",
        "verify-close","source-read","file-rename","manifest-write","cancel-during","final-rename","uncertain-rename","cancel-after-rename"};
    format_name=label;
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        format(fmt,expected);seed();struct kui_problem_report_view view;
        activate(cases[i]);assert(!export(true,&view));active=false;
        assert(injected && !open_count && !view.path[0] && !view.status.passed && view.status.complete);
        assert(view.status.stopped==cancelled && view.status.errors==(cancelled?0u:1u));
        bool published=!strcmp(cases[i],"uncertain-rename") || !strcmp(cases[i],"cancel-after-rename");
        assert(exists(KUI_PROBLEM_REPORT_ROOT "/REPORT-000001")==published);
        assert(exists(KUI_PROBLEM_REPORT_ROOT "/REPORT-000001.part")==!published);
        if(published)assert(renamed_stage[0] && renamed_final[0]);
        originals_unchanged();remount(expected);originals_unchanged();originals_free();
        printf("PASS problem report %s fault %s: no success/path, no overwritten sources, %s retained\n",label,cases[i],published?"uncertain final folder":"unpublished partial folder");++case_count;
    }
    assert(f_mount(NULL,"0:",0)==FR_OK);
}
static void missing_and_invalid(BYTE fmt,BYTE expected,const char *label) {
    format_name=label;format(fmt,expected);struct kui_problem_report_view view;
    const struct kui_problem_report_options options={true};activate("none");
    assert(kui_problem_report_export_mounted(&options,"3.0.0","aabbccddeeff",NULL,0,&view,log_line,cancel));active=false;
    assert(view.files==3u && view.unavailable==3u && !open_count);
    compare_file(view.path,"session-log.txt","No session log was available.\n",30u);
    activate("none");cancelled=true;assert(!export(false,&view));active=false;
    assert(view.status.stopped && !view.path[0] && !open_count);
    assert(!kui_problem_report_export_mounted(&options,"bad\nversion","aabb",session,sizeof(session)-1u,&view,NULL,NULL));
    assert(!view.path[0] && view.status.errors==1u);
    assert(!kui_problem_report_export_mounted(&options,"3.0.0","aabb",NULL,1u,&view,NULL,NULL));
    assert(!view.path[0] && view.status.errors==1u);
    assert(f_mount(NULL,"0:",0)==FR_OK);case_count+=4u;
    printf("PASS problem report %s: absent optional inputs, frozen-log fallback, early cancel and invalid requests\n",label);
}
int main(int argc,char **argv) {
    assert(argc==2);host_root=argv[1];disk=malloc((size_t)TEST_SECTORS*512u);assert(disk);
    for(unsigned i=0;i<sizeof(long_log);i++)long_log[i]=(uint8_t)('a'+i%26u);
    normal(FM_FAT32,FS_FAT32,"FAT32");failures(FM_FAT32,FS_FAT32,"FAT32");missing_and_invalid(FM_FAT32,FS_FAT32,"FAT32");
    normal(FM_EXFAT,FS_EXFAT,"exFAT");failures(FM_EXFAT,FS_EXFAT,"exFAT");missing_and_invalid(FM_EXFAT,FS_EXFAT,"exFAT");
    free(disk);printf("PASS problem report actual filesystem scenarios: %u\n",case_count);return 0;
}
