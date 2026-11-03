/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/games_library.h"
#include "kui/games_covers.h"
#include "kui/games_storage.h"
#include "kui/games_scan_log.h"
#include "kui/hash.h"
#include "kui/media.h"
#include "kui/storage.h"
#include "kui/game_launch_settings.h"
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
    bool active,connected,cancelled,cancel_directory,cancel_readback,fail_write,fail_read,fail_source,fail_group_ip,cancel_group_ip;
    unsigned block_reads,block_writes,opens,game_opens,dir_opens,readdirs,stats,writes,non_trace_writes,trace_writes,unlinks,files,dirs,fail_generation,group_reads,group_bytes;
    struct {FIL *file;bool index,generation,source,ip,trace;} opened[16];
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
        test.opened[i].ip=strstr(path,"/Games/") && strstr(path,"track03.bin");
        test.opened[i].trace=!strcmp(path,"0:/KUI/games-library/scan-a.txt") || !strcmp(path,"0:/KUI/games-library/scan-b.txt");
    }
    return result;
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    FRESULT result=__real_f_close(file);
    if(test.active) {
        assert(test.files);--test.files;unsigned i=0;for(;i<16 && test.opened[i].file!=file;i++);
        assert(i<16);test.opened[i].file=NULL;
        test.opened[i].index=test.opened[i].generation=test.opened[i].source=test.opened[i].ip=test.opened[i].trace=false;
    }
    return result;
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *out,UINT count,UINT *got) {
    bool ip=false;for(unsigned i=0;i<16;i++)if(test.opened[i].file==file)ip=test.opened[i].ip;
    if(test.active && test.fail_group_ip && ip) {*got=0;return FR_DISK_ERR;}
    if(test.active && test.fail_read && library_file(file)) {*got=0;return FR_DISK_ERR;}
    if(test.active && test.fail_source && source_file(file)) {*got=0;return FR_DISK_ERR;}
    if(test.active && test.fail_generation==1 && generation_file(file)) {*got=0;return FR_DISK_ERR;}
    FRESULT result=__real_f_read(file,out,count,got);
    if(test.active && ip) {++test.group_reads;test.group_bytes+=*got;}
    if(test.active && test.cancel_group_ip && ip)test.cancelled=true;
    if(test.active && test.fail_generation==2 && generation_file(file) && result==FR_OK && *got)--*got;
    if(test.active && test.cancel_readback && library_file(file) && result==FR_OK && *got>=8 && !memcmp(out,"KUIGTMP2",8))
        test.cancelled=true;
    return result;
}
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *file,const void *data,UINT count,UINT *put) {
    if(test.active) {
        ++test.writes;
        bool trace=false;for(unsigned i=0;i<16;i++)if(test.opened[i].file==file)trace=test.opened[i].trace;
        if(trace)++test.trace_writes;else ++test.non_trace_writes;
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
static void breadcrumb_check(const char *phase,const char *image) {
    clear();test.active=false;mounted(true);test.active=true;
    unsigned writes=test.writes,blocks_written=test.block_writes;
    char *text=malloc(KUI_GAMES_SCAN_LOG_TEXT_MAX+1u);size_t bytes=0;assert(text);
    assert(kui_games_scan_log_load(text,KUI_GAMES_SCAN_LOG_TEXT_MAX+1u,&bytes,log_line) && bytes);
    char expected[80];snprintf(expected,sizeof(expected),"\nphase=%s\n",phase);assert(strstr(text,expected));
    assert(strstr(text,"build=") && strstr(text,"last_work_phase=") && strstr(text,"root=/Games\n") && strstr(text,"transport="));
    if(image)assert(strstr(text,image));
    assert(test.writes==writes && test.block_writes==blocks_written);
    free(text);test.active=false;mounted(false);test.active=true;
    printf("EVIDENCE durable Games scan phase=%s%s survived full cache clear/remount; breadcrumb read caused zero writes\n",phase,image?" with exact candidate image path":"");
}
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
static void activity_check(const char *name) {
    struct kui_games_entry skies,standalone;
    assert(find("Skies of Arcadia",&skies) && skies.group[0]);
    assert(find("Standalone",&standalone) && !standalone.group[0]);
    struct kui_games_page result;
    assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_FAVORITES,&result,log_line,cancel));
    assert(!result.count && !result.total && result.filter==KUI_GAMES_FILTER_FAVORITES);
    assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_RECENT,&result,log_line,cancel));
    assert(!result.count && !result.total);
    assert(kui_games_library_favorite_toggle("/Games",skies.group,skies.path,log_line));

    if(!strcmp(name,"activity-write-fail")) {
        test.fail_write=true;
        assert(!kui_games_library_favorite_toggle("/Games",skies.group,skies.path,log_line));
        assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_FAVORITES,&result,log_line,cancel));
        assert(result.count==1 && result.entries[0].favorite && !strcmp(result.entries[0].group,skies.group));
        test.fail_write=false;clear();
        assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_FAVORITES,&result,log_line,cancel));
        assert(result.count==1 && result.entries[0].favorite);
        assert(kui_games_library_favorite_toggle("/Games",skies.group,skies.path,log_line));
        clear();
        assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_FAVORITES,&result,log_line,cancel));
        assert(!result.count);
        puts("EVIDENCE failed favorite write kept RAM and last good disk generation; next successful removal persisted");
        return;
    }
    assert(kui_games_library_favorite_toggle("/Games","",standalone.path,log_line));
    if(!strcmp(name,"activity-corrupt")) {
        corrupt("0:/KUI/games-library/activity-b.bin","BROKEN!!");clear();
        assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_FAVORITES,&result,log_line,cancel));
        assert(result.count==1 && !strcmp(result.entries[0].group,skies.group));
        corrupt("0:/KUI/games-library/activity-a.bin","BROKEN!!");clear();
        assert(find("Standalone",NULL));unsigned writes=test.writes;
        assert(!kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_FAVORITES,&result,log_line,cancel));
        assert(!kui_games_library_favorite_toggle("/Games",skies.group,skies.path,log_line));
        assert(test.writes==writes);
        puts("EVIDENCE damaged latest activity recovered its prior generation; two damaged records did not overwrite user history or hide All games");
        return;
    }

    assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_FAVORITES,&result,log_line,cancel));
    assert(result.count==2 && result.total==2 && !result.has_more);
    assert(!strcmp(result.entries[0].group,skies.group) && !strcmp(result.entries[1].path,standalone.path));
    assert(result.entries[0].favorite && result.entries[1].favorite);
    struct kui_games_page discs;
    assert(kui_games_library_rows_filter("/Games",skies.group,0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_FAVORITES,&discs,log_line,cancel));
    assert(discs.count==2 && discs.entries[0].disc==1 && discs.entries[1].disc==2 && discs.entries[0].favorite && discs.entries[1].favorite);
    struct kui_native_session_request session;
    assert(kui_games_library_session_request("/Games",discs.entries[0].path,&session,log_line,cancel));
    assert(session.count==2u && session.selected==0u && !strcmp(session.paths[0],discs.entries[0].path) &&
        !strcmp(session.paths[1],discs.entries[1].path));
    assert(kui_games_library_session_request("/Games",discs.entries[1].variant_2048_path,&session,log_line,cancel));
    assert(session.count==2u && session.selected==1u && !strcmp(session.paths[1],discs.entries[1].variant_2048_path) &&
        !strcmp(session.paths[0],discs.entries[0].path));
    assert(kui_games_library_session_request("/Games",standalone.path,&session,log_line,cancel));
    assert(session.count==1u && !session.selected && !strcmp(session.paths[0],standalone.path));
    struct kui_games_activity_entry first,second,alternate;
    assert(kui_games_library_activity_key("/Games",discs.entries[0].path,&first,log_line));
    assert(kui_games_library_activity_key("/Games",discs.entries[1].path,&second,log_line));
    assert(!strcmp(first.path,"/games") && !strcmp(first.path,second.path) && !strcmp(first.group,second.group));
    assert(discs.entries[1].variant_2048_path[0]);
    assert(kui_games_library_activity_key("/Games",discs.entries[1].variant_2048_path,&alternate,log_line));
    assert(!strcmp(first.path,alternate.path) && !strcmp(first.group,alternate.group));
    assert(kui_games_library_record_played(&second,log_line));
    assert(kui_games_library_activity_key("/Games",standalone.path,&second,log_line));
    assert(kui_games_library_record_played(&second,log_line));
    assert(kui_games_library_record_played(&first,log_line));
    clear();unsigned dirs=test.readdirs,source_reads=test.game_opens,writes=test.writes;
    assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_RECENT,&result,log_line,cancel));
    assert(result.count==2 && result.total==2 && !strcmp(result.entries[0].group,skies.group));
    assert(!strcmp(result.entries[1].path,standalone.path));
    assert(test.readdirs==dirs && test.game_opens==source_reads && test.writes==writes);
    unsigned offset=100;
    assert(kui_games_library_find_filter("/Games","",discs.entries[1].name,KUI_GAMES_FILTER_RECENT,&offset,log_line,cancel) && !offset);
    assert(kui_games_library_rows_filter("/Games",skies.group,0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_RECENT,&discs,log_line,cancel));
    assert(discs.count==2 && discs.entries[0].disc==1 && discs.entries[1].disc==2);
    assert(kui_games_library_invalidate("/Games",log_line));
    assert(kui_games_library_scan("/Games",true,log_line,cancel));clear();
    assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_FAVORITES,&result,log_line,cancel));
    assert(result.count==2);
    add_game(true);assert(kui_games_library_scan("/Games",false,log_line,cancel));clear();
    assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_RECENT,&result,log_line,cancel));
    assert(result.count==1 && !strcmp(result.entries[0].group,skies.group));
    puts("EVIDENCE favorites and recent launches survived cold reload/rebuild; disc 1/disc 2/2048 share one recent group; removed images did not become ghost rows");
}
static void query_page(const char *group,const struct kui_games_query *query,struct kui_games_page *out) {
    assert(kui_games_library_rows_query("/Games",group,0,KUI_GAMES_VIEW_LIST,query,out,log_line,cancel));
}
static void query_check(void) {
    struct kui_games_entry verified,group;assert(find("Verified",&verified) && find("Skies of Arcadia",&group));
    char anchor[KUI_GAMES_FILE_CAP];assert(kui_games_library_verify_identity("/Games",verified.path,KUI_ZSO_LZ4,anchor,log_line,cancel));
    struct kui_games_query query;struct kui_games_page result;kui_games_query_default(&query);
    query_page("",&query,&result); /* An edit invalidates the saved-view cache once. */
    unsigned reads=test.block_reads,opens=test.opens,dirs=test.readdirs,stats=test.stats,writes=test.writes;
    strcpy(query.text,"fIlLeR 0");query_page("",&query,&result);assert(result.total==10 && result.count==8 && result.has_more);
    query.region=KUI_GAMES_REGION_USA;query_page("",&query,&result);assert(!result.total);
    query.text[0]=0;query_page("",&query,&result);assert(result.total==1 && !strcmp(result.entries[0].path,verified.path));
    query.region=KUI_GAMES_REGION_JAPAN;query_page("",&query,&result);assert(result.total==1);
    query.region=KUI_GAMES_REGION_EUROPE;query_page("",&query,&result);assert(result.total==1);
    query.region=KUI_GAMES_REGION_UNKNOWN;query_page("",&query,&result);assert(result.total==17);
    query.region=KUI_GAMES_REGION_ANY;strcpy(query.text,"skies");query.disc=2;query_page("",&query,&result);assert(result.total==2 && result.entries[0].group[0]);
    query_page(group.group,&query,&result);assert(result.count==1 && result.entries[0].disc==2 && strstr(result.entries[0].path,"Disc 2/"));
    unsigned offset=777;assert(kui_games_library_find_query("/Games",group.group,result.entries[0].name,&query,&offset,log_line,cancel) && !offset);
    kui_games_query_default(&query);assert(kui_games_library_jump_query("/Games","",'V',&query,&offset,log_line,cancel));
    assert(kui_games_library_rows_query("/Games","",offset,KUI_GAMES_VIEW_LIST,&query,&result,log_line,cancel));assert(!strcmp(result.entries[0].path,verified.path));
    assert(!kui_games_library_jump_query("/Games","",'Z',&query,&offset,log_line,cancel));
    assert(test.block_reads==reads && test.opens==opens && test.readdirs==dirs && test.stats==stats && test.writes==writes);
    clear();query.region=KUI_GAMES_REGION_USA;query_page("",&query,&result);assert(result.total==1 && !strcmp(result.entries[0].region,"JUE"));
    puts("EVIDENCE cached text/region/disc queries and alphabet jumps performed zero block/file/discovery I/O; verified multi-region metadata survived cold reload");
}
static void editor_check(const char *name) {
    struct kui_games_entry selected,group;assert(find("Standalone",&selected) && find("Skies of Arcadia",&group));
    char real_path[KUI_GAMES_FILE_CAP];strcpy(real_path,selected.path);
    struct kui_games_library_edit edit={0},readback;
    if(!strcmp(name,"editor-corrupt")) {
        strcpy(edit.alias,"First complete alias");assert(kui_games_library_set_edit("/Games","",real_path,&edit,log_line,cancel));
        strcpy(edit.alias,"Second complete alias");assert(kui_games_library_set_edit("/Games","",real_path,&edit,log_line,cancel));
        corrupt("0:/KUI/games-library/metadata-b.bin","BROKEN!!");clear();
        assert(kui_games_library_get_edit("/Games","",real_path,&readback,log_line) && !strcmp(readback.alias,"First complete alias"));
        corrupt("0:/KUI/games-library/metadata-a.bin","BROKEN!!");clear();assert(find("Standalone",&selected));
        unsigned writes=test.writes;assert(!kui_games_library_set_edit("/Games","",real_path,&edit,log_line,cancel));assert(test.writes==writes);
        puts("EVIDENCE corrupt newer metadata recovered the older alias; two corrupt slots did not overwrite metadata or hide All games");return;
    }
    strcpy(edit.alias,"AAA Custom Release");strcpy(edit.cover_path,"/KUI/covers/assigned.png");
    assert(kui_games_library_set_edit("/Games","",real_path,&edit,log_line,cancel));
    assert(kui_games_library_get_edit("/Games","",real_path,&readback,log_line));
    assert(!strcmp(readback.alias,edit.alias) && !strcmp(readback.cover_path,"/kui/covers/assigned.png"));
    assert(find("Standalone",&selected) && !strcmp(selected.path,real_path) && !strcmp(selected.name,"Standalone") &&
        selected.custom_title && !strcmp(selected.title,edit.alias) && !strcmp(selected.cover_path,readback.cover_path));
    if(!strcmp(name,"editor-write-fail")) {
        test.fail_write=true;strcpy(edit.alias,"Rejected replacement");
        assert(!kui_games_library_set_edit("/Games","",real_path,&edit,log_line,cancel));
        assert(kui_games_library_get_edit("/Games","",real_path,&readback,log_line) && !strcmp(readback.alias,"AAA Custom Release"));
        test.fail_write=false;clear();assert(kui_games_library_get_edit("/Games","",real_path,&readback,log_line));
        assert(!strcmp(readback.alias,"AAA Custom Release"));puts("EVIDENCE failed metadata write kept the old alias and cover in RAM and after cold reload");return;
    }
    struct kui_games_query query;kui_games_query_default(&query);strcpy(query.text,"CUSTOM");struct kui_games_page result;
    unsigned opens=test.opens,reads=test.block_reads;query_page("",&query,&result);
    assert(result.count==1 && !strcmp(result.entries[0].path,real_path));unsigned offset=99;query.text[0]=0;
    assert(kui_games_library_jump_query("/Games","",'A',&query,&offset,log_line,cancel) && !offset);
    assert(test.opens==opens && test.block_reads==reads);
    uint16_t *pixels=malloc(KUI_COVER_PIXELS*sizeof(*pixels));assert(pixels);struct kui_games_cover_result cover;
    assert(kui_games_selected_cover(&selected,KUI_COVER_SIZE_SMALL,pixels,&cover,log_line,cancel) && cover.cover && !strcmp(cover.title,edit.alias));
    for(unsigned i=0;i<KUI_COVER_SMALL*KUI_COVER_SMALL;i++)assert(pixels[i]==0xf800u);
    opens=test.opens;reads=test.block_reads;memset(pixels,0,KUI_COVER_PIXELS*sizeof(*pixels));
    assert(kui_games_selected_cover(&selected,KUI_COVER_SIZE_SMALL,pixels,&cover,log_line,cancel) && cover.cover);
    assert(test.opens==opens && test.block_reads==reads && pixels[0]==0xf800u);free(pixels);
    strcpy(edit.alias,"Do not publish invalid art");strcpy(edit.cover_path,"/kui/covers/bad.png");
    assert(!kui_games_library_set_edit("/Games","",real_path,&edit,log_line,cancel));
    assert(kui_games_library_get_edit("/Games","",real_path,&readback,log_line) && !strcmp(readback.alias,"AAA Custom Release"));
    test.cancelled=true;assert(!kui_games_library_set_edit("/Games","",real_path,&edit,log_line,cancel));test.cancelled=false;
    edit=(struct kui_games_library_edit){0};strcpy(edit.alias,"Air Pirates");
    assert(kui_games_library_set_edit("/Games",group.group,group.path,&edit,log_line,cancel));
    strcpy(query.text,"air pirates");query_page("",&query,&result);
    assert(result.total==1 && !strcmp(result.entries[0].group,group.group) && !strcmp(result.entries[0].path,group.path));
    query_page(group.group,&query,&result);assert(result.count==2 && !strcmp(result.group_name,"Air Pirates") &&
        result.entries[0].disc==1 && result.entries[1].disc==2 && !strcmp(result.entries[0].title,"Disc 1"));
    clear();assert(find("Standalone",&selected) && !strcmp(selected.path,real_path) && !strcmp(selected.title,"AAA Custom Release"));
    assert(kui_games_library_get_edit("/Games",group.group,group.path,&readback,log_line) && !strcmp(readback.alias,"Air Pirates"));
    puts("EVIDENCE aliases and valid assigned PNG pixels round-tripped without changing real paths; cached queries/art stayed in RAM; invalid art/cancel stayed unpublished; group aliases kept the disc chooser");
}
static void move_verified(bool change_content) {
    assert(kui_games_library_invalidate("/Games",log_line));
    assert(kui_games_storage_release());test.active=false;mounted(true);
    if(change_content) {
        FIL file;UINT got=0,put=0;uint8_t byte;
        assert(f_open(&file,"0:/Games/Verified/track03.bin",FA_READ|FA_WRITE)==FR_OK);
        assert(f_lseek(&file,48u*2352u+16u+77u)==FR_OK && f_read(&file,&byte,1,&got)==FR_OK && got==1);
        byte^=0x5a;assert(f_lseek(&file,48u*2352u+16u+77u)==FR_OK && f_write(&file,&byte,1,&put)==FR_OK && put==1);
        assert(f_close(&file)==FR_OK);
    }
    assert(f_rename("0:/Games/Verified","0:/Games/Moved")==FR_OK);
    mounted(false);test.active=true;assert(kui_games_library_scan("/Games",false,log_line,cancel));
}
static void replace_with_cooked(void) {
    assert(kui_games_library_invalidate("/Games",log_line));
    assert(kui_games_storage_release());test.active=false;mounted(true);
    assert(f_rename("0:/Games/Verified","0:/Staging/RawKept")==FR_OK);
    assert(f_rename("0:/Staging/Cooked","0:/Games/Moved")==FR_OK);
    mounted(false);test.active=true;assert(kui_games_library_scan("/Games",false,log_line,cancel));
}
static void identity_pair_check(void) {
    struct kui_games_entry group;assert(find("Skies of Arcadia",&group));struct kui_games_page discs;
    assert(kui_games_library_rows("/Games",group.group,0,KUI_GAMES_VIEW_LIST,&discs,log_line,cancel));
    assert(discs.count==2 && discs.entries[1].variant_2048_path[0]);
    char raw[KUI_GAMES_FILE_CAP],cooked[KUI_GAMES_FILE_CAP],anchor[KUI_GAMES_FILE_CAP];
    strcpy(raw,discs.entries[1].path);strcpy(cooked,discs.entries[1].variant_2048_path);
    struct kui_games_activity_entry raw_key,cooked_key;
    assert(kui_games_activity_key(&raw_key,raw,"") && kui_games_activity_key(&cooked_key,cooked,""));
    assert(kui_games_library_verify_identity("/Games",raw,KUI_ZSO_LZ4,anchor,log_line,cancel) && !strcmp(anchor,raw_key.path));
    assert(kui_games_library_verify_identity("/Games",cooked,KUI_ZSO_LZ4,anchor,log_line,cancel) && !strcmp(anchor,cooked_key.path));
    assert(strcmp(raw_key.path,cooked_key.path));
    struct kui_game_launch_settings a,b,loaded;kui_game_launch_settings_default(&a);kui_game_launch_settings_default(&b);
    a.reader=KUI_GAME_READER_BACKGROUND;a.vmu_slot=KUI_GAME_VMU_SLOT_A1;
    assert(kui_games_storage_open(log_line)==KUI_GAMES_STORAGE_OK && kui_game_launch_settings_save(raw_key.path,&a,log_line) &&
        kui_game_launch_settings_save(cooked_key.path,&b,log_line));assert(kui_games_storage_done(true));
    assert(kui_games_library_settings_path(raw,anchor,log_line) && !strcmp(anchor,raw_key.path));
    assert(kui_games_storage_open(log_line)==KUI_GAMES_STORAGE_OK && kui_game_launch_settings_load(anchor,&loaded,log_line));
    assert(kui_games_storage_done(true) && loaded.reader==a.reader && loaded.vmu_slot==a.vmu_slot);
    assert(kui_games_library_settings_path(cooked,anchor,log_line) && !strcmp(anchor,cooked_key.path));
    assert(kui_games_storage_open(log_line)==KUI_GAMES_STORAGE_OK && kui_game_launch_settings_load(anchor,&loaded,log_line));
    assert(kui_games_storage_done(true) && loaded.reader==b.reader && loaded.vmu_slot==b.vmu_slot);
    clear();assert(kui_games_library_settings_path(raw,anchor,log_line) && !strcmp(anchor,raw_key.path));
    assert(kui_games_library_settings_path(cooked,anchor,log_line) && !strcmp(anchor,cooked_key.path));
    puts("EVIDENCE paired Original/2048 images verified independently and kept distinct reader/VMU settings anchors across cold reload");
}
static void identity_check(const char *name) {
    if(!strcmp(name,"identity-pair")) {identity_pair_check();return;}
    struct kui_games_entry selected,copy;assert(find("Verified",&selected) && find("Copy",&copy));
    char anchor[KUI_GAMES_FILE_CAP],original_path[KUI_GAMES_FILE_CAP];strcpy(original_path,selected.path);
    assert(kui_games_library_verify_identity("/Games",original_path,KUI_ZSO_LZ4,anchor,log_line,cancel));
    assert(!strcmp(anchor,"/games/verified/disc.gdi"));
    struct kui_games_library_edit edit={0};strcpy(edit.alias,"Remembered verified game");strcpy(edit.cover_path,"/kui/covers/assigned.png");
    assert(kui_games_library_set_edit("/Games","",original_path,&edit,log_line,cancel));
    assert(kui_games_library_favorite_toggle("/Games","",original_path,log_line));
    struct kui_games_activity_entry activity_key;assert(kui_games_library_activity_key("/Games",original_path,&activity_key,log_line));
    assert(kui_games_library_record_played(&activity_key,log_line));
    struct kui_game_launch_settings settings,loaded;kui_game_launch_settings_default(&settings);settings.reader=KUI_GAME_READER_BACKGROUND;
    settings.encoding=KUI_GAME_ENCODING_PLAIN;settings.vmu_slot=KUI_GAME_VMU_SLOT_A1;
    assert(kui_games_storage_open(log_line)==KUI_GAMES_STORAGE_OK && kui_game_launch_settings_save(anchor,&settings,log_line));assert(kui_games_storage_done(true));
    bool duplicate=!strcmp(name,"identity-duplicate"),changed=!strcmp(name,"identity-changed");
    bool representation=!strcmp(name,"identity-representation"),legacy=!strcmp(name,"identity-legacy");
    if(duplicate) {
        char copy_anchor[KUI_GAMES_FILE_CAP];assert(kui_games_library_verify_identity("/Games",copy.path,KUI_ZSO_LZ4,copy_anchor,log_line,cancel));
        assert(!strcmp(copy_anchor,"/games/copy/disc.gdi") && find("Copy",&copy) && !copy.favorite && !copy.custom_title);
    }
    if(legacy) {
        struct kui_games_metadata *old=malloc(sizeof(*old));assert(old && kui_games_storage_open(log_line)==KUI_GAMES_STORAGE_OK);
        assert(kui_games_metadata_load(old,log_line));const struct kui_games_metadata_entry *record=kui_games_metadata_find(old,original_path,"");
        assert(record && record->verified);old->entries[record-old->entries].representation=0;
        assert(kui_games_metadata_save(old,log_line) && kui_games_storage_done(true));free(old);clear();
    }
    if(representation)replace_with_cooked();else move_verified(changed);
    assert(find("Moved",&selected));
    if(!strcmp(name,"identity-conflict")) {
        assert(kui_games_library_favorite_toggle("/Games","",selected.path,log_line));
        assert(kui_games_library_activity_key("/Games",selected.path,&activity_key,log_line) &&
            kui_games_library_record_played(&activity_key,log_line));
        unsigned writes=test.writes;strcpy(anchor,"unchanged");
        assert(!kui_games_library_verify_identity("/Games",selected.path,KUI_ZSO_LZ4,anchor,log_line,cancel));
        assert(!strcmp(anchor,"unchanged") && test.writes==writes);
        assert(kui_games_library_get_edit("/Games","",selected.path,&edit,log_line) && !edit.alias[0] && !edit.cover_path[0]);
        assert(kui_games_library_settings_path(selected.path,anchor,log_line) && !strcmp(anchor,"/games/moved/disc.gdi"));
        struct kui_games_activity *kept=malloc(sizeof(*kept));assert(kept && kui_games_storage_open(log_line)==KUI_GAMES_STORAGE_OK);
        assert(kui_games_activity_load(kept,log_line));
        const struct kui_games_activity_entry *old_activity=kui_games_activity_find(kept,original_path,"");
        const struct kui_games_activity_entry *new_activity=kui_games_activity_find(kept,selected.path,"");
        assert(old_activity && old_activity->favorite && old_activity->played &&
            new_activity && new_activity->favorite && new_activity->played && kept->count==2);
        assert(kui_games_storage_done(true));free(kept);
        clear();assert(find("Moved",&selected) && selected.favorite && !selected.custom_title);
        puts("EVIDENCE independent destination activity blocked automatic identity rebind without writes, hiding preferences or losing the destination favorite");return;
    }
    assert(kui_games_library_verify_identity("/Games",selected.path,KUI_ZSO_LZ4,anchor,log_line,cancel));
    struct kui_games_page result;
    assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_FAVORITES,&result,log_line,cancel));
    if(duplicate || changed || representation || legacy) {
        assert(!strcmp(anchor,"/games/moved/disc.gdi") && !result.count && find("Moved",&selected) && !selected.favorite && !selected.custom_title);
        assert(kui_games_library_get_edit("/Games","",selected.path,&edit,log_line) && !edit.alias[0] && !edit.cover_path[0]);
        puts(duplicate?"EVIDENCE two registered equal identities prevented ambiguous move inheritance; duplicate/new paths kept separate preferences":
            representation?"EVIDENCE cooked replacement with equal canonical payload kept a new settings anchor instead of inheriting raw-image reader/favorite choices":
            legacy?"EVIDENCE legacy verified metadata with unknown representation remained readable but did not automatically rebind moved preferences":
            "EVIDENCE changed whole-image content did not inherit another path's favorite, recent history, alias, cover or settings anchor");return;
    }
    assert(!strcmp(anchor,"/games/verified/disc.gdi") && result.count==1 && !strcmp(result.entries[0].path,selected.path));
    assert(result.entries[0].favorite && !strcmp(result.entries[0].title,"Remembered verified game"));
    assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_RECENT,&result,log_line,cancel));
    assert(result.count==1 && !strcmp(result.entries[0].path,selected.path));
    char resolved[KUI_GAMES_FILE_CAP];assert(kui_games_library_settings_path(selected.path,resolved,log_line) && !strcmp(resolved,anchor));
    assert(kui_games_storage_open(log_line)==KUI_GAMES_STORAGE_OK && kui_game_launch_settings_load(resolved,&loaded,log_line));assert(kui_games_storage_done(true));
    assert(loaded.reader==settings.reader && loaded.encoding==settings.encoding && loaded.vmu_slot==settings.vmu_slot);
    assert(!kui_games_library_settings_path(original_path,resolved,log_line));
    clear();assert(find("Moved",&selected) && selected.favorite && !strcmp(selected.title,"Remembered verified game"));
    assert(kui_games_library_settings_path(selected.path,resolved,log_line) && !strcmp(resolved,anchor));
    puts("EVIDENCE explicit whole-image verified move preserved favorite, recent launch, alias, assigned cover and existing launch settings through the stable anchor; real paths stayed current");
}
static unsigned read16(const uint8_t *p) {return p[0]|(unsigned)p[1]<<8;}
static void write16(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void write32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void catalogue_legacy_replace(unsigned version) {
    assert(kui_games_storage_release());test.active=false;mounted(true);char path[128];index_path(path,0);
    FIL file;UINT got=0;assert(f_open(&file,path,FA_READ)==FR_OK);size_t bytes=(size_t)f_size(&file);
    uint8_t *newer=malloc(bytes),*older=calloc(1,bytes);assert(newer && older);
    assert(f_read(&file,newer,(UINT)bytes,&got)==FR_OK && got==bytes && f_close(&file)==FR_OK);
    assert(!memcmp(newer,"KUIGLIB4",8));memcpy(older,newer,96);memcpy(older,version==3?"KUIGLIB3":"KUIGLIB2",8);write32(older+8,version);
    unsigned root=read16(newer+64),count=read16(newer+66);memcpy(older+96,newer+96,root);size_t source=96u+root,target=source;
    for(unsigned i=0;i<count;i++) {
        unsigned sizes[5];for(unsigned j=0;j<5;j++)sizes[j]=read16(newer+source+2*j);
        unsigned fields=version==3?5u:4u,header=version==3?16u:12u;
        for(unsigned j=0;j<fields;j++)write16(older+target+2*j,sizes[j]);
        older[target+fields*2u]=newer[source+10];
        source+=16;target+=header;
        for(unsigned j=0;j<5;j++) {if(j<fields) {memcpy(older+target,newer+source,sizes[j]);target+=sizes[j];}source+=sizes[j];}
        source+=156;
    }
    assert(source==bytes);write32(older+12,(uint32_t)target);write32(older+68,kui_crc32(0,older+96,target-96));write32(older+72,kui_crc32(0,older,72));
    write_file(path,older,target);free(newer);free(older);mounted(false);test.active=true;clear();
}
static void catalogue_legacy_check(unsigned version) {
    catalogue_legacy_replace(version);cold_check();
    struct kui_games_query query;kui_games_query_default(&query);query.region=KUI_GAMES_REGION_UNKNOWN;struct kui_games_page page;
    query_page("",&query,&page);assert(page.total==16);
    test.fail_write=true;assert(!kui_games_library_scan("/Games",false,log_line,cancel));test.fail_write=false;clear();cold_check();
    assert(kui_games_library_scan("/Games",false,log_line,cancel));clear();cold_check();
    assert(kui_games_storage_release());test.active=false;mounted(true);char path[128];index_path(path,1);uint8_t magic[8];FIL file;UINT got=0;
    assert(f_open(&file,path,FA_READ)==FR_OK && f_read(&file,magic,sizeof(magic),&got)==FR_OK && got==sizeof(magic));
    assert(f_close(&file)==FR_OK && !memcmp(magic,"KUIGLIB4",8));mounted(false);test.active=true;
    printf("EVIDENCE v%u saved catalogue cold-loaded without discovery I/O, survived failed upgrade and upgraded atomically to v4 on explicit scan\n",version);
}
static void capture_activity_check(const char *name) {
    bool explicit_choice=!strcmp(name,"capture-activity-explicit"),capacity=!strcmp(name,"capture-activity-capacity");
    catalogue_legacy_replace(3);
    struct kui_games_entry first,second,other;assert(find("Shenmue",&first) && !first.group[0]);
    assert(find("Shenmue (2)",&second) && !second.group[0] && second.variant_2048_path[0] && find("Standalone",&other));
    assert(kui_games_library_favorite_toggle("/Games","",first.path,log_line));
    struct kui_games_activity_entry key;
    assert(kui_games_library_activity_key("/Games",first.path,&key,log_line) && !key.group[0] && kui_games_library_record_played(&key,log_line));
    assert(kui_games_library_activity_key("/Games",other.path,&key,log_line) && kui_games_library_record_played(&key,log_line));
    assert(kui_games_activity_key(&key,second.variant_2048_path,"") && kui_games_library_record_played(&key,log_line));
    if(capacity) {
        assert(kui_games_storage_open(log_line)==KUI_GAMES_STORAGE_OK);
        struct kui_games_activity *full=calloc(1,sizeof(*full));assert(full && kui_games_activity_load(full,log_line));
        for(unsigned i=0;i<125;i++) {char unrelated[80];snprintf(unrelated,sizeof(unrelated),"/Other/Favorite %03u.iso",i);assert(kui_games_activity_favorite(full,unrelated,"",true));}
        assert(full->count==KUI_GAMES_ACTIVITY_CAP && kui_games_activity_save(full,log_line));free(full);
        assert(kui_games_storage_done(false));clear();
    }
    unsigned reads=test.group_reads;assert(kui_games_library_scan("/Games",false,log_line,cancel) && test.group_reads==reads+3u);
    assert(find("Shenmue",&first) && first.group[0] && first.favorite);
    struct kui_games_page favorites,recent;
    assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_FAVORITES,&favorites,log_line,cancel) &&
        favorites.total==1 && !strcmp(favorites.entries[0].group,first.group));
    assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_RECENT,&recent,log_line,cancel) &&
        recent.total==2 && !strcmp(recent.entries[0].group,first.group));
    assert(kui_games_library_activity_key("/Games",first.path,&key,log_line) && key.favorite);
    if(explicit_choice) {
        /* First launch snapshots the inherited flag before freeing the
         * catalogue, just as main does during preparation. */
        clear();assert(kui_games_library_record_played(&key,log_line));
        assert(find("Shenmue",&first) && first.favorite);
        assert(kui_games_storage_open(log_line)==KUI_GAMES_STORAGE_OK);
        struct kui_games_activity *history=calloc(1,sizeof(*history));assert(history && kui_games_activity_load(history,log_line));
        assert(kui_games_activity_favorite(history,key.path,key.group,false) && kui_games_activity_save(history,log_line));
        free(history);assert(kui_games_storage_done(false));clear();assert(find("Shenmue",&first) && !first.favorite);
        /* An existing explicit false group flag overrides raw favorites. */
        assert(kui_games_library_favorite_toggle("/Games",first.group,first.path,log_line) && find("Shenmue",&first) && first.favorite);
    }
    clear();assert(find("Shenmue",&first) && first.favorite);
    test.fail_write=true;assert(!kui_games_library_favorite_toggle("/Games",first.group,first.path,log_line));test.fail_write=false;
    assert(find("Shenmue",&first) && first.favorite);clear();assert(find("Shenmue",&first) && first.favorite);
    /* The first toggle of an inherited favorite turns OFF atomically. */
    assert(kui_games_library_favorite_toggle("/Games",first.group,first.path,log_line));clear();
    assert(find("Shenmue",&first) && !first.favorite);
    assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_FAVORITES,&favorites,log_line,cancel) && !favorites.total);
    assert(kui_games_library_rows_filter("/Games","",0,KUI_GAMES_VIEW_LIST,KUI_GAMES_FILTER_RECENT,&recent,log_line,cancel) &&
        recent.total==2 && !strcmp(recent.entries[0].group,first.group));
    clear();assert(find("Shenmue",&first) && !first.favorite);
    if(capacity) {
        assert(kui_games_storage_open(log_line)==KUI_GAMES_STORAGE_OK);
        struct kui_games_activity *full=calloc(1,sizeof(*full));assert(full && kui_games_activity_load(full,log_line) && full->count==128);
        for(unsigned i=0;i<125;i++) {char unrelated[80];snprintf(unrelated,sizeof(unrelated),"/Other/Favorite %03u.iso",i);const struct kui_games_activity_entry *kept=kui_games_activity_find(full,unrelated,"");assert(kept && kept->favorite);}
        const struct kui_games_activity_entry *kept=kui_games_activity_find(full,second.variant_2048_path,"");assert(kept && kept->played==3 && full->clock==3);
        free(full);assert(kui_games_storage_done(false));
    }
    puts("EVIDENCE pre-group raw/2048 favorites and latest played survived v3-to-v4 rescan/cold reload; inherited first toggle turned off, failed save preserved flags, explicit group choice won, and unfavorite never resurrected legacy favorites");
}
static void capture_groups_check(const char *name) {
    unsigned candidates=!strcmp(name,"capture-label-collision")?7u:!strcmp(name,"capture-two-releases")?5u:
        !strcmp(name,"capture-passport") || !strcmp(name,"capture-duplicate-copy")?4u:3u;
    assert(test.group_reads==candidates && test.group_bytes<=2352u*candidates);
    unsigned ip_reads=test.group_reads,ip_bytes=test.group_bytes;
    bool grouped=strcmp(name,"capture-duplicates") && strcmp(name,"capture-duplicate-copy");
    unsigned expected=!strcmp(name,"capture-release-conflict") || !strcmp(name,"capture-invalid")?2u:3u;
    struct kui_games_entry shenmue;assert(find("Shenmue",&shenmue) && (shenmue.group[0]!=0)==grouped);
    if(grouped) {
        struct kui_games_page discs;assert(kui_games_library_rows("/Games",shenmue.group,0,KUI_GAMES_VIEW_LIST,&discs,log_line,cancel));
        assert(discs.count==expected && discs.total==expected);
        for(unsigned i=0;i<expected;i++)assert(discs.entries[i].disc==i+1u);
        assert(strstr(discs.entries[0].path,"/Shenmue/") && strstr(discs.entries[1].path,"/Shenmue (2)/") &&
            (expected==2u || strstr(discs.entries[2].path,"/Shenmue (3)/")) && strstr(discs.entries[1].variant_2048_path,"-2048/"));
        struct kui_games_activity_entry first,third,cooked;
        assert(kui_games_library_activity_key("/Games",discs.entries[0].path,&first,log_line) &&
            kui_games_library_activity_key("/Games",discs.entries[expected-1u].path,&third,log_line) &&
            kui_games_library_activity_key("/Games",discs.entries[1].variant_2048_path,&cooked,log_line));
        assert(!strcmp(first.group,third.group) && !strcmp(first.group,cooked.group));
        struct kui_native_session_request session;assert(kui_games_library_session_request("/Games",discs.entries[1].variant_2048_path,&session,log_line,cancel));
        assert(session.count==expected && session.selected==1 && !strcmp(session.paths[1],discs.entries[1].variant_2048_path));
        if(expected==2u) {struct kui_games_entry separate;assert(find("Shenmue (3)",&separate) && !separate.group[0]);}
        if(!strcmp(name,"capture-passport")) {struct kui_games_entry passport;assert(find("Shenmue (4)",&passport) && !passport.group[0]);}
        if(!strcmp(name,"capture-two-releases")) {
            struct kui_games_entry second;assert(find("Shenmue (4)",&second) && second.group[0] && strcmp(shenmue.group,second.group));
            assert(strstr(shenmue.title,"U V1.003") && strstr(second.title,"E V1.004") && strcmp(shenmue.title,second.title));
            assert(kui_games_library_rows("/Games",second.group,0,KUI_GAMES_VIEW_LIST,&discs,log_line,cancel) && discs.count==2);
        }
        if(!strcmp(name,"capture-release-conflict")) {
            struct kui_games_entry singleton;struct kui_games_library_edit edit={0};strcpy(edit.alias,"Other revision");
            assert(find("Shenmue (3)",&singleton) && singleton.disc==3 &&
                kui_games_library_set_edit("/Games","",singleton.path,&edit,log_line,cancel));
            assert(find("Other revision",&singleton) && singleton.disc==3 && !singleton.group[0]);
        }
        if(!strcmp(name,"capture-label-collision")) {
            char title[128],third[128];for(unsigned i=0;i<36;i++)memcpy(title+i*3u,"海",3);title[108]=0;
            snprintf(third,sizeof(third),"%.108s (3)",title);struct kui_games_entry first,second;
            assert(find(title,&first) && find(third,&second) && first.group[0] && second.group[0] && strcmp(first.group,second.group));
            assert(strcmp(first.title,second.title) && kui_destination_name_valid(first.title) && kui_destination_name_valid(second.title));
            assert(strstr(first.title,"set ") && strstr(second.title,"set ") && strstr(first.title,"KUI-1234"));
        }
    } else {
        struct kui_games_entry second,third;assert(find("Shenmue (2)",&second) && !second.group[0] &&
            find("Shenmue (3)",&third) && !third.group[0]);
    }
    for(unsigned i=0;i<3;i++) {
        static const char *const titles[]={"Skies of Arcadia","Shenmue II","RE CODE VERONICA"};
        struct kui_games_entry preserved;assert(find(titles[i],&preserved) && preserved.group[0]);
    }
    if(!strcmp(name,"capture-read-fail") || !strcmp(name,"capture-cancel")) {
        test.fail_group_ip=!strcmp(name,"capture-read-fail");test.cancel_group_ip=!test.fail_group_ip;
        unsigned writes=test.non_trace_writes;assert(!kui_games_library_scan("/Games",false,log_line,cancel));
        assert(test.non_trace_writes==writes && kui_games_library_ready("/Games"));
        test.fail_group_ip=test.cancel_group_ip=test.cancelled=false;
        assert(find("Shenmue",&shenmue) && shenmue.group[0]);
    }
    clear();unsigned dirs=test.readdirs,opens=test.game_opens,writes=test.writes;
    assert(find("Shenmue",&shenmue) && (shenmue.group[0]!=0)==grouped);
    assert(test.readdirs==dirs && test.game_opens==opens && test.writes==writes);
    unsigned reads=test.block_reads,files=test.opens;
    assert(find("Shenmue",&shenmue));assert(test.block_reads==reads && test.opens==files);
    printf("EVIDENCE collision-named captures %s; %u candidates used %u IP reads (%u bytes); exact release+unique disc proof, Original/2048, session paths and existing explicit sets preserved; cold/warm game I/O=0\n",
        grouped?"grouped":"refused",candidates,ip_reads,ip_bytes);
}
static void check(const char *name) {
    test.active=true;
    if(!strcmp(name,"first-write-fail")) {
        test.fail_write=true;struct kui_games_page result;page(0,&result);assert(result.count && kui_games_library_ready("/Games"));
        unsigned reads=test.block_reads,opens=test.opens;page(0,&result);assert(test.block_reads==reads && test.opens==opens);
        test.fail_write=false;clear();assert(kui_games_library_scan("/Games",false,log_line,cancel));cold_check();
    } else {
        assert(kui_games_library_scan("/Games",false,log_line,cancel));assert(kui_games_library_ready("/Games"));
        if(!strncmp(name,"activity",8))activity_check(name);
        else if(!strcmp(name,"query"))query_check();
        else if(!strncmp(name,"editor",6))editor_check(name);
        else if(!strncmp(name,"identity-",9))identity_check(name);
        else if(!strcmp(name,"catalogue-v2") || !strcmp(name,"catalogue-v3"))catalogue_legacy_check(!strcmp(name,"catalogue-v3")?3u:2u);
        else if(!strncmp(name,"capture-activity",16))capture_activity_check(name);
        else if(!strncmp(name,"capture-",8))capture_groups_check(name);
        else if(!strcmp(name,"cold") || !strcmp(name,"groups"))cold_check();
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
            add_game(false);test.fail_source=true;unsigned writes=test.non_trace_writes;
            assert(!kui_games_library_scan("/Games",false,log_line,cancel));
            assert(test.non_trace_writes==writes && kui_games_library_ready("/Games"));
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
    if(!strcmp(name,"cold") || !strcmp(name,"groups") || !strcmp(name,"capture-groups"))breadcrumb_check("complete",NULL);
    else if(!strcmp(name,"capture-read-fail"))breadcrumb_check("failed","image_path=/Games/Shenmue/disc.gdi\n");
    else if(!strcmp(name,"capture-cancel"))breadcrumb_check("cancelled","image_path=/Games/Shenmue/disc.gdi\n");
    else if(!strcmp(name,"source-read-fail"))breadcrumb_check("failed",NULL);
    else if(!strcmp(name,"cancel") || !strcmp(name,"cancel-readback"))breadcrumb_check("cancelled",NULL);
    clear();assert(!test.connected && !test.files && !test.dirs);
    printf("PASS Games catalogue %s; production writes stayed under KUI\n",name);
}
int main(int argc,char **argv) {
    assert(!setvbuf(stdout,NULL,_IONBF,0));
    assert(argc==5);test.image=fopen(argv[1],"r+b");assert(test.image);
    struct stat info;assert(!fstat(fileno(test.image),&info) && info.st_size>0 && !(info.st_size%512));test.blocks=(uint64_t)info.st_size/512;
    if(!strcmp(argv[3],"seed")) {mounted(true);seed(argv[2]);mounted(false);}
    else {assert(!strcmp(argv[3],"check"));check(argv[4]);}
    assert(!fclose(test.image));return 0;
}
