/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/games.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static struct {
    FILE *image;
    uint64_t blocks, read_bytes, track_bytes;
    const char *fault;
    FIL *track;
    DIR *lazy_dir,*root_dir;
    unsigned writes, connects, disconnects, files, dirs, read_calls;
    unsigned dir_calls,stat_calls,block_reads,variant_summaries,variant_failures,callbacks;
    bool active, connected, injected, cancelled;
} test;
static FATFS fs;
static const char *const folder = "0:/Games/Reader Test";
static const char *const selected = "/Games/Reader Test/disc.gdi";
static bool fault(const char *name) {
    return test.active && (!strcmp(test.fault,name) ||
        (!strncmp(test.fault,"variant-",8) && !strcmp(test.fault+8,name)));
}
static void log_line(const char *format, ...) {
    if(!strncmp(format,"Games variants:",15)) ++test.variant_summaries;
    else if(!strncmp(format,"Games variant ",14)) ++test.variant_failures;
    va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
static uint64_t blocks(void *ctx) { (void)ctx; return test.blocks; }
static int read_image(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    (void)ctx;
    if(test.active) ++test.block_reads;
    if(fault("mount-fail")) { test.injected = true; return -1; }
    return fseeko(test.image, (off_t)block * 512, SEEK_SET) ||
        fread(data, 512, count, test.image) != count ? -1 : 0;
}
static int write_image(void *ctx, uint32_t block, size_t count, const uint8_t *data) {
    (void)ctx; ++test.writes; assert(!test.active);
    return fseeko(test.image, (off_t)block * 512, SEEK_SET) ||
        fwrite(data, 512, count, test.image) != count ? -1 : 0;
}
static int sync_image(void *ctx) {
    (void)ctx; return fflush(test.image) || fsync(fileno(test.image)) ? -1 : 0;
}
static const struct kui_media_ops media = {NULL, blocks, read_image, write_image, sync_image};
bool kui_sd_connect(void) {
    assert(!test.connected); ++test.connects;
    if(fault("connect-fail")) { test.injected = true; return false; }
    test.connected = true; kui_media_set(&media); return true;
}
void kui_sd_disconnect(void) {
    assert(test.connected); assert(!test.files && !test.dirs);
    test.connected = false; ++test.disconnects; kui_media_set(NULL);
}
static bool cancel(void) { return test.cancelled || fault("cancel-before"); }

FRESULT __real_f_stat(const TCHAR *path,FILINFO *info);
FRESULT __wrap_f_stat(const TCHAR *path,FILINFO *info) {
    if(test.active) ++test.stat_calls;
    FRESULT result=__real_f_stat(path,info);
    if(result==FR_OK && info && (info->fattrib&AM_DIR) && fault("stat-sfn")) {
        strcpy(info->fname,"READER~1");test.injected=true;
    }
    if(result==FR_OK && info && (info->fattrib&AM_DIR) && fault("stat-case")) {
        for(char *c=info->fname;*c;c++) if(*c>='a' && *c<='z') *c+='A'-'a';
        test.injected=true;
    }
    return result;
}

FRESULT __real_f_open(FIL *file, const TCHAR *path, BYTE flags);
FRESULT __wrap_f_open(FIL *file, const TCHAR *path, BYTE flags) {
    if(test.active) {
        assert(!(flags & (FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_OPEN_APPEND)));
        if(fault("open-fail")) { test.injected = true; return FR_DISK_ERR; }
    }
    FRESULT result = __real_f_open(file, path, flags);
    if(test.active && result == FR_OK) {
        ++test.files;
        if(strstr(path, "track")) test.track = file;
    }
    return result;
}
FRESULT __real_f_read(FIL *file, void *buffer, UINT bytes, UINT *read);
FRESULT __wrap_f_read(FIL *file, void *buffer, UINT bytes, UINT *read) {
    FRESULT result = __real_f_read(file, buffer, bytes, read);
    if(test.active) {
        ++test.read_calls; test.read_bytes += *read;
        if(file == test.track) test.track_bytes += *read;
        if(fault("read-fail")) { test.injected = true; return FR_DISK_ERR; }
        if((fault("pair-read-once") || fault("reverse-pair-read-once") || fault("cache-pair-read-once")) && !test.injected) {test.injected=true;return FR_DISK_ERR;}
        if(fault("short-read") && *read) { --*read; test.injected = true; }
        if(fault("cancel-read") && file == test.track) { test.cancelled = true; test.injected = true; }
        if(fault("cancel-pair")) { test.cancelled = true; test.injected = true; }
    }
    return result;
}
FRESULT __real_f_lseek(FIL *file, FSIZE_t offset);
FRESULT __wrap_f_lseek(FIL *file, FSIZE_t offset) {
    if(fault("seek-fail")) { test.injected = true; return FR_DISK_ERR; }
    return __real_f_lseek(file, offset);
}
FRESULT __real_f_close(FIL *file);
FRESULT __wrap_f_close(FIL *file) {
    FRESULT result = __real_f_close(file);
    if(test.active) {
        assert(test.files); --test.files;
        if(file == test.track) test.track = NULL;
        if(fault("close-fail")) { test.injected = true; return FR_DISK_ERR; }
        if((fault("pair-close-once") || fault("reverse-pair-close-once") || fault("cache-pair-close-once")) && !test.injected) {test.injected=true;return FR_DISK_ERR;}
    }
    return result;
}
FRESULT __real_f_opendir(DIR *dir, const TCHAR *path);
FRESULT __wrap_f_opendir(DIR *dir, const TCHAR *path) {
    FRESULT result = __real_f_opendir(dir, path);
    if(test.active && result == FR_OK) ++test.dirs;
    if(test.active && result==FR_OK && strstr(path,"/Original Only")) test.lazy_dir=dir;
    if(test.active && result==FR_OK && !strcmp(path,"0:/Games")) test.root_dir=dir;
    return result;
}
FRESULT __real_f_readdir(DIR *dir, FILINFO *info);
FRESULT __wrap_f_readdir(DIR *dir, FILINFO *info) {
    if(test.active) ++test.dir_calls;
    if(dir==test.root_dir && info && fault("cache-index-read-once") && !test.injected) {test.injected=true;return FR_DISK_ERR;}
    if(dir==test.lazy_dir && fault("cache-dir-read-once") && !test.injected) {test.injected=true;return FR_DISK_ERR;}
    if(fault("dir-read-fail")) { test.injected = true; return FR_DISK_ERR; }
    FRESULT result = __real_f_readdir(dir, info);
    /* Production FatFs disables chmod. Inject these returned attributes while
     * keeping real directory enumeration, paths and file access underneath. */
    if(test.active && result == FR_OK && info) {
        if(!strcmp(info->fname, "Hidden Folder")) info->fattrib |= AM_HID;
        if(!strcmp(info->fname, "System Folder")) info->fattrib |= AM_SYS;
    }
    if(fault("cancel-list")) { test.cancelled = true; test.injected = true; }
    if(dir==test.lazy_dir && fault("cache-cancel-lazy") && !test.injected) {test.cancelled=true;test.injected=true;}
    return result;
}
FRESULT __real_f_closedir(DIR *dir);
FRESULT __wrap_f_closedir(DIR *dir) {
    FRESULT result = __real_f_closedir(dir);
    if(test.active) {
        assert(test.dirs); --test.dirs;
        if(dir==test.lazy_dir) {
            test.lazy_dir=NULL;
            if(fault("cache-dir-close-once") && !test.injected) {test.injected=true;return FR_DISK_ERR;}
        }
        if(fault("dir-close-fail")) { test.injected = true; return FR_DISK_ERR; }
    }
    return result;
}
FRESULT __real_f_write(FIL *file, const void *data, UINT bytes, UINT *written);
FRESULT __wrap_f_write(FIL *file, const void *data, UINT bytes, UINT *written) {
    assert(!test.active); return __real_f_write(file, data, bytes, written);
}
FRESULT __real_f_mkdir(const TCHAR *path);
FRESULT __wrap_f_mkdir(const TCHAR *path) { assert(!test.active); return __real_f_mkdir(path); }
FRESULT __real_f_unlink(const TCHAR *path);
FRESULT __wrap_f_unlink(const TCHAR *path) { assert(!test.active); return __real_f_unlink(path); }
FRESULT __real_f_rename(const TCHAR *a, const TCHAR *b);
FRESULT __wrap_f_rename(const TCHAR *a, const TCHAR *b) { assert(!test.active); return __real_f_rename(a, b); }

static void write_file(const char *path, const void *data, size_t bytes) {
    FIL file; UINT written;
    assert(bytes <= UINT32_MAX);
    assert(f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    assert(f_write(&file, data, (UINT)bytes, &written) == FR_OK && written == bytes);
    assert(f_close(&file) == FR_OK);
}
static size_t load_file(const char *path, uint8_t *data, size_t capacity) {
    FIL file; UINT got;
    assert(f_open(&file, path, FA_READ) == FR_OK && f_size(&file) <= capacity);
    size_t size = (size_t)f_size(&file);
    assert(f_read(&file, data, (UINT)size, &got) == FR_OK && got == size);
    assert(f_close(&file) == FR_OK); return size;
}
static void seed_fixture(const char *host,const char *target,bool cooked) {
    const char *raw_files[]={"disc.gdi","track01.bin","track02.raw","track03.bin"};
    const char *cooked_files[]={"disc.gdi","track01.iso","track02.raw","track03.iso"};
    const char *const *files=cooked?cooked_files:raw_files;
    assert(f_mkdir(target)==FR_OK);
    for(unsigned i=0;i<4;i++) {
        char path[1024],card[512];uint8_t data[200000];
        assert(snprintf(path,sizeof(path),"%s%s/%s",host,cooked?"/../cooked-gdi":"",files[i])<(int)sizeof(path));
        FILE *file=fopen(path,"rb");assert(file);
        size_t bytes=fread(data,1,sizeof(data),file);
        assert(bytes<sizeof(data) && !ferror(file) && !fclose(file));
        assert(snprintf(card,sizeof(card),"%s/%s",target,files[i])<(int)sizeof(card));
        write_file(card,data,bytes);
    }
}
static void seed_variants(const char *host) {
    assert(f_mkdir("0:/Games")==FR_OK);
    if(!strcmp(test.fault,"variant-list-cache-overflow")) {
        for(unsigned i=0;i<1025;i++) {
            char path[96];snprintf(path,sizeof(path),"0:/Games/Folder %04u",i);assert(f_mkdir(path)==FR_OK);
        }
        return;
    }
    if(!strcmp(test.fault,"variant-unicode-case") || !strcmp(test.fault,"variant-unicode-exact")) {
        seed_fixture(host,"0:/Games/Armáda",false);
        seed_fixture(host,!strcmp(test.fault,"variant-unicode-exact")?"0:/Games/Armáda-2048":"0:/Games/ARMÁDA-2048",true);return;
    }
    if(!strcmp(test.fault,"variant-sfn-alias")) {
        seed_fixture(host,folder,false);FILINFO info;assert(f_stat(folder,&info)==FR_OK);
        char path[96];snprintf(path,sizeof(path),"0:/Games/%s-2048",info.altname[0]?info.altname:"READER~1");
        seed_fixture(host,path,true);return;
    }
    if(!strcmp(test.fault,"variant-pages") || !strncmp(test.fault,"variant-cache-",14)) {
        /* All converted rows precede their originals, including across pages. */
        for(unsigned pass=0;pass<2;pass++) for(unsigned i=0;i<10;i++) {
            char path[96];snprintf(path,sizeof(path),"0:/Games/Pair %02u%s",i,pass?"":"-2048");
            seed_fixture(host,path,!pass);
        }
        seed_fixture(host,"0:/Games/Original Only",false);
        seed_fixture(host,"0:/Games/Cooked Only-2048",true);
        assert(f_mkdir("0:/Games/Empty Folder")==FR_OK);
        write_file("0:/Games/Loose.gdi","1\n",2);return;
    }
    if(!strcmp(test.fault,"variant-nested")) {
        assert(f_mkdir(folder)==FR_OK);
        seed_fixture(host,"0:/Games/Reader Test/extracted",false);
        seed_fixture(host,"0:/Games/Reader Test/extracted-2048",true);return;
    }
    const char *cooked=!strcmp(test.fault,"variant-case")?"0:/Games/reader test-2048":"0:/Games/Reader Test-2048";
    bool reverse=!strncmp(test.fault,"variant-reverse",15);
    if(!strcmp(test.fault,"variant-converted-only")) {seed_fixture(host,cooked,true);return;}
    if(reverse) seed_fixture(host,cooked,true);
    seed_fixture(host,folder,false);
    if(!strcmp(test.fault,"variant-original-only")) return;
    if(!reverse) seed_fixture(host,cooked,true);
    if(!strcmp(test.fault,"variant-chain")) seed_fixture(host,"0:/Games/Reader Test-2048-2048",true);
    if(!strcmp(test.fault,"variant-index-overflow")) {
        for(unsigned i=0;i<512;i++) {
            char path[96];snprintf(path,sizeof(path),"0:/Games/Empty %03u-2048",i);
            assert(f_mkdir(path)==FR_OK);
        }
    }
    const char *descriptor="0:/Games/Reader Test-2048/disc.gdi";
    if(!strcmp(test.fault,"variant-descriptor-name")) {
        assert(f_rename(descriptor,"0:/Games/Reader Test-2048/Converted version.GDI")==FR_OK);
        const char *gdi="3\n1 0 4 2352 \"track01.bin\" 0\n2 4 0 2352 \"track02.raw\" 0\n3 45000 4 2352 \"track03.bin\" 0\n";
        write_file("0:/Games/Reader Test/disc.gdi",gdi,strlen(gdi));
    }
    if(!strcmp(test.fault,"variant-mixed-original") || !strcmp(test.fault,"variant-all-cooked-original")) {
        uint8_t data[200000];size_t bytes=load_file("0:/Games/Reader Test-2048/track01.iso",data,sizeof(data));
        write_file("0:/Games/Reader Test/track01.bin",data,bytes);
        bool all=!strcmp(test.fault,"variant-all-cooked-original");
        if(all) {bytes=load_file("0:/Games/Reader Test-2048/track03.iso",data,sizeof(data));write_file("0:/Games/Reader Test/track03.bin",data,bytes);}
        char gdi[256];snprintf(gdi,sizeof(gdi),"3\n1 0 4 2048 track01.bin 0\n2 4 0 2352 track02.raw 0\n3 45000 4 %u track03.bin 0\n",all?2048u:2352u);
        write_file("0:/Games/Reader Test/disc.gdi",gdi,strlen(gdi));
    }
    if(!strcmp(test.fault,"variant-many-tracks")) {
        uint8_t sector[2352]={0};memset(sector+1,255,10);sector[12]=0x10;sector[13]=0x04;sector[14]=0x50;sector[15]=1;
        write_file("0:/Games/Reader Test/track05.bin",sector,sizeof(sector));
        write_file("0:/Games/Reader Test-2048/track05.iso",sector+16,2048);
        memset(sector,0,sizeof(sector));write_file("0:/Games/Reader Test/track04.raw",sector,sizeof(sector));
        write_file("0:/Games/Reader Test-2048/track04.raw",sector,sizeof(sector));
        const char *a="5\n1 0 4 2352 track01.bin 0\n2 4 0 2352 track02.raw 0\n3 45000 4 2352 track03.bin 0\n4 45100 0 2352 track04.raw 0\n5 45200 4 2352 track05.bin 0\n";
        const char *b="5\n1 0 4 2048 track01.iso 0\n2 4 0 2352 track02.raw 0\n3 45000 4 2048 track03.iso 0\n4 45100 0 2352 track04.raw 0\n5 45200 4 2048 track05.iso 0\n";
        write_file("0:/Games/Reader Test/disc.gdi",a,strlen(a));write_file(descriptor,b,strlen(b));
    }
    if(!strcmp(test.fault,"variant-invalid")) write_file(descriptor,"1\n",2);
    if(!strcmp(test.fault,"variant-mismatch-lba")) {
        const char *gdi="3\n1 0 4 2048 track01.iso 0\n2 4 0 2352 track02.raw 0\n3 45001 4 2048 track03.iso 0\n";
        write_file(descriptor,gdi,strlen(gdi));
    }
    if(!strcmp(test.fault,"variant-mismatch-count")) {
        const char *gdi="2\n1 0 4 2048 track01.iso 0\n2 4 0 2352 track02.raw 0\n";
        write_file(descriptor,gdi,strlen(gdi));
    }
    if(!strcmp(test.fault,"variant-mismatch-length")) {
        uint8_t data[200000];size_t bytes=load_file("0:/Games/Reader Test-2048/track03.iso",data,sizeof(data));
        write_file("0:/Games/Reader Test-2048/track03.iso",data,bytes-2048u);
    }
    if(!strcmp(test.fault,"variant-mismatch-control")) {
        const char *gdi="3\n1 0 4 2048 track01.iso 0\n2 4 4 2048 track02.raw 0\n3 45000 4 2048 track03.iso 0\n";
        uint8_t data[4u*2048u]={0};write_file("0:/Games/Reader Test-2048/track02.raw",data,sizeof(data));
        write_file(descriptor,gdi,strlen(gdi));
    }
    if(!strcmp(test.fault,"variant-missing-track")) assert(f_unlink("0:/Games/Reader Test-2048/track01.iso")==FR_OK);
    if(!strcmp(test.fault,"variant-raw-data")) {
        const char *gdi="3\n1 0 4 2352 track01.iso 0\n2 4 0 2352 track02.raw 0\n3 45000 4 2048 track03.iso 0\n";
        uint8_t data[200000];size_t bytes=load_file("0:/Games/Reader Test/track01.bin",data,sizeof(data));
        write_file("0:/Games/Reader Test-2048/track01.iso",data,bytes);write_file(descriptor,gdi,strlen(gdi));
    }
    if(!strcmp(test.fault,"variant-ambiguous-original")) write_file("0:/Games/Reader Test/other.gdi","1\n",2);
    if(!strcmp(test.fault,"variant-ambiguous-converted")) write_file("0:/Games/Reader Test-2048/other.gdi","1\n",2);
    if(!strcmp(test.fault,"variant-layout-only")) {
        uint8_t data[200000];size_t bytes=load_file("0:/Games/Reader Test-2048/track03.iso",data,sizeof(data));
        data[0]^=1;write_file("0:/Games/Reader Test-2048/track03.iso",data,bytes);
        write_file("0:/Games/Reader Test-2048/conversion.json","not trusted metadata",20);
    }
}
static void seed(const char *host) {
    if(!strncmp(test.fault,"variant-",8)) {seed_variants(host);return;}
    if(!strcmp(test.fault, "missing-root")) return;
    assert(f_mkdir("0:/Games") == FR_OK && f_mkdir(folder) == FR_OK);
    const char *files[] = {"disc.gdi", "track01.bin", "track02.raw", "track03.bin"};
    for(unsigned i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        char path[1024], target[512]; uint8_t data[200000];
        assert(snprintf(path, sizeof(path), "%s/%s", host, files[i]) < (int)sizeof(path));
        FILE *file = fopen(path, "rb"); assert(file);
        size_t bytes = fread(data, 1, sizeof(data), file);
        assert(bytes < sizeof(data) && !ferror(file) && !fclose(file));
        assert(snprintf(target, sizeof(target), "%s/%s", folder, files[i]) < (int)sizeof(target));
        write_file(target, data, bytes);
    }
    if(!strcmp(test.fault, "missing-track")) assert(f_unlink("0:/Games/Reader Test/track02.raw") == FR_OK);
    if(!strcmp(test.fault, "truncated-audio") || !strcmp(test.fault, "truncated-boot")) {
        const char *path = !strcmp(test.fault, "truncated-audio") ?
            "0:/Games/Reader Test/track02.raw" : "0:/Games/Reader Test/track03.bin";
        uint8_t data[200000]; size_t bytes = load_file(path, data, sizeof(data));
        write_file(path, data, !strcmp(test.fault, "truncated-audio") ? bytes - 1 : 22u * 2352u);
    }
    if(!strcmp(test.fault, "unsafe-gdi")) {
        const char *bad = "3\n1 0 4 2352 track01.bin 0\n2 4 0 2352 ../track02.raw 0\n3 45000 4 2352 track03.bin 0\n";
        write_file("0:/Games/Reader Test/disc.gdi", bad, strlen(bad));
    }
    if(!strcmp(test.fault, "bad-ip")) {
        uint8_t data[200000]; size_t bytes = load_file("0:/Games/Reader Test/track03.bin", data, sizeof(data));
        data[16] ^= 1; write_file("0:/Games/Reader Test/track03.bin", data, bytes);
    }
    if(!strcmp(test.fault, "valid-session-size")) {
        uint8_t data[200000]; size_t bytes = load_file("0:/Games/Reader Test/track03.bin", data, sizeof(data));
        /* Common multisession writers store session-relative volume size while
         * keeping directory and boot extents absolute. Do not relocate LBAs. */
        const uint8_t both_endian[8] = {64, 0, 0, 0, 0, 0, 0, 64};
        memcpy(data + 16u * 2352u + 16u + 80u, both_endian, sizeof(both_endian));
        write_file("0:/Games/Reader Test/track03.bin", data, bytes);
    }
    if(!strcmp(test.fault, "unsupported-format")) {
        const char *bad = "3\n1 0 4 2336 track01.bin 0\n2 4 0 2352 track02.raw 0\n3 45000 4 2352 track03.bin 0\n";
        write_file("0:/Games/Reader Test/disc.gdi", bad, strlen(bad));
    }
    if(!strcmp(test.fault, "listing") || !strcmp(test.fault, "cancel-list") ||
       !strcmp(test.fault, "dir-read-fail") || !strcmp(test.fault, "dir-close-fail")) {
        assert(f_mkdir("0:/Games/Reader Test (2)") == FR_OK);
        assert(f_mkdir("0:/Games/Empty Folder") == FR_OK);
        assert(f_mkdir("0:/Games/Ambiguous") == FR_OK);
        write_file("0:/Games/Reader Test (2)/Reader Test.GDI", "1\n", 2);
        write_file("0:/Games/Ambiguous/a.gdi", "1\n", 2);
        write_file("0:/Games/Ambiguous/b.gdi", "1\n", 2);
        write_file("0:/Games/Standalone.gdi", "1\n", 2);
        write_file("0:/Games/ignore.txt", "ignored", 7);
        assert(f_mkdir("0:/Games/.hidden") == FR_OK);
        assert(f_mkdir("0:/Games/Hidden Folder") == FR_OK);
        assert(f_mkdir("0:/Games/System Folder") == FR_OK);
        for(unsigned i = 0; i < 12; ++i) {
            char path[64]; snprintf(path, sizeof(path), "0:/Games/Folder %02u", i);
            assert(f_mkdir(path) == FR_OK);
        }
        char long_name[140]; strcpy(long_name, "0:/Games/"); memset(long_name + 9, 'x', 128); long_name[137] = 0;
        assert(f_mkdir(long_name) == FR_OK);
    }
    if(!strcmp(test.fault, "long-root")) {
        char path[160]; strcpy(path, "0:/"); memset(path + 3, 'p', 120); path[123] = 0;
        assert(f_mkdir(path) == FR_OK); strcat(path, "/ExtendsTooFar"); assert(f_mkdir(path) == FR_OK);
    }
}

static void check_listing(void) {
    struct kui_games_entry all[24]; unsigned count = 0;
    bool saw_game = false, saw_duplicate = false, saw_empty = false, saw_ambiguous = false, saw_gdi = false;
    unsigned disabled = 0, ordinary = 0;
    for(unsigned offset = 0; ; ) {
        struct kui_games_page page;
        assert(kui_games_list("/Games", offset, &page, log_line, cancel));
        assert(page.count <= KUI_GAMES_ROWS && count + page.count <= 24);
        for(unsigned i = 0; i < page.count; ++i) {
            const struct kui_games_entry *entry = &page.entries[i];
            for(unsigned j = 0; j < count; ++j) assert(strcmp(all[j].name, entry->name));
            all[count++] = *entry;
            if(entry->disabled) { ++disabled; continue; }
            assert(entry->name[0] != '.' && !strstr(entry->name, "Hidden") && !strstr(entry->name, "System"));
            if(!strcmp(entry->name, "Reader Test")) {
                assert(!entry->directory && !strcmp(entry->path, selected)); saw_game = true;
            } else if(!strcmp(entry->name, "Reader Test (2)")) {
                assert(!entry->directory && !strcmp(entry->path, "/Games/Reader Test (2)/Reader Test.GDI")); saw_duplicate = true;
            } else if(!strcmp(entry->name, "Empty Folder")) { assert(entry->directory); saw_empty = true; }
            else if(!strcmp(entry->name, "Ambiguous")) { assert(entry->directory); saw_ambiguous = true; }
            else if(!strcmp(entry->name, "Standalone.gdi")) { assert(!entry->directory); saw_gdi = true; }
            else { assert(!strncmp(entry->name, "Folder ", 7) && entry->directory); ++ordinary; }
        }
        offset += page.count;
        if(!page.has_more) break;
        assert(page.count == KUI_GAMES_ROWS);
    }
    assert(count == 18 && disabled == 1 && ordinary == 12);
    assert(saw_game && saw_duplicate && saw_empty && saw_ambiguous && saw_gdi);
    struct kui_games_page page;
    assert(kui_games_list("/Games", count, &page, log_line, cancel) && !page.count && !page.has_more);
    assert(kui_games_list("/Games", 3, &page, log_line, cancel) && page.count == KUI_GAMES_ROWS);
    for(unsigned i = 0; i < page.count; ++i) {
        assert(!strcmp(page.entries[i].name, all[3 + i].name));
        assert(page.entries[i].disabled == all[3 + i].disabled);
    }
    assert(kui_games_list("/Games/Empty Folder", 0, &page, log_line, cancel) && !page.count);
    assert(!test.track_bytes && !test.read_bytes);
}

static void cached_mounted(void *ctx,kui_log_fn log,kui_cancel_fn stop) {
    (void)ctx;(void)log;(void)stop;
    assert(test.connected && !test.files && !test.dirs);++test.callbacks;
    if(fault("cache-callback-cancel")) {test.cancelled=true;test.injected=true;}
}
static void check_cache(void) {
    struct kui_games_page page;
    if(fault("cache-pair-read-once") || fault("cache-pair-close-once") || fault("cache-index-read-once")) {
        unsigned initial_total=fault("cache-index-read-once")?24u:15u;
        assert(kui_games_list("/Games",0,&page,log_line,cancel) && page.total==initial_total && test.injected);
        unsigned dirs=test.dir_calls,reads=test.read_calls;
        assert(kui_games_list("/Games",0,&page,log_line,cancel) && page.total==14u);
        assert(test.dir_calls>dirs && test.read_calls>reads && test.variant_summaries==1);
        for(unsigned i=0;i<page.count;i++) assert(page.entries[i].variant_2048_path[0]);
        dirs=test.dir_calls;reads=test.read_calls;
        assert(kui_games_list("/Games",0,&page,log_line,cancel) && page.total==14u);
        assert(test.dir_calls==dirs && test.read_calls==reads);return;
    }
    assert(kui_games_list("/Games",0,&page,log_line,cancel) && page.count==8 && page.total==14);
    assert(test.variant_summaries==1 && !test.track_bytes);
    unsigned first_reads=test.read_calls;
    if(!strcmp(test.fault,"variant-cache-refresh")) {
        kui_games_cache_clear();
        assert(kui_games_list("/Games",0,&page,log_line,cancel));
        assert(test.read_calls>first_reads && test.variant_summaries==2);return;
    }
    if(!strcmp(test.fault,"variant-cache-root-change")) {
        assert(kui_games_list("/Games/Empty Folder",0,&page,log_line,cancel) && !page.count);
        assert(kui_games_list("/Games",0,&page,log_line,cancel));
        assert(test.read_calls>first_reads && test.variant_summaries==3);return;
    }
    if(!strcmp(test.fault,"variant-cache-callback") || !strcmp(test.fault,"variant-cache-callback-cancel")) {
        unsigned dirs=test.dir_calls,stats=test.stat_calls,reads=test.read_calls,connects=test.connects;
        bool cancelled=fault("cache-callback-cancel");
        assert(kui_games_list_with("/Games",0,&page,cached_mounted,NULL,log_line,cancel)==!cancelled);
        if(cancelled) {assert(!page.count && !page.has_more && test.injected);test.cancelled=false;}
        assert(test.callbacks==1 && test.connects==connects+1);
        assert(test.dir_calls==dirs && test.stat_calls==stats && test.read_calls==reads);
        assert(kui_games_list("/Games",0,&page,log_line,cancel) && page.count==8 && page.total==14);
        assert(test.connects==connects+1 && test.variant_summaries==1);return;
    }
    bool lazy_fault=!strcmp(test.fault,"variant-cache-dir-read-once") ||
        !strcmp(test.fault,"variant-cache-dir-close-once") || !strcmp(test.fault,"variant-cache-cancel-lazy");
    if(lazy_fault) {
        assert(!test.injected);
        bool cancelled=!strcmp(test.fault,"variant-cache-cancel-lazy");
        bool result=kui_games_list("/Games",8,&page,log_line,cancel);
        assert(result==!cancelled && test.injected);
        if(cancelled) {assert(!page.count && !page.has_more);test.cancelled=false;}
        else {
            bool folder=false;
            for(unsigned i=0;i<page.count;i++) if(!strcmp(page.entries[i].name,"Original Only")) folder=page.entries[i].directory;
            assert(folder);
        }
        unsigned dirs=test.dir_calls;
        assert(kui_games_list("/Games",8,&page,log_line,cancel));
        assert(test.dir_calls>dirs);
        for(unsigned i=0;i<page.count;i++) if(!strcmp(page.entries[i].name,"Original Only"))
            assert(!page.entries[i].directory && !strcmp(page.entries[i].path,"/Games/Original Only/disc.gdi"));
    } else assert(kui_games_list("/Games",8,&page,log_line,cancel));
    /* Both pages have now been resolved. Paging and highlighting need zero
     * media, descriptor, directory or stat work on the same snapshot. */
    unsigned connects=test.connects,reads=test.read_calls,dirs=test.dir_calls,stats=test.stat_calls,blocks=test.block_reads;
    for(unsigned i=0;i<12;i++) {
        unsigned offset=i%3==0?0u:i%3==1?3u:8u;
        assert(kui_games_list("/Games",offset,&page,log_line,cancel) && page.total==14);
    }
    assert(test.connects==connects && test.read_calls==reads && test.dir_calls==dirs &&
        test.stat_calls==stats && test.block_reads==blocks && test.variant_summaries==1);
    printf("PASS cached navigation: 12 page requests, zero media/directory/stat/descriptor reads\n");
}
static void check_variants(void) {
    struct kui_games_page page;
    if(!strncmp(test.fault,"variant-cache-",14)) {check_cache();return;}
    if(!strcmp(test.fault,"variant-list-cache-overflow")) {
        assert(kui_games_list("/Games",0,&page,log_line,cancel) && page.total==1025 && page.count==8);
        unsigned dirs=test.dir_calls,connects=test.connects;
        assert(kui_games_list("/Games",1024,&page,log_line,cancel) && page.total==1025 && page.count==1 && !page.has_more);
        assert(!strcmp(page.entries[0].name,"Folder 1024") && page.entries[0].directory);
        assert(test.dir_calls>dirs && test.connects==connects+1 && test.variant_summaries==1 && !test.read_bytes);return;
    }
    if(!strcmp(test.fault,"variant-unicode-case") || !strcmp(test.fault,"variant-unicode-exact") || !strcmp(test.fault,"variant-sfn-alias")) {
        bool paired=!strcmp(test.fault,"variant-unicode-exact");
        assert(kui_games_list("/Games",0,&page,log_line,cancel) && page.count==(paired?1u:2u) && page.total==page.count);
        bool original=false;
        for(unsigned i=0;i<page.count;i++) {
            const struct kui_games_entry *e=&page.entries[i];
            assert(!e->directory && !e->disabled);
            if(!strcmp(e->name,"Armáda") || !strcmp(e->name,"Reader Test")) original=true;
            assert((e->variant_2048_path[0]!=0)==paired);
            if(paired) {
                assert(!strcmp(e->path,"/Games/Armáda/disc.gdi"));
                assert(!strcmp(e->variant_2048_path,"/Games/Armáda-2048/disc.gdi"));
            }
        }
        assert(original && !test.track_bytes);return;
    }
    if(!strcmp(test.fault,"variant-nested")) {
        assert(kui_games_list("/Games",0,&page,log_line,cancel));
        assert(page.count==1 && page.entries[0].directory && !page.entries[0].variant_2048_path[0]);
        assert(kui_games_list("/Games/Reader Test",0,&page,log_line,cancel));
        assert(page.count==1 && !page.entries[0].directory && !strcmp(page.entries[0].name,"extracted"));
        assert(!strcmp(page.entries[0].path,"/Games/Reader Test/extracted/disc.gdi"));
        assert(!strcmp(page.entries[0].variant_2048_path,"/Games/Reader Test/extracted-2048/disc.gdi"));
        assert(!test.track_bytes);return;
    }
    if(!strcmp(test.fault,"variant-cancel-pair")) {
        assert(!kui_games_list("/Games",0,&page,log_line,cancel));
        assert(test.cancelled && test.injected && !page.count && !page.has_more);
        assert(strstr(page.message,"stopped"));return;
    }
    if(!strcmp(test.fault,"variant-index-overflow")) {
        assert(kui_games_list("/Games",0,&page,log_line,cancel));
        assert(page.count==KUI_GAMES_ROWS && page.total==514 && page.has_more);
        assert(!strcmp(page.entries[0].name,"Reader Test") && !page.entries[0].variant_2048_path[0]);
        assert(!strcmp(page.entries[1].name,"Reader Test-2048") && !page.entries[1].variant_2048_path[0]);
        assert(kui_games_list("/Games",513,&page,log_line,cancel));
        assert(page.count==1 && page.total==514 && !page.has_more);
        assert(!test.read_bytes && !test.track_bytes);return;
    }
    if(!strcmp(test.fault,"variant-pages")) {
        struct kui_games_entry all[16];unsigned count=0,pairs=0;
        for(unsigned offset=0;;) {
            assert(kui_games_list("/Games",offset,&page,log_line,cancel));
            assert(page.total==14 && page.count<=KUI_GAMES_ROWS && count+page.count<=16);
            for(unsigned i=0;i<page.count;i++) {
                const struct kui_games_entry *e=&page.entries[i];
                for(unsigned j=0;j<count;j++) assert(strcmp(e->name,all[j].name));
                all[count++]=*e;
                if(!strncmp(e->name,"Pair ",5)) {
                    char path[128];snprintf(path,sizeof(path),"/Games/%s/disc.gdi",e->name);
                    assert(!strcmp(e->path,path));snprintf(path,sizeof(path),"/Games/%s-2048/disc.gdi",e->name);
                    assert(!strcmp(e->variant_2048_path,path) && !e->directory);++pairs;
                } else assert(!e->variant_2048_path[0]);
            }
            offset+=page.count;if(!page.has_more) break;
            assert(page.count==KUI_GAMES_ROWS);
        }
        assert(count==14 && pairs==10);
        for(unsigned offset=1;offset<14;offset+=3) {
            assert(kui_games_list("/Games",offset,&page,log_line,cancel));
            unsigned expected=14-offset<KUI_GAMES_ROWS?14-offset:KUI_GAMES_ROWS;
            assert(page.count==expected && page.total==14 && page.has_more==(14-offset>KUI_GAMES_ROWS));
            for(unsigned i=0;i<page.count;i++) {
                assert(!strcmp(page.entries[i].path,all[offset+i].path));
                assert(!strcmp(page.entries[i].variant_2048_path,all[offset+i].variant_2048_path));
            }
        }
        assert(kui_games_list("/Games",14,&page,log_line,cancel) && !page.count && !page.has_more && page.total==14);
        assert(!test.track_bytes && test.read_bytes<65536);return;
    }
    assert(kui_games_list("/Games",0,&page,log_line,cancel));
    bool chain=!strcmp(test.fault,"variant-chain");
    bool paired=chain || !strcmp(test.fault,"variant-pair") || !strcmp(test.fault,"variant-reverse") ||
        !strcmp(test.fault,"variant-layout-only") || !strcmp(test.fault,"variant-stat-sfn") ||
        !strcmp(test.fault,"variant-stat-case") || !strcmp(test.fault,"variant-case") ||
        !strcmp(test.fault,"variant-descriptor-name") || !strcmp(test.fault,"variant-mixed-original") ||
        !strcmp(test.fault,"variant-all-cooked-original") || !strcmp(test.fault,"variant-many-tracks");
    bool original_only=!strcmp(test.fault,"variant-original-only");
    bool converted_only=!strcmp(test.fault,"variant-converted-only");
    assert(page.count==(paired || original_only || converted_only?1u:2u)+(chain?1u:0u));
    assert(page.total==page.count && !page.has_more);
    bool original=false,converted=false,double_copy=false;
    for(unsigned i=0;i<page.count;i++) {
        const struct kui_games_entry *e=&page.entries[i];
        if(!strcmp(e->name,"Reader Test")) {
            original=true;
            if(!strcmp(test.fault,"variant-ambiguous-original")) assert(e->directory && !strcmp(e->path,"/Games/Reader Test"));
            else assert(!e->directory && !strcmp(e->path,selected));
            const char *expected=!strcmp(test.fault,"variant-case")?"/Games/reader test-2048/disc.gdi":
                !strcmp(test.fault,"variant-descriptor-name")?"/Games/Reader Test-2048/Converted version.GDI":
                "/Games/Reader Test-2048/disc.gdi";
            if(paired) assert(!strcmp(e->variant_2048_path,expected));
            else assert(!e->variant_2048_path[0]);
        } else if(chain && !strcmp(e->name,"Reader Test-2048-2048")) {
            double_copy=true;assert(!e->directory && !e->variant_2048_path[0]);
            assert(!strcmp(e->path,"/Games/Reader Test-2048-2048/disc.gdi"));
        } else {
            assert(!strcmp(e->name,"Reader Test-2048"));converted=true;
            assert(!e->variant_2048_path[0]);
            if(!strcmp(test.fault,"variant-ambiguous-converted")) assert(e->directory && !strcmp(e->path,"/Games/Reader Test-2048"));
            else assert(!e->directory && !strcmp(e->path,"/Games/Reader Test-2048/disc.gdi"));
        }
    }
    assert(original==!converted_only && converted==(!paired && !original_only));
    assert(double_copy==chain);
    assert(!test.track_bytes && test.read_bytes<4096);
    if(original_only || converted_only) assert(!test.read_bytes);
}

static void check(void) {
    if(!strncmp(test.fault,"variant-",8)) {check_variants();return;}
    if(!strcmp(test.fault, "listing")) { check_listing(); return; }
    if(!strcmp(test.fault, "missing-root") || !strcmp(test.fault, "cancel-list") ||
       !strcmp(test.fault, "dir-read-fail") || !strcmp(test.fault, "dir-close-fail") ||
       !strcmp(test.fault, "invalid-root") || !strcmp(test.fault, "offset-limit")) {
        struct kui_games_page page;
        const char *root = !strcmp(test.fault, "invalid-root") ? "/Games/../escape" : "/Games";
        unsigned offset = !strcmp(test.fault, "offset-limit") ? 8193 : 0;
        assert(!kui_games_list(root, offset, &page, log_line, cancel));
        assert(page.message[0]); return;
    }
    if(!strcmp(test.fault, "long-root")) {
        char root[128]; root[0] = '/'; memset(root + 1, 'p', 120); root[121] = 0;
        struct kui_games_page page; assert(kui_games_list(root, 0, &page, log_line, cancel));
        assert(page.count == 1 && page.entries[0].disabled); return;
    }
    struct kui_games_detail detail; memset(&detail, 0xa5, sizeof(detail));
    const char *path = !strcmp(test.fault, "invalid-path") ? "/Games/../disc.gdi" : selected;
    bool result = kui_games_inspect(path, &detail, log_line, cancel);
    bool valid = !strcmp(test.fault, "valid") || !strcmp(test.fault, "valid-session-size");
    assert(result == valid && detail.valid == valid);
    assert(detail.message[0]);
    if(!strcmp(test.fault, "unsupported-format"))
        assert(!strcmp(detail.message,
            "GDI needs 2048/2352-byte data, 2352-byte audio and zero offsets"));
    if(valid) {
        assert(!strcmp(detail.path, selected));
        assert(!strcmp(detail.title, "Independent Games Fixture"));
        assert(!strcmp(detail.product, "KUI-TEST"));
        assert(!strcmp(detail.region, "JUE"));
        assert(!strcmp(detail.boot_file, "1ST_READ.BIN"));
        assert(detail.tracks == 3 && detail.audio_tracks == 1 && detail.data_tracks == 2);
        assert(detail.bytes == 72u * 2352u && detail.boot_bytes == 4096 && detail.boot_lba == 45021);
        assert(test.track_bytes > 0 && test.track_bytes < 65536);
    }
    if(!strcmp(test.fault, "cancel-before") || !strcmp(test.fault, "cancel-read")) assert(detail.stopped);
    if(!strcmp(test.fault, "cancel-before")) assert(!test.read_calls && !test.connects);
}

int main(int argc, char **argv) {
    if(argc != 5) return 2;
    struct stat st; if(lstat(argv[1], &st) || !S_ISREG(st.st_mode) || st.st_size % 512) return 2;
    test.image = fopen(argv[1], "r+b"); assert(test.image);
    test.blocks = (uint64_t)st.st_size / 512; test.fault = argv[4];
    if(!strcmp(argv[3], "seed")) {
        kui_media_set(&media); assert(kui_mount(&fs, log_line)); seed(argv[2]);
        assert(f_mount(NULL, "0:", 0) == FR_OK); kui_media_set(NULL);
    } else {
        assert(!strcmp(argv[3], "check")); test.active = true; check();
        assert(!test.writes && !test.files && !test.dirs && !test.connected);
        assert(test.connects == test.disconnects ||
            (!strcmp(test.fault, "connect-fail") && test.connects == test.disconnects + 1));
        if(!strcmp(test.fault, "connect-fail") || !strcmp(test.fault, "mount-fail") ||
           !strcmp(test.fault, "open-fail") || !strcmp(test.fault, "read-fail") ||
           !strcmp(test.fault, "short-read") || !strcmp(test.fault, "seek-fail") ||
           !strcmp(test.fault, "close-fail") || !strcmp(test.fault, "dir-read-fail") ||
           !strcmp(test.fault, "dir-close-fail") || !strcmp(test.fault, "cancel-list") ||
           !strcmp(test.fault, "cancel-read")) assert(test.injected);
        if(!strncmp(test.fault,"variant-",8) && (fault("open-fail") || fault("read-fail") ||
            fault("short-read") || fault("seek-fail") || fault("close-fail") || fault("cancel-pair") ||
            fault("pair-read-once") || fault("pair-close-once") ||
            fault("reverse-pair-read-once") || fault("reverse-pair-close-once") ||
            fault("stat-sfn") || fault("stat-case") || fault("cache-dir-read-once") ||
            fault("cache-dir-close-once") || fault("cache-cancel-lazy"))) assert(test.injected);
    }
    kui_games_cache_clear();
    assert(!fclose(test.image));
    printf("PASS Games %s %s; no active-operation writes\n", argv[3], test.fault);
    return 0;
}
