/* SPDX-License-Identifier: GPL-3.0-only */
#define _DEFAULT_SOURCE
#include "kui/games_covers.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Real FatFs on a card image built by tests/test_games_covers_images.py. */
static struct {
    FILE *image;
    uint64_t blocks;
    const char *fault;
    unsigned writes, connects, disconnects, files, dirs, progress, cancel_after;
    bool active, connected, cancelled;
} test;
static FATFS fs;
static uint16_t pixels[KUI_GAMES_ROWS][KUI_COVER_PIXELS];

static void log_line(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
static uint64_t blocks(void *ctx) { (void)ctx; return test.blocks; }
static int read_image(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    (void)ctx;
    return fseeko(test.image, (off_t)block * 512, SEEK_SET) || fread(data, 512, count, test.image) != count ? -1 : 0;
}
static int write_image(void *ctx, uint32_t block, size_t count, const uint8_t *data) {
    (void)ctx; ++test.writes;
    return fseeko(test.image, (off_t)block * 512, SEEK_SET) || fwrite(data, 512, count, test.image) != count ? -1 : 0;
}
static int sync_image(void *ctx) { (void)ctx; return fflush(test.image) || fsync(fileno(test.image)) ? -1 : 0; }
static const struct kui_media_ops media = {NULL, blocks, read_image, write_image, sync_image};
bool kui_sd_connect(void) {
    assert(!test.connected); ++test.connects;
    test.connected = true; kui_media_set(&media); return true;
}
void kui_sd_disconnect(void) {
    assert(test.connected); assert(!test.files && !test.dirs);
    test.connected = false; ++test.disconnects; kui_media_set(NULL);
}
static bool cancel(void) { return test.cancelled; }
static void progress(const struct kui_app_status *status) {
    assert(status->message[0]);
    if(++test.progress == test.cancel_after) test.cancelled = true;
}

FRESULT __real_f_open(FIL *file, const TCHAR *path, BYTE flags);
FRESULT __wrap_f_open(FIL *file, const TCHAR *path, BYTE flags) {
    FRESULT result = __real_f_open(file, path, flags);
    if(test.active && result == FR_OK) ++test.files;
    return result;
}
FRESULT __real_f_close(FIL *file);
FRESULT __wrap_f_close(FIL *file) {
    FRESULT result = __real_f_close(file);
    if(test.active) { assert(test.files); --test.files; }
    return result;
}
FRESULT __real_f_opendir(DIR *dir, const TCHAR *path);
FRESULT __wrap_f_opendir(DIR *dir, const TCHAR *path) {
    FRESULT result = __real_f_opendir(dir, path);
    if(test.active && result == FR_OK) ++test.dirs;
    return result;
}
FRESULT __real_f_closedir(DIR *dir);
FRESULT __wrap_f_closedir(DIR *dir) {
    FRESULT result = __real_f_closedir(dir);
    if(test.active) { assert(test.dirs); --test.dirs; }
    return result;
}
FRESULT __real_f_write(FIL *file, const void *data, UINT bytes, UINT *written);
FRESULT __wrap_f_write(FIL *file, const void *data, UINT bytes, UINT *written) {
    if(test.active && !strcmp(test.fault, "write-fail")) { *written = 0; return FR_DISK_ERR; }
    return __real_f_write(file, data, bytes, written);
}

static void write_file(const char *path, const void *data, size_t bytes) {
    FIL file; UINT written;
    assert(f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    assert(f_write(&file, data, (UINT)bytes, &written) == FR_OK && written == bytes);
    assert(f_close(&file) == FR_OK);
}
static uint8_t *host_file(const char *path, size_t *bytes) {
    FILE *file = fopen(path, "rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long size = ftell(file); assert(size >= 0);
    rewind(file);
    uint8_t *data = malloc((size_t)size + 1u); assert(data);
    assert(fread(data, 1, (size_t)size, file) == (size_t)size && !fclose(file));
    *bytes = (size_t)size;
    return data;
}
/* The driver lists folders and files in name order ("D path" / "F path"),
 * so FAT directory order, and so the scan order, is fixed. */
static void seed(const char *host) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/manifest.txt", host);
    FILE *list = fopen(path, "r");
    assert(list);
    char line[600];
    while(fgets(line, sizeof(line), list)) {
        line[strcspn(line, "\n")] = 0;
        assert((line[0] == 'D' || line[0] == 'F') && line[1] == ' ');
        const char *relative = line + 2;
        if(!strcmp(test.fault, "missing-root") && !strncmp(relative, "Games", 5)) continue;
        char card[512];
        assert(snprintf(card, sizeof(card), "0:/%s", relative) < (int)sizeof(card));
        if(line[0] == 'D') { assert(f_mkdir(card) == FR_OK); continue; }
        size_t bytes;
        assert(snprintf(path, sizeof(path), "%s/%s", host, relative) < (int)sizeof(path));
        uint8_t *data = host_file(path, &bytes);
        write_file(card, data, bytes);
        free(data);
    }
    assert(!fclose(list));
}

static uint16_t rgb565(unsigned r, unsigned g, unsigned b) {
    return (uint16_t)((r * 31u + 127u) / 255u << 11 | (g * 63u + 127u) / 255u << 5 | (b * 31u + 127u) / 255u);
}
static const uint16_t navy = KUI_COVER_BACKGROUND, red = 0xF800, blue = 0x001F, green = 0x07E0,
    magenta = 0xF81F, yellow = 0xFFE0;
static uint16_t at(const uint16_t *px, unsigned edge, unsigned x, unsigned y) { return px[y * edge + x]; }
/* The split texture's top is red and its bottom blue, whatever the size. */
static void check_split(const uint16_t *px, unsigned edge) {
    assert(at(px, edge, edge - 10, 5) == red && at(px, edge, 5, edge - 10) == blue);
}
static void mounted(bool on) {
    if(on) { kui_media_set(&media); assert(kui_mount(&fs, log_line)); }
    else { assert(f_mount(NULL, "0:", 0) == FR_OK); kui_media_set(NULL); }
}
/* Every record is whole; no half-written replacement is left behind. */
static unsigned records(void) {
    mounted(true);
    DIR dir; FILINFO info; unsigned count = 0;
    if(f_opendir(&dir, KUI_GAMES_COVERS_FOLDER) != FR_OK) { mounted(false); return 0; }
    while(f_readdir(&dir, &info) == FR_OK && info.fname[0]) {
        size_t n = strlen(info.fname);
        assert(n < 4 || strcmp(info.fname + n - 4, ".new"));
        if(n > 4 && !strcmp(info.fname + n - 4, ".kcv")) {
            char path[800]; snprintf(path, sizeof(path), "%s/%s", KUI_GAMES_COVERS_FOLDER, info.fname);
            FIL file; UINT got; uint8_t header[KUI_COVER_HEADER_BYTES]; struct kui_cover_record record;
            assert(f_open(&file, path, FA_READ) == FR_OK);
            assert(f_read(&file, header, sizeof(header), &got) == FR_OK && got == sizeof(header));
            assert(kui_cover_header_decode(&record, header));
            assert(f_size(&file) == (record.source == KUI_COVER_SOURCE_NONE ? KUI_COVER_HEADER_BYTES : KUI_COVER_FILE_BYTES));
            assert(f_close(&file) == FR_OK);
            ++count;
        }
    }
    assert(f_closedir(&dir) == FR_OK);
    mounted(false);
    return count;
}
static const struct kui_games_entry *entry(const struct kui_games_page *page, const char *name, unsigned *row) {
    for(unsigned i = 0; i < page->count; ++i)
        if(!strcmp(page->entries[i].name, name)) { *row = i; return &page->entries[i]; }
    assert(!"entry missing");
    return NULL;
}
static void list(unsigned view, struct kui_games_page *page) {
    memset(pixels, 0, sizeof(pixels));
    assert(kui_games_list_covers("/Games", 0, view, page, pixels, log_line, cancel));
    assert(page->count == 7 && page->total == 7 && !page->has_more && page->artwork);
}
static void scan(struct kui_games_scan_counts *counts, bool expect_ok) {
    struct kui_app_status status;
    assert(kui_games_scan(&status, counts, progress, log_line, cancel) == expect_ok);
    assert(status.complete && status.message[0] && status.line_count >= 5);
}
static void check_scan(const char *host) {
    struct kui_games_scan_counts c;
    scan(&c, true);
    assert(c.games == 8 && c.disc == 4 && c.user == 1 && c.none == 3 && c.unchanged == 0 &&
        c.failed == 0 && c.duplicates == 1);
    assert(records() == 8);
    struct kui_games_page page;
    unsigned row;
    list(KUI_GAMES_VIEW_SAVED, &page);
    assert(page.view == KUI_GAMES_VIEW_LIST);
    const struct kui_games_entry *e = entry(&page, "Fighting", &row);
    assert(e->directory && !e->cover && !strcmp(e->title, "Fighting"));
    e = entry(&page, "Pair", &row);
    assert(e->directory && !e->cover);
    e = entry(&page, "Twiddled Game", &row);
    assert(e->cover && !strcmp(e->title, "TWIDDLED GAME"));
    check_split(pixels[row], KUI_COVER_LARGE);
    e = entry(&page, "VQ Game", &row);
    assert(e->cover && !strcmp(e->title, "VQ GAME") && at(pixels[row], 160, 80, 80) == green);
    e = entry(&page, "Loose.gdi", &row);
    assert(e->cover && !strcmp(e->title, "LOOSE GAME"));
    assert(at(pixels[row], 160, 80, 80) == magenta && at(pixels[row], 160, 80, 20) == navy);
    e = entry(&page, "User Art", &row);
    assert(e->cover && !strcmp(e->title, "USER ART GAME"));
    assert(at(pixels[row], 160, 80, 80) == rgb565(30, 60, 200) && at(pixels[row], 160, 5, 80) == navy);
    /* The name's record belongs to the category's game, not this one. */
    e = entry(&page, "Plain Game", &row);
    assert(!e->cover && !strcmp(e->title, "Plain Game"));
    /* Listing with the saved view writes nothing; a new view is saved. */
    test.writes = 0;
    list(KUI_GAMES_VIEW_LIST, &page);
    assert(!test.writes);
    list(KUI_GAMES_VIEW_GALLERY, &page);
    assert(test.writes && page.view == KUI_GAMES_VIEW_GALLERY);
    entry(&page, "Twiddled Game", &row);
    check_split(pixels[row], KUI_COVER_MEDIUM);
    test.writes = 0;
    list(KUI_GAMES_VIEW_SAVED, &page);
    assert(!test.writes && page.view == KUI_GAMES_VIEW_GALLERY);
    list(KUI_GAMES_VIEW_COMPACT, &page);
    entry(&page, "Twiddled Game", &row);
    check_split(pixels[row], KUI_COVER_SMALL);
    assert(page.view == KUI_GAMES_VIEW_COMPACT);
    /* Image details show the large cover for exactly that GDI. */
    struct kui_games_detail detail;
    static uint16_t large[KUI_COVER_PIXELS];
    assert(kui_games_inspect_cover("/Games/Twiddled Game/disc.gdi", &detail, large, log_line, cancel));
    assert(detail.valid && detail.cover && !strcmp(detail.title, "TWIDDLED GAME"));
    check_split(large, KUI_COVER_LARGE);
    assert(kui_games_inspect_cover("/Games/Loose.gdi", &detail, large, log_line, cancel) && detail.cover);
    assert(at(large, 160, 80, 80) == magenta);
    assert(kui_games_inspect_cover("/Games/Plain Game/disc.gdi", &detail, large, log_line, cancel) && !detail.cover);
    assert(kui_games_inspect_cover("/Games/Fighting/Category Game/disc.gdi", &detail, large, log_line, cancel));
    assert(detail.cover && at(large, 160, 80, 80) == yellow);
    /* An unchanged library is checked without writing. */
    test.writes = 0;
    scan(&c, true);
    assert(c.unchanged == 8 && c.disc + c.user + c.none + c.failed == 0 && c.duplicates == 1 && !test.writes);
    /* A replaced owner image is used again; a removed one falls back to the disc. */
    char path[1024]; size_t bytes;
    snprintf(path, sizeof(path), "%s/extra/User Art.png", host);
    uint8_t *data = host_file(path, &bytes);
    test.active = false; mounted(true);
    write_file(KUI_GAMES_COVERS_FOLDER "/User Art.png", data, bytes);
    mounted(false); test.active = true;
    free(data);
    scan(&c, true);
    assert(c.user == 1 && c.unchanged == 7);
    list(KUI_GAMES_VIEW_LIST, &page);
    entry(&page, "User Art", &row);
    assert(at(pixels[row], 160, 80, 80) == rgb565(200, 60, 30));
    test.active = false; mounted(true);
    assert(f_unlink(KUI_GAMES_COVERS_FOLDER "/User Art.png") == FR_OK);
    mounted(false); test.active = true;
    scan(&c, true);
    assert(c.none == 1 && c.unchanged == 7);
    list(KUI_GAMES_VIEW_LIST, &page);
    e = entry(&page, "User Art", &row);
    assert(!e->cover && !strcmp(e->title, "USER ART GAME"));
    assert(records() == 8);
}

static void check(const char *host) {
    if(!strcmp(test.fault, "scan")) { check_scan(host); return; }
    struct kui_games_scan_counts c;
    if(!strcmp(test.fault, "cancel")) {
        test.cancel_after = 4; /* "Finding games", then during the third game. */
        scan(&c, false);
        assert(test.cancelled);
        unsigned kept = records();
        assert(kept >= 1 && kept < 8);
        return;
    }
    if(!strcmp(test.fault, "write-fail")) {
        scan(&c, true);
        assert(c.failed == 8 && c.disc + c.user + c.none == 0);
        assert(records() == 0);
        return;
    }
    assert(!strcmp(test.fault, "missing-root"));
    scan(&c, false);
    assert(c.games == 0);
}

int main(int argc, char **argv) {
    if(argc != 5) return 2;
    struct stat st; if(lstat(argv[1], &st) || !S_ISREG(st.st_mode) || st.st_size % 512) return 2;
    test.image = fopen(argv[1], "r+b"); assert(test.image);
    test.blocks = (uint64_t)st.st_size / 512; test.fault = argv[4];
    if(!strcmp(argv[3], "seed")) {
        mounted(true); seed(argv[2]); mounted(false);
    } else {
        assert(!strcmp(argv[3], "check"));
        test.active = true; check(argv[2]);
        assert(!test.files && !test.dirs && !test.connected && test.connects == test.disconnects);
    }
    assert(!fclose(test.image));
    printf("PASS Games covers %s %s\n", argv[3], test.fault);
    return 0;
}
