/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/games.h"
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

/* Actual FatFs, actual libchdr, actual Games adapter. Faults are injected at
 * the file boundary so the on-disk FAT32/exFAT transaction is still exercised. */
static struct {
    FILE *image;
    uint64_t blocks;
    const char *fault;
    bool active,connected,injected,cancelled,readonly,cd;
    unsigned writes,connects,disconnects,files,dirs,progress;
    struct { FIL *file; bool source,output; unsigned kind; } handles[128];
} test;
static const char source[]="/Games/CHD Test/selected.chd";
static const char original[]="/Games/Original/disc.gdi";
static bool is_fault(const char *name) {return test.active && !test.injected && !strcmp(test.fault,name);}
static bool trigger(const char *name) {
    if(!is_fault(name)) return false;
    test.injected=true;return true;
}
static void log_line(const char *fmt,...) {
    va_list args;va_start(args,fmt);vprintf(fmt,args);va_end(args);puts("");
}
static uint64_t blocks(void *ctx) {(void)ctx;return test.blocks;}
static int read_image(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;return fseeko(test.image,(off_t)block*512,SEEK_SET) ||
        fread(data,512,count,test.image)!=count?-1:0;
}
static int write_image(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;if(test.active) {assert(!test.readonly);++test.writes;}
    return fseeko(test.image,(off_t)block*512,SEEK_SET) ||
        fwrite(data,512,count,test.image)!=count?-1:0;
}
static int sync_image(void *ctx) {(void)ctx;return fflush(test.image) || fsync(fileno(test.image))?-1:0;}
static const struct kui_media_ops media={NULL,blocks,read_image,write_image,sync_image};
unsigned kui_storage_active(void) {return KUI_STORAGE_SCI;}
bool kui_sd_connect(void) {
    assert(!test.connected);++test.connects;
    if(trigger("connect-fail")) return false;
    test.connected=true;kui_media_set(&media);return true;
}
void kui_sd_disconnect(void) {
    assert(test.connected && !test.files && !test.dirs);
    test.connected=false;++test.disconnects;kui_media_set(NULL);
}
static bool cancel(void) {return test.cancelled || is_fault("cancel-before");}
static void progress(const struct kui_app_status *status) {
    assert(status && status->total && status->done<=status->total);++test.progress;
}
static int handle(FIL *file) {
    for(unsigned i=0;i<128;i++) if(test.handles[i].file==file) return (int)i;
    return -1;
}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE flags) {
    bool src=strstr(path,"selected.chd")!=NULL;
    bool output=strstr(path,"/Cache/CHD/") && (flags&FA_WRITE);
    if(test.active) {
        if(test.readonly) assert(!(flags&(FA_WRITE|FA_CREATE_NEW|FA_CREATE_ALWAYS|FA_OPEN_ALWAYS)));
        if((src && trigger("source-open-fail")) || (output && trigger("output-open-fail"))) return FR_DISK_ERR;
    }
    FRESULT result=__real_f_open(file,path,flags);
    if(test.active && result==FR_OK) {
        unsigned i;for(i=0;i<128 && test.handles[i].file;i++);assert(i<128);
        test.handles[i].file=file;test.handles[i].source=src;test.handles[i].output=output;
        test.handles[i].kind=output?(strstr(path,"/cache.bin")?2u:strstr(path,"/disc.gdi")?3u:
            strstr(path,"/disc.cue")?4u:1u):0u;++test.files;
    }
    return result;
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *data,UINT bytes,UINT *got) {
    FRESULT result=__real_f_read(file,data,bytes,got);int i=handle(file);
    if(test.active && i>=0) {
        if(test.handles[i].source) {
            if(trigger("source-read-fail")) return FR_DISK_ERR;
            for(unsigned h=0;h<128;h++) if(test.handles[h].file && test.handles[h].kind==1u &&
                trigger("decode-read-fail")) return FR_DISK_ERR;
            if(*got && trigger("source-short-read")) --*got;
            if(trigger("cancel-read")) test.cancelled=true;
        }
        if(test.handles[i].output && *got && trigger("verify-corrupt")) ((uint8_t *)data)[*got/2]^=0x80;
        if(test.handles[i].kind==1u && trigger("cancel-verify")) test.cancelled=true;
        if(test.handles[i].kind==3u && *got && trigger("descriptor-corrupt")) ((uint8_t *)data)[*got/2]^=0x80;
        if(test.handles[i].kind==4u && *got && trigger("cue-corrupt")) ((uint8_t *)data)[*got/2]^=0x80;
    }
    return result;
}
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *file,const void *data,UINT bytes,UINT *put) {
    if(test.active) {
        assert(!test.readonly);int i=handle(file);
        if(i>=0 && test.handles[i].output) {
            if(trigger("write-fail")) {*put=0;return FR_DISK_ERR;}
            if((test.handles[i].kind==2u && trigger("stamp-write-fail")) ||
               (test.handles[i].kind==3u && trigger("descriptor-write-fail")) ||
               (test.handles[i].kind==4u && trigger("cue-write-fail"))) {*put=0;return FR_DISK_ERR;}
            if(bytes && trigger("short-write")) return __real_f_write(file,data,bytes-1,put);
            if(trigger("cancel-write")) test.cancelled=true;
        }
    }
    return __real_f_write(file,data,bytes,put);
}
FRESULT __real_f_lseek(FIL *,FSIZE_t);
FRESULT __wrap_f_lseek(FIL *file,FSIZE_t at) {
    int i=handle(file);
    if(test.active && i>=0 && test.handles[i].source && trigger("source-seek-fail")) return FR_DISK_ERR;
    return __real_f_lseek(file,at);
}
FRESULT __real_f_sync(FIL *);
FRESULT __wrap_f_sync(FIL *file) {
    int i=handle(file);
    if(test.active && i>=0 && test.handles[i].output &&
        (trigger("sync-fail") || (test.handles[i].kind==2u && trigger("stamp-sync-fail")) ||
         (test.handles[i].kind==4u && trigger("cue-sync-fail")))) return FR_DISK_ERR;
    return __real_f_sync(file);
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    int i=handle(file);FRESULT result=__real_f_close(file);
    if(test.active) {
        assert(i>=0 && test.files);--test.files;
        bool src=test.handles[i].source,output=test.handles[i].output;unsigned kind=test.handles[i].kind;
        memset(&test.handles[i],0,sizeof(test.handles[i]));
        if(kind==2u && trigger("cancel-publish")) test.cancelled=true;
        if(kind==4u && trigger("cancel-cue-publish")) test.cancelled=true;
        if((src && trigger("source-close-fail")) || (output && trigger("close-fail"))) return FR_DISK_ERR;
        if(kind==3u && trigger("descriptor-close-fail")) return FR_DISK_ERR;
        if(kind==4u && trigger("cue-close-fail")) return FR_DISK_ERR;
    }
    return result;
}
FRESULT __real_f_opendir(DIR *,const TCHAR *);
FRESULT __wrap_f_opendir(DIR *dir,const TCHAR *path) {
    FRESULT result=__real_f_opendir(dir,path);if(test.active && result==FR_OK) ++test.dirs;return result;
}
FRESULT __real_f_closedir(DIR *);
FRESULT __wrap_f_closedir(DIR *dir) {
    FRESULT result=__real_f_closedir(dir);if(test.active) {assert(test.dirs);--test.dirs;}return result;
}
FRESULT __real_f_mkdir(const TCHAR *);
FRESULT __wrap_f_mkdir(const TCHAR *path) {
    if(test.active) {assert(!test.readonly);if(trigger("mkdir-fail")) return FR_DISK_ERR;}
    return __real_f_mkdir(path);
}
FRESULT __real_f_unlink(const TCHAR *);
FRESULT __wrap_f_unlink(const TCHAR *path) {
    if(test.active) {assert(!test.readonly);assert(strstr(path,"/Cache/CHD/") && strstr(path,".work"));}
    return __real_f_unlink(path);
}
FRESULT __real_f_rename(const TCHAR *,const TCHAR *);
FRESULT __wrap_f_rename(const TCHAR *a,const TCHAR *b) {
    if(test.active) {
        assert(!test.readonly && strstr(a,".work") && !strstr(b,".work"));
        if(trigger("rename-fail")) return FR_DISK_ERR;
    }
    return __real_f_rename(a,b);
}
FRESULT __real_f_getfree(const TCHAR *,DWORD *,FATFS **);
FRESULT __wrap_f_getfree(const TCHAR *path,DWORD *clusters,FATFS **fs) {
    if(trigger("getfree-fail")) return FR_DISK_ERR;
    FRESULT result=__real_f_getfree(path,clusters,fs);
    if(result==FR_OK && trigger("no-space")) *clusters=0;
    return result;
}
static void mount_card(FATFS *fs) {kui_media_set(&media);assert(kui_mount(fs,log_line));}
static void unmount_card(void) {assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);}
static void copy_host(const char *host,const char *target) {
    FILE *in=fopen(host,"rb");assert(in);FIL out;UINT put;uint8_t data[32768];
    assert(f_open(&out,target,FA_WRITE|FA_CREATE_NEW)==FR_OK);
    for(;;) {
        size_t got=fread(data,1,sizeof(data),in);assert(!ferror(in));if(!got) break;
        assert(f_write(&out,data,(UINT)got,&put)==FR_OK && put==got);
    }
    assert(f_close(&out)==FR_OK && !fclose(in));
}
static void seed(const char *host) {
    assert(f_mkdir("0:/Games")==FR_OK && f_mkdir("0:/Games/CHD Test")==FR_OK &&
        f_mkdir("0:/Games/Original")==FR_OK);
    const char *names[]={"disc.gdi","track01.bin","track02.raw","track03.bin","selected.chd"};
    const char *cd_names[]={"disc.cue","selected.iso","selected.chd"};
    const char *const *files=test.cd?cd_names:names;
    unsigned count=test.cd?3u:5u;
    for(unsigned i=0;i<count;i++) {
        char from[1024],to[256];assert(snprintf(from,sizeof(from),"%s/%s",host,files[i])<(int)sizeof(from));
        assert(snprintf(to,sizeof(to),"0:/Games/%s/%s",i+1u==count?"CHD Test":"Original",files[i])<(int)sizeof(to));
        copy_host(from,to);
    }
    assert(f_mkdir("0:/KUI")==FR_OK && f_mkdir("0:/KUI/VMU")==FR_OK && f_mkdir("0:/KUI/VMU/p0001")==FR_OK);
    FIL file;UINT put;static const char sentinel[]="Existing VMU profile and card bytes must survive CHD preparation";
    assert(f_open(&file,"0:/KUI/VMU/p0001/card.bin",FA_WRITE|FA_CREATE_NEW)==FR_OK &&
        f_write(&file,sentinel,sizeof(sentinel),&put)==FR_OK && put==sizeof(sentinel) && f_close(&file)==FR_OK);
}
static void file_hash(const char *path,uint8_t digest[32]) {
    FIL file;UINT got;uint8_t data[32768];struct kui_sha256 hash;kui_sha256_init(&hash);
    assert(f_open(&file,path,FA_READ)==FR_OK);
    for(;;) {
        assert(f_read(&file,data,sizeof(data),&got)==FR_OK);if(!got) break;kui_sha256_update(&hash,data,got);
    }
    assert(f_close(&file)==FR_OK);kui_sha256_digest(&hash,digest);
}
static unsigned cache_count(bool stages) {
    DIR dir;FILINFO info;unsigned count=0;FRESULT result=f_opendir(&dir,"0:/KUI/Cache/CHD");
    if(result==FR_NO_PATH || result==FR_NO_FILE) return 0;
    assert(result==FR_OK);
    for(;;) {
        assert(f_readdir(&dir,&info)==FR_OK);if(!info.fname[0]) break;
        assert(info.fattrib&AM_DIR);
        if((strstr(info.fname,".work")!=NULL)==stages) ++count;
    }
    assert(f_closedir(&dir)==FR_OK);return count;
}
static void verify_tracks(const char *cache) {
    const char *names[]={"track01.bin","track02.raw","track03.bin"};
    const uint64_t sizes[]={4u*2352u,4u*2352u,64u*2352u};
    char folder[KUI_GAMES_FILE_CAP];strcpy(folder,cache);*strrchr(folder,'/')=0;
    for(unsigned i=0;i<(test.cd?1u:3u);i++) {
        char a[KUI_DEST_PATH_CAP],b[KUI_DEST_PATH_CAP];uint8_t x[32],y[32];FILINFO info;
        assert(snprintf(a,sizeof(a),"0:/Games/Original/%s",test.cd?"selected.iso":names[i])<(int)sizeof(a));
        assert(snprintf(b,sizeof(b),"0:%s/track%02u.bin",folder,i+1)<(int)sizeof(b));
        assert(f_stat(b,&info)==FR_OK && info.fsize==(test.cd?64u*2048u:sizes[i]));
        file_hash(a,x);file_hash(b,y);assert(!memcmp(x,y,32));
    }
}
static void assert_metadata(const struct kui_games_detail *a,const struct kui_games_detail *b) {
    assert(a->valid && b->valid && !strcmp(a->title,b->title) && !strcmp(a->product,b->product) &&
        !strcmp(a->region,b->region) && !strcmp(a->boot_file,b->boot_file) &&
        a->boot_lba==b->boot_lba && a->boot_bytes==b->boot_bytes && a->tracks==b->tracks &&
        a->audio_tracks==b->audio_tracks && a->data_tracks==b->data_tracks && a->bytes==b->bytes &&
        a->native_gd==b->native_gd && a->native_cd==b->native_cd && a->windows_ce==b->windows_ce &&
        a->cd_image==b->cd_image && a->scrambled==b->scrambled &&
        a->audio.classification==b->audio.classification && a->audio.candidate_tracks==b->audio.candidate_tracks);
}
static void check(void) {
    struct kui_games_detail detail,reference;FATFS fs;uint8_t before[32],after[32],guard[32];
    mount_card(&fs);file_hash("0:/Games/CHD Test/selected.chd",before);
    file_hash("0:/KUI/VMU/p0001/card.bin",guard);unmount_card();
    bool readonly=!strcmp(test.fault,"inspect"),reuse=!strcmp(test.fault,"reuse"),change=!strcmp(test.fault,"changed-source"),
        existing=!strcmp(test.fault,"existing-incomplete"),success=!strcmp(test.fault,"prepare") || reuse || change;
    test.active=true;test.readonly=readonly;
    if(readonly) {
        assert(!kui_games_inspect(source,&detail,log_line,cancel));
        assert(detail.chd && detail.chd_prepare && !detail.chd_ready && !detail.valid &&
            detail.cache_bytes>(test.cd?64u*2048u:72u*2352u));
        assert(!test.writes);
    } else {
        if(existing) {
            /* First discover the deterministic destination, then truncate its
             * manifest outside the active operation. Existing bytes must stay. */
            test.active=false;assert(kui_games_chd_prepare(source,&detail,log_line,cancel,progress));
            char path[KUI_DEST_PATH_CAP];strcpy(path,"0:");strcat(path,detail.cache_path);
            *strrchr(path,'/')=0;strcat(path,"/cache.bin");
            mount_card(&fs);FIL file;assert(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
            UINT put;assert(f_write(&file,"sentinel",8,&put)==FR_OK && put==8 && f_close(&file)==FR_OK);unmount_card();
            test.active=true;test.readonly=true;
            assert(!kui_games_chd_prepare(source,&detail,log_line,cancel,progress));
            assert(strstr(detail.message,"incomplete"));test.readonly=false;
        } else {
            bool ok=kui_games_chd_prepare(source,&detail,log_line,cancel,progress);
            printf("Preparation result=%u: %s\n",(unsigned)ok,detail.message);
            assert(ok==success);
            if(success) {
                assert(detail.chd && detail.chd_ready && detail.valid && !strcmp(detail.path,source));
                assert(detail.cache_path[0] && test.progress);
                if(test.cd) assert(strstr(detail.cache_path,"/disc.cue") && detail.cd_image && detail.native_cd &&
                    !detail.native_gd && detail.boot_lba==21u && detail.boot_bytes==3001u);
                char first[KUI_GAMES_FILE_CAP];strcpy(first,detail.cache_path);
                if(reuse) {
                    unsigned writes=test.writes,updates=test.progress;test.readonly=true;
                    assert(kui_games_chd_prepare(source,&detail,log_line,cancel,progress));
                    assert(!strcmp(first,detail.cache_path) && writes==test.writes && updates==test.progress);test.readonly=false;
                }
                if(change) {
                    /* v5 overall content SHA1 changes identity without altering
                     * compressed sectors. A new source must get a new folder. */
                    test.active=false;mount_card(&fs);FIL file;UINT got,put;uint8_t byte;
                    assert(f_open(&file,"0:/Games/CHD Test/selected.chd",FA_READ|FA_WRITE)==FR_OK &&
                        f_lseek(&file,84)==FR_OK && f_read(&file,&byte,1,&got)==FR_OK && got==1);
                    byte^=1;assert(f_lseek(&file,84)==FR_OK && f_write(&file,&byte,1,&put)==FR_OK && put==1 && f_close(&file)==FR_OK);
                    file_hash("0:/Games/CHD Test/selected.chd",before);unmount_card();test.active=true;
                    assert(kui_games_chd_prepare(source,&detail,log_line,cancel,progress));
                    assert(strcmp(first,detail.cache_path));
                }
                test.readonly=true;assert(kui_games_inspect(test.cd?"/Games/Original/disc.cue":original,&reference,log_line,cancel));
                assert_metadata(&reference,&detail);test.readonly=false;
            } else {
                assert(detail.message[0]);
                if(!strncmp(test.fault,"cancel-",7)) assert(detail.stopped);
            }
        }
    }
    assert(!test.files && !test.dirs && !test.connected);
    assert(test.connects==test.disconnects+(!strcmp(test.fault,"connect-fail")?1u:0u));
    test.active=false;mount_card(&fs);
    file_hash("0:/Games/CHD Test/selected.chd",after);assert(!memcmp(before,after,32));
    file_hash("0:/KUI/VMU/p0001/card.bin",after);assert(!memcmp(guard,after,32));
    unsigned finals=cache_count(false),stages=cache_count(true);
    printf("Cache folders: final=%u staging=%u\n",finals,stages);
    assert(!stages && finals==(change?2u:success || existing?1u:0u));
    if(success) verify_tracks(detail.cache_path);
    unmount_card();
    if(!success && !readonly && !existing && strcmp(test.fault,"cancel-before")) assert(test.injected);
}
int main(int argc,char **argv) {
    if(argc!=5) return 2;
    struct stat st;if(lstat(argv[1],&st) || !S_ISREG(st.st_mode) || st.st_size%512) return 2;
    test.image=fopen(argv[1],"r+b");assert(test.image);test.blocks=(uint64_t)st.st_size/512;
    test.cd=!strncmp(argv[4],"cd-",3);test.fault=argv[4]+(test.cd?3:0);
    if(!strcmp(argv[3],"seed")) {FATFS fs;mount_card(&fs);seed(argv[2]);unmount_card();}
    else {assert(!strcmp(argv[3],"check"));check();}
    kui_games_cache_clear();assert(!fclose(test.image));printf("PASS CHD Games %s %s\n",argv[3],argv[4]);return 0;
}
