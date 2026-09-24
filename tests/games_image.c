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
    unsigned writes, connects, disconnects, files, dirs, read_calls;
    bool active, connected, injected, cancelled;
} test;
static FATFS fs;
static const char *const folder = "0:/Games/Reader Test";
static const char *const selected = "/Games/Reader Test/disc.gdi";
static bool fault(const char *name) { return test.active && !strcmp(test.fault, name); }
static void log_line(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
static uint64_t blocks(void *ctx) { (void)ctx; return test.blocks; }
static int read_image(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    (void)ctx;
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
        if(fault("short-read") && *read) { --*read; test.injected = true; }
        if(fault("cancel-read") && file == test.track) { test.cancelled = true; test.injected = true; }
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
    }
    return result;
}
FRESULT __real_f_opendir(DIR *dir, const TCHAR *path);
FRESULT __wrap_f_opendir(DIR *dir, const TCHAR *path) {
    FRESULT result = __real_f_opendir(dir, path);
    if(test.active && result == FR_OK) ++test.dirs;
    return result;
}
FRESULT __real_f_readdir(DIR *dir, FILINFO *info);
FRESULT __wrap_f_readdir(DIR *dir, FILINFO *info) {
    if(fault("dir-read-fail")) { test.injected = true; return FR_DISK_ERR; }
    FRESULT result = __real_f_readdir(dir, info);
    /* Production FatFs disables chmod. Inject these returned attributes while
     * keeping real directory enumeration, paths and file access underneath. */
    if(test.active && result == FR_OK && info) {
        if(!strcmp(info->fname, "Hidden Folder")) info->fattrib |= AM_HID;
        if(!strcmp(info->fname, "System Folder")) info->fattrib |= AM_SYS;
    }
    if(fault("cancel-list")) { test.cancelled = true; test.injected = true; }
    return result;
}
FRESULT __real_f_closedir(DIR *dir);
FRESULT __wrap_f_closedir(DIR *dir) {
    FRESULT result = __real_f_closedir(dir);
    if(test.active) {
        assert(test.dirs); --test.dirs;
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
static void seed(const char *host) {
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
        const char *bad = "3\n1 0 4 2048 track01.bin 0\n2 4 0 2352 track02.raw 0\n3 45000 4 2352 track03.bin 0\n";
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

static void check(void) {
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
    }
    assert(!fclose(test.image));
    printf("PASS Games %s %s; no active-operation writes\n", argv[3], test.fault);
    return 0;
}
