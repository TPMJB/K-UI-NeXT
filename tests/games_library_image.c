/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/games_library.h"
#include "kui/games_covers.h"
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

/* Actual FatFs and production catalogue APIs on synthetic card images.
 * Fixture edits happen only with active=false; production writes are fenced
 * to KUI, and cold cache hits must not discover or open any Games content. */
static struct {
    FILE *image;
    uint64_t blocks;
    bool active,connected,cancelled,cancel_directory,cancel_readback,fail_write,fail_read,fail_source;
    unsigned block_reads,block_writes,opens,game_opens,dir_opens,readdirs,stats,writes,unlinks,files,dirs,fail_generation;
    struct {FIL *file;bool index,generation,source;} opened[16];
} test;
static FATFS fs;
static void log_line(const char *format,...) {
    va_list args;va_start(args,format);vprintf(format,args);va_end(args);putchar('\n');
}
static uint64_t blocks(void *ctx) {(void)ctx;return test.blocks;}
static int read_image(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;if(test.active)++test.block_reads;
    return fseeko(test.image,(off_t)block*512,SEEK_SET) || fread(data,512,count,test.image)!=count?-1:0;
}
static int write_image(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;if(test.active)++test.block_writes;
    return fseeko(test.image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,test.image)!=count?-1:0;
}
static int sync_image(void *ctx) {(void)ctx;return fflush(test.image)||fsync(fileno(test.image))?-1:0;}
static const struct kui_media_ops media={NULL,blocks,read_image,write_image,sync_image};
unsigned kui_storage_active(void) {return KUI_STORAGE_SCIF;}
bool kui_sd_connect(void) {assert(!test.connected);test.connected=true;kui_media_set(&media);return true;}
void kui_sd_disconnect(void) {assert(test.connected && !test.files && !test.dirs);test.connected=false;kui_media_set(NULL);}
static bool cancel(void) {return test.cancelled;}
static bool library_file(FIL *file) {
    for(unsigned i=0;i<16;i++)if(test.opened[i].file==file)return test.opened[i].index;
    return false;
}
static bool generation_file(FIL *file) {
    for(unsigned i=0;i<16;i++)if(test.opened[i].file==file)return test.opened[i].generation;
    return false;
}
static bool source_file(FIL *file) {
    for(unsigned i=0;i<16;i++)if(test.opened[i].file==file)return test.opened[i].source;
    return false;
}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE flags) {
    if(test.active) {
        ++test.opens;if(!strncmp(path,"0:/Games/",9))++test.game_opens;
        if(flags&FA_WRITE)assert(!strncmp(path,"0:/KUI/",7));
    }
    FRESULT result=__real_f_open(file,path,flags);
    if(test.active && result==FR_OK) {
        ++test.files;unsigned i=0;for(;i<16 && test.opened[i].file;i++);
        assert(i<16);test.opened[i].file=file;
        test.opened[i].index=strstr(path,"/games-library/") && !strstr(path,"/generation-");
        test.opened[i].generation=strstr(path,"/games-library/generation-")!=NULL;
        test.opened[i].source=strstr(path,"/Games/") && strstr(path,".gdi");
    }
    return result;
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    FRESULT result=__real_f_close(file);
    if(test.active) {
        assert(test.files);--test.files;unsigned i=0;for(;i<16 && test.opened[i].file!=file;i++);
        assert(i<16);test.opened[i].file=NULL;
        test.opened[i].index=test.opened[i].generation=test.opened[i].source=false;
    }
    return result;
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *out,UINT count,UINT *got) {
    if(test.active && test.fail_read && library_file(file)) {*got=0;return FR_DISK_ERR;}
    if(test.active && test.fail_source && source_file(file)) {*got=0;return FR_DISK_ERR;}
    if(test.active && test.fail_generation==1 && generation_file(file)) {*got=0;return FR_DISK_ERR;}
    FRESULT result=__real_f_read(file,out,count,got);
    if(test.active && test.fail_generation==2 && generation_file(file) && result==FR_OK && *got)--*got;
    if(test.active && test.cancel_readback && library_file(file) && result==FR_OK && *got>=8 && !memcmp(out,"KUIGTMP2",8))
        test.cancelled=true;
    return result;
}
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *file,const void *data,UINT count,UINT *put) {
    if(test.active) {
        ++test.writes;
        if(test.fail_write) {*put=0;return FR_DISK_ERR;}
    }
    return __real_f_write(file,data,count,put);
}
FRESULT __real_f_opendir(DIR *,const TCHAR *);
FRESULT __wrap_f_opendir(DIR *dir,const TCHAR *path) {
    FRESULT result=__real_f_opendir(dir,path);
    if(test.active) {++test.dir_opens;if(result==FR_OK)++test.dirs;}
    return result;
}
FRESULT __real_f_closedir(DIR *);
FRESULT __wrap_f_closedir(DIR *dir) {
    FRESULT result=__real_f_closedir(dir);if(test.active) {assert(test.dirs);--test.dirs;}return result;
}
FRESULT __real_f_readdir(DIR *,FILINFO *);
FRESULT __wrap_f_readdir(DIR *dir,FILINFO *info) {
    FRESULT result=__real_f_readdir(dir,info);
    if(test.active) {++test.readdirs;if(test.cancel_directory)test.cancelled=true;}
    return result;
}
FRESULT __real_f_stat(const TCHAR *,FILINFO *);
FRESULT __wrap_f_stat(const TCHAR *path,FILINFO *info) {if(test.active)++test.stats;return __real_f_stat(path,info);}
FRESULT __real_f_mkdir(const TCHAR *);
FRESULT __wrap_f_mkdir(const TCHAR *path) {
    if(test.active)assert(!strcmp(path,"0:/KUI") || !strncmp(path,"0:/KUI/",7));
    return __real_f_mkdir(path);
}
FRESULT __real_f_unlink(const TCHAR *);
FRESULT __wrap_f_unlink(const TCHAR *path) {
    if(test.active) {++test.unlinks;assert(!strncmp(path,"0:/KUI/",7));}
    return __real_f_unlink(path);
}
FRESULT __real_f_rename(const TCHAR *,const TCHAR *);
FRESULT __wrap_f_rename(const TCHAR *old,const TCHAR *next) {
    if(test.active)assert(!strncmp(old,"0:/KUI/",7) && !strncmp(next,"0:/KUI/",7));
    return __real_f_rename(old,next);
}
static void mounted(bool on) {
    assert(!test.active && !test.connected);
    if(on) {kui_media_set(&media);assert(kui_mount(&fs,log_line));}
    else {assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);}
}
static void write_file(const char *path,const void *data,size_t bytes) {
    FIL file;UINT put=0;assert(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    assert(f_write(&file,data,(UINT)bytes,&put)==FR_OK && put==bytes);assert(f_close(&file)==FR_OK);
}
static void seed(const char *host) {
    char path[1024],line[600];snprintf(path,sizeof(path),"%s/manifest.txt",host);
    FILE *list=fopen(path,"r");assert(list);
    while(fgets(line,sizeof(line),list)) {
        line[strcspn(line,"\n")]=0;assert((line[0]=='D'||line[0]=='F') && line[1]==' ');
        char target[600];assert(snprintf(target,sizeof(target),"0:/%s",line+2)<(int)sizeof(target));
        if(line[0]=='D') {assert(f_mkdir(target)==FR_OK);continue;}
        assert(snprintf(path,sizeof(path),"%s/%s",host,line+2)<(int)sizeof(path));
        FILE *input=fopen(path,"rb");assert(input);assert(!fseek(input,0,SEEK_END));long size=ftell(input);assert(size>=0);rewind(input);
        uint8_t *data=malloc((size_t)size+1u);assert(data);assert(fread(data,1,(size_t)size,input)==(size_t)size && !fclose(input));
        write_file(target,data,(size_t)size);free(data);
    }
    assert(!fclose(list));
}
static void clear(void) {assert(kui_games_storage_release());kui_games_library_clear();kui_games_cache_clear();kui_games_covers_cache_clear();}
static void page(unsigned offset,struct kui_games_page *out) {assert(kui_games_library_rows("/Games","",offset,KUI_GAMES_VIEW_LIST,out,log_line,cancel));}
static bool find(const char *name,struct kui_games_entry *out) {
    for(unsigned offset=0;;offset+=KUI_GAMES_ROWS) {
        struct kui_games_page result;page(offset,&result);
        for(unsigned i=0;i<result.count;i++)if(!strcmp(result.entries[i].name,name) || !strcmp(result.entries[i].title,name)) {if(out)*out=result.entries[i];return true;}
        if(!result.has_more)return false;
    }
}
static void add_game(bool remove_old) {
    assert(kui_games_storage_release());test.active=false;mounted(true);
    assert(f_mkdir("0:/Games/New Arrival")==FR_OK);write_file("0:/Games/New Arrival/selected.iso","SYNTHETIC",9);
    if(remove_old) {assert(f_unlink("0:/Games/Standalone/selected.iso")==FR_OK);assert(f_unlink("0:/Games/Standalone")==FR_OK);}
    mounted(false);test.active=true;
}
static void index_path(char path[128],unsigned slot) {snprintf(path,128,"0:/KUI/games-library/%08lx-%c.bin",(unsigned long)kui_crc32(0,"/Games",6),slot?'b':'a');}
static void corrupt(const char *path,const char *magic) {
    assert(kui_games_storage_release());test.active=false;mounted(true);
    FIL file;UINT put=0;assert(f_open(&file,path,FA_WRITE)==FR_OK);
    assert(f_write(&file,magic,8,&put)==FR_OK && put==8 && f_close(&file)==FR_OK);
    mounted(false);test.active=true;
}
static void serial_mismatch(void) {
    assert(kui_games_storage_release());test.active=false;
    uint8_t boot[24u*512u];assert(!fseeko(test.image,0,SEEK_SET) && fread(boot,1,sizeof(boot),test.image)==sizeof(boot));
    if(!memcmp(boot+3,"EXFAT   ",8)) {
        for(unsigned region=0;region<2;region++) {
            uint8_t *area=boot+region*12u*512u;area[100]^=0x5a;
            uint32_t checksum=0;
            for(unsigned i=0;i<11u*512u;i++) {
                if(i==106 || i==107 || i==112)continue;
                checksum=(checksum>>1)|(checksum<<31);checksum+=area[i];
            }
            for(unsigned i=11u*512u;i<12u*512u;i+=4)
                for(unsigned j=0;j<4;j++)area[i+j]=(uint8_t)(checksum>>(8*j));
        }
    } else {
        assert(!memcmp(boot+82,"FAT32   ",8));boot[67]^=0x5a;
        unsigned backup=(unsigned)boot[50]|(unsigned)boot[51]<<8;
        assert(backup && backup<24);boot[backup*512u+67]^=0x5a;
    }
    assert(!fseeko(test.image,0,SEEK_SET) && fwrite(boot,1,sizeof(boot),test.image)==sizeof(boot));
    assert(!fflush(test.image) && !fsync(fileno(test.image)));test.active=true;
}
static void cold_check(void) {
    clear();unsigned dirs=test.readdirs,dir_opens=test.dir_opens,stats=test.stats,games=test.game_opens,writes=test.writes;
    struct kui_games_page result;page(0,&result);assert(result.count && result.total==16);
    assert(test.readdirs==dirs && test.dir_opens==dir_opens && test.stats==stats && test.game_opens==games && test.writes==writes);
    printf("EVIDENCE cold catalogue: directory_opens=%u readdir_calls=%u stat_calls=%u game_image_opens=%u writes=%u\n",
        test.dir_opens-dir_opens,test.readdirs-dirs,test.stats-stats,test.game_opens-games,test.writes-writes);
    unsigned reads=test.block_reads,opens=test.opens;
    page(8,&result);assert(result.count==8 && !result.has_more);
    struct kui_games_entry group;assert(find("Skies of Arcadia",&group) && group.group[0]);
    struct kui_games_page discs;assert(kui_games_library_rows("/Games",group.group,0,KUI_GAMES_VIEW_LIST,&discs,log_line,cancel));
    assert(discs.count==2 && discs.entries[0].disc==1 && discs.entries[1].disc==2);
    assert(!strcmp(discs.entries[0].title,"Disc 1") && !strcmp(discs.entries[1].title,"Disc 2"));
    assert(strstr(discs.entries[0].path,"Disc 1/") && strstr(discs.entries[1].path,"Disc 2/"));
    assert(strstr(discs.entries[1].variant_2048_path,"-2048/") && !discs.entries[0].group[0]);
    unsigned selected=1234;assert(kui_games_library_find("/Games",group.group,discs.entries[1].name,&selected,log_line,cancel) && selected==1);
    assert(test.block_reads==reads && test.opens==opens && test.writes==writes);
    printf("EVIDENCE warm page/disc chooser: block_reads=%u file_opens=%u writes=%u\n",
        test.block_reads-reads,test.opens-opens,test.writes-writes);
    struct kui_games_entry japan;assert(find("Skies of Arcadia (Japan)",&japan) && japan.group[0] && strcmp(japan.group,group.group));
    struct kui_games_entry duplicate;assert(find("Duplicate Disc 1",&duplicate) && !duplicate.group[0]);
    assert(find("Duplicate Disc 01",&duplicate) && !duplicate.group[0]);
    assert(find("Duplicate Disc 2",&duplicate) && !duplicate.group[0]);
}
static void check(const char *name) {
    test.active=true;
    if(!strcmp(name,"first-write-fail")) {
        test.fail_write=true;struct kui_games_page result;page(0,&result);assert(result.count && kui_games_library_ready("/Games"));
        unsigned reads=test.block_reads,opens=test.opens;page(0,&result);assert(test.block_reads==reads && test.opens==opens);
        test.fail_write=false;clear();assert(kui_games_library_scan("/Games",false,log_line,cancel));cold_check();
    } else {
        assert(kui_games_library_scan("/Games",false,log_line,cancel));assert(kui_games_library_ready("/Games"));
        if(!strcmp(name,"cold") || !strcmp(name,"groups"))cold_check();
        else if(!strcmp(name,"stale")) {
            add_game(true);assert(find("Standalone",NULL) && !find("New Arrival",NULL));clear();
            assert(find("Standalone",NULL) && !find("New Arrival",NULL));
            assert(kui_games_library_scan("/Games",false,log_line,cancel));assert(!find("Standalone",NULL) && find("New Arrival",NULL));
        } else if(!strcmp(name,"cancel") || !strcmp(name,"cancel-readback")) {
            add_game(false);test.cancel_directory=!strcmp(name,"cancel");test.cancel_readback=!test.cancel_directory;
            assert(!kui_games_library_scan("/Games",false,log_line,cancel) && test.cancelled);
            test.cancelled=test.cancel_directory=test.cancel_readback=false;
            assert(kui_games_library_ready("/Games") && !find("New Arrival",NULL));clear();assert(!find("New Arrival",NULL));
        } else if(!strcmp(name,"cancel-before")) {
            unsigned reads=test.block_reads,writes=test.writes;test.cancelled=true;
            assert(!kui_games_library_scan("/Games",false,log_line,cancel));
            assert(test.block_reads==reads && test.writes==writes);test.cancelled=false;assert(find("Standalone",NULL));
        } else if(!strcmp(name,"load-read-fail")) {
            clear();test.fail_read=true;unsigned writes=test.writes;struct kui_games_page result;
            assert(!kui_games_library_rows("/Games","",0,KUI_GAMES_VIEW_LIST,&result,log_line,cancel));
            assert(test.writes==writes && !kui_games_library_ready("/Games"));test.fail_read=false;cold_check();
        } else if(!strcmp(name,"source-read-fail")) {
            add_game(false);test.fail_source=true;unsigned writes=test.writes;
            assert(!kui_games_library_scan("/Games",false,log_line,cancel));
            assert(test.writes==writes && kui_games_library_ready("/Games"));
            test.fail_source=false;assert(!find("New Arrival",NULL));clear();assert(!find("New Arrival",NULL));
            puts("EVIDENCE source descriptor I/O failure did not seal an incomplete catalogue or replace the previous one");
        } else if(!strcmp(name,"failed-invalidation-readonly")) {
            test.fail_write=true;
            assert(!kui_games_library_invalidate("/Games",log_line) && !kui_games_library_ready("/Games"));
            add_game(true);clear();unsigned writes=test.writes;
            assert(find("New Arrival",NULL) && !find("Standalone",NULL) && test.writes==writes);
            /* Dropping RAM cannot make a stale disk snapshot trusted again. */
            clear();assert(find("New Arrival",NULL) && !find("Standalone",NULL) && test.writes==writes);
            test.fail_write=false;assert(kui_games_library_scan("/Games",true,log_line,cancel));
            clear();unsigned dirs=test.readdirs;
            assert(find("New Arrival",NULL) && !find("Standalone",NULL) && test.readdirs==dirs);
            puts("EVIDENCE failed invalidation kept readonly browsing fresh without cache writes; RAM clear retained distrust until explicit rebuild");
        } else if(!strcmp(name,"corrupt-newer") || !strcmp(name,"pending-newer")) {
            add_game(false);assert(kui_games_library_scan("/Games",false,log_line,cancel) && find("New Arrival",NULL));
            char path[128];index_path(path,1);corrupt(path,!strcmp(name,"pending-newer")?"KUIGTMP2":"BROKEN!!");
            clear();assert(!find("New Arrival",NULL) && find("Standalone",NULL));
        } else if(!strcmp(name,"epoch")) {
            assert(kui_games_library_invalidate("/Games",log_line));add_game(false);unsigned dirs=test.readdirs;
            assert(find("New Arrival",NULL) && test.readdirs>dirs);clear();dirs=test.readdirs;
            assert(find("New Arrival",NULL) && test.readdirs==dirs);
        } else if(!strcmp(name,"volume-identity")) {
            add_game(false);serial_mismatch();clear();unsigned dirs=test.readdirs;
            assert(find("New Arrival",NULL) && test.readdirs>dirs);
            clear();dirs=test.readdirs;assert(find("New Arrival",NULL) && test.readdirs==dirs);
            puts("EVIDENCE changed volume serial rejected the stale catalogue; fresh scan persisted correctly");
        } else if(!strcmp(name,"generation-read-fail")) {
            assert(kui_games_library_invalidate("/Games",log_line));assert(kui_games_library_scan("/Games",false,log_line,cancel));
            for(unsigned fault=1;fault<=2;fault++) {
                clear();test.fail_generation=fault;unsigned writes=test.writes,unlinks=test.unlinks,blocks_written=test.block_writes;
                struct kui_games_page result;
                assert(!kui_games_library_rows("/Games","",0,KUI_GAMES_VIEW_LIST,&result,log_line,cancel));
                assert(!kui_games_library_scan("/Games",true,log_line,cancel));
                assert(test.writes==writes && test.unlinks==unlinks && test.block_writes==blocks_written);
                test.fail_generation=0;
            }
            cold_check();puts("EVIDENCE generation I/O failure and short read caused zero writes/deletes, including Rebuild");
        } else if(!strcmp(name,"corrupt-epoch")) {
            /* Keep another root's epoch-zero snapshot under uppercase FAT
             * spelling. Ledger recovery must purge it before epoch zero can
             * be admitted again, including its uppercase hex/slot/extension. */
            const char *other_root="/Games/Filler 00";
            assert(kui_games_library_scan(other_root,false,log_line,cancel));
            char lower_cache[128],upper_cache[128];
            uint32_t other_hash=kui_crc32(0,other_root,strlen(other_root));
            snprintf(lower_cache,sizeof(lower_cache),"0:/KUI/games-library/%08lx-a.bin",(unsigned long)other_hash);
            snprintf(upper_cache,sizeof(upper_cache),"0:/KUI/games-library/%08lX-A.BIN",(unsigned long)other_hash);
            assert(kui_games_storage_release());test.active=false;mounted(true);
            assert(f_rename(lower_cache,"0:/KUI/games-library/case-rename.tmp")==FR_OK);
            assert(f_rename("0:/KUI/games-library/case-rename.tmp",upper_cache)==FR_OK);
            FILINFO cache_info;assert(f_stat(upper_cache,&cache_info)==FR_OK);
            mounted(false);test.active=true;
            assert(kui_games_library_invalidate("/Games",log_line));assert(kui_games_library_scan("/Games",false,log_line,cancel));
            assert(kui_games_library_invalidate("/Games",log_line));
            corrupt("0:/KUI/games-library/generation-a.bin","BROKEN!!");corrupt("0:/KUI/games-library/generation-b.bin","BROKEN!!");
            clear();struct kui_games_page result;unsigned writes=test.writes;
            assert(kui_games_library_rows("/Games","",0,KUI_GAMES_VIEW_LIST,&result,log_line,cancel) && result.count);
            assert(test.writes==writes && !kui_games_library_scan("/Games",false,log_line,cancel));
            assert(kui_games_library_scan("/Games",true,log_line,cancel));assert(find("Standalone",NULL));
            assert(kui_games_storage_release());test.active=false;mounted(true);
            assert(f_stat(upper_cache,&cache_info)==FR_NO_FILE);
            mounted(false);test.active=true;
            puts("EVIDENCE corrupt-ledger recovery purged an uppercase old-generation catalogue for another root");
        } else assert(!"unknown catalogue test");
    }
    clear();assert(!test.connected && !test.files && !test.dirs);
    printf("PASS Games catalogue %s; production writes stayed under KUI\n",name);
}
int main(int argc,char **argv) {
    assert(argc==5);test.image=fopen(argv[1],"r+b");assert(test.image);
    struct stat info;assert(!fstat(fileno(test.image),&info) && info.st_size>0 && !(info.st_size%512));test.blocks=(uint64_t)info.st_size/512;
    if(!strcmp(argv[3],"seed")) {mounted(true);seed(argv[2]);mounted(false);}
    else {assert(!strcmp(argv[3],"check"));check(argv[4]);}
    assert(!fclose(test.image));return 0;
}
