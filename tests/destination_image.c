/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/destination.h"
#include "kui/settings.h"
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
    uint64_t blocks;
    unsigned writes;
    const char *fault;
    bool payload_seen, injected;
} test;
static FATFS fs;
static const char *slots[] = {KUI_DEST_PATH_A, KUI_DEST_PATH_B};
static const char *kept[] = {"0:/KUI/dumps/keep/track03.bin", "0:/KUI/dumps/keep/checkpoint-a.bin"};
static const char sentinel[] = "Existing capture/checkpoint must remain unchanged.\n";
static const char *previous = "/Saved/Games", *changed = "/New/Collection", *third = "/保存/ゲーム";
static const struct kui_settings settings[] = {{false, true, false}, {true, false, true}};

static bool fault(const char *name) { return test.fault && !strcmp(test.fault, name); }
static void log_line(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
static uint64_t blocks(void *ctx) { (void)ctx; return test.blocks; }
static bool contains_record(const uint8_t *data, size_t count) {
    for(size_t i = 0; i < count; ++i)
        if(!memcmp(data + i * 512, "KUIDEST1", 8)) return true;
    return false;
}
static int read_image(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    (void)ctx;
    if(fault("read-fail")) { test.injected = true; return -1; }
    if(fseeko(test.image, (off_t)block * 512, SEEK_SET) || fread(data, 512, count, test.image) != count)
        return -1;
    if(test.payload_seen && contains_record(data, count)) {
        if(fault("readback-fail")) { test.injected = true; return -1; }
        if(fault("readback-corrupt")) { data[24] ^= 1; test.injected = true; }
    }
    return 0;
}
static int write_image(void *ctx, uint32_t block, size_t count, const uint8_t *data) {
    (void)ctx; ++test.writes;
    if(contains_record(data, count)) {
        test.payload_seen = true;
        if(fault("write-fail")) { test.injected = true; return -1; }
    }
    return fseeko(test.image, (off_t)block * 512, SEEK_SET) || fwrite(data, 512, count, test.image) != count ? -1 : 0;
}
static int sync_image(void *ctx) {
    (void)ctx;
    if(fault("sync-fail") && test.payload_seen) { test.injected = true; return -1; }
    return fflush(test.image) || fsync(fileno(test.image)) ? -1 : 0;
}
static const struct kui_media_ops media = {NULL, blocks, read_image, write_image, sync_image};
static void remount(void) {
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    kui_media_set(&media); assert(kui_mount(&fs, log_line));
}
static void check_file(const char *path, const void *expected, size_t size) {
    FIL file; UINT got; uint8_t data[512];
    assert(size <= sizeof(data));
    assert(f_open(&file, path, FA_READ) == FR_OK && f_size(&file) == size);
    assert(f_read(&file, data, (UINT)size, &got) == FR_OK && got == size);
    assert(!memcmp(data, expected, size)); assert(f_close(&file) == FR_OK);
}
static void write_file(const char *path, const void *data, size_t size) {
    FIL file; UINT written;
    assert(f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    assert(f_write(&file, data, (UINT)size, &written) == FR_OK && written == size);
    assert(f_sync(&file) == FR_OK && f_close(&file) == FR_OK);
}
static void check_slot(unsigned slot, const char *expected, uint64_t seq) {
    uint8_t record[KUI_DEST_RECORD_SIZE]; assert(kui_destination_encode(record, expected, seq));
    check_file(slots[slot], record, sizeof(record));
}
static void check_loaded(const char *expected) {
    char actual[KUI_DEST_ROOT_CAP]; memset(actual, 0xa5, sizeof(actual));
    assert(kui_destination_load(actual, log_line)); assert(!strcmp(actual, expected));
}
static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}
static void damage_newest(const char *kind) {
    uint8_t record[KUI_DEST_RECORD_SIZE + 1] = {0};
    assert(kui_destination_encode(record, changed, 2));
    size_t size = KUI_DEST_RECORD_SIZE;
    if(!strcmp(kind, "corrupt")) record[25] ^= 1;
    else if(!strcmp(kind, "truncated")) --size;
    else if(!strcmp(kind, "oversize")) ++size;
    else {
        if(!strcmp(kind, "version")) put32(record + 8, 2);
        else if(!strcmp(kind, "bad-path")) { memset(record + 24, 0, KUI_DEST_ROOT_CAP); memcpy(record + 24, "/../bad", 7); }
        else if(!strcmp(kind, "bad-padding")) record[151] = 1;
        else assert(false);
        put32(record + 152, kui_crc32(0, record, 152));
    }
    write_file(slots[1], record, size);
}
static void check_directory(const char *path) {
    char full[KUI_DEST_PATH_CAP]; FILINFO info;
    assert(snprintf(full, sizeof(full), "0:%s", path) < (int)sizeof(full));
    assert(f_stat(full, &info) == FR_OK && (info.fattrib & AM_DIR));
}
static void directories(void) {
    unsigned before = test.writes;
    assert(kui_destination_mkdirs("/", log_line)); assert(test.writes == before);
    assert(kui_destination_mkdirs("/Browse/Deep/Nested", log_line));
    check_directory("/Browse"); check_directory("/Browse/Deep"); check_directory("/Browse/Deep/Nested");
    before = test.writes;
    assert(kui_destination_mkdirs("/Browse/Deep/Nested", log_line)); assert(test.writes == before);
    write_file("0:/Browse/occupied.dat", sentinel, sizeof(sentinel));
    before = test.writes;
    assert(!kui_destination_mkdirs("/Browse/occupied.dat/child", log_line));
    assert(!kui_destination_mkdirs("/Browse/../bad", log_line)); assert(test.writes == before);
    check_file("0:/Browse/occupied.dat", sentinel, sizeof(sentinel));

    char names[23][KUI_DEST_NAME_CAP]; unsigned expected_count = 0;
    strcpy(names[expected_count++], "Deep");
    for(unsigned i = 0; i < 18; ++i) {
        snprintf(names[expected_count++], KUI_DEST_NAME_CAP, "directory-%02u", i);
    }
    strcpy(names[expected_count++], "Alpha"); strcpy(names[expected_count++], "zeta");
    strcpy(names[expected_count++], "日本語"); strcpy(names[expected_count++], "Café");
    assert(expected_count == 23);
    for(unsigned i = 1; i < expected_count; ++i) {
        char path[KUI_DEST_ROOT_CAP];
        assert(kui_destination_join(path, "/Browse", names[i]));
        assert(kui_destination_mkdirs(path, log_line));
    }
    char long_name[129], full[384]; memset(long_name, 'x', 128); long_name[128] = 0;
    snprintf(full, sizeof(full), "0:/Browse/%s", long_name); assert(f_mkdir(full) == FR_OK);
    for(unsigned i = 0; i < 64; ++i) { long_name[2 * i] = (char)0xc3; long_name[2 * i + 1] = (char)0xa9; }
    snprintf(full, sizeof(full), "0:/Browse/%s", long_name); assert(f_mkdir(full) == FR_OK);
    write_file("0:/Browse/not-a-directory.bin", sentinel, sizeof(sentinel));
    write_file("0:/Browse/日本語.txt", sentinel, sizeof(sentinel));
    remount();
    before = test.writes;

    bool seen[23] = {false}; unsigned offset = 0, disabled = 0;
    char prior_name[KUI_DEST_NAME_CAP] = "";
    struct kui_destination_entry listed[25];
    do {
        struct kui_destination_page page;
        assert(kui_destination_list("/Browse", offset, &page, log_line));
        assert(page.count <= KUI_DEST_PAGE_SIZE && page.count > 0);
        assert(offset + page.count <= sizeof(listed) / sizeof(listed[0]));
        for(unsigned i = 0; i < page.count; ++i) {
            listed[offset + i] = page.entries[i];
            if(page.entries[i].disabled) {
                assert(!strcmp(page.entries[i].name, "[Name too long]")); ++disabled;
            } else {
                assert(strcmp(prior_name, page.entries[i].name) < 0);
                strcpy(prior_name, page.entries[i].name);
                unsigned found = expected_count;
                for(unsigned j = 0; j < expected_count; ++j)
                    if(!strcmp(page.entries[i].name, names[j])) { found = j; break; }
                assert(found < expected_count && !seen[found]); seen[found] = true;
            }
        }
        offset += page.count;
        assert(page.has_more == (offset < expected_count + 2));
        if(!page.has_more) break;
        assert(page.count == KUI_DEST_PAGE_SIZE);
    } while(offset < expected_count + 2);
    assert(offset == expected_count + 2 && disabled == 2);
    for(unsigned i = 0; i < expected_count; ++i) assert(seen[i]);
    struct kui_destination_page page;
    assert(kui_destination_list("/Browse", offset, &page, log_line)); assert(!page.count && !page.has_more);
    assert(kui_destination_list("/Browse", UINT32_MAX, &page, log_line)); assert(!page.count && !page.has_more);
    assert(kui_destination_list("/Browse", 3, &page, log_line)); assert(page.count == KUI_DEST_PAGE_SIZE && page.has_more);
    for(unsigned i = 0; i < page.count; ++i) {
        assert(!strcmp(page.entries[i].name, listed[3 + i].name));
        assert(page.entries[i].disabled == listed[3 + i].disabled);
    }
    assert(kui_destination_list("/Browse", 22, &page, log_line)); assert(page.count == 3 && !page.has_more);
    for(unsigned i = 0; i < page.count; ++i) {
        assert(!strcmp(page.entries[i].name, listed[22 + i].name));
        assert(page.entries[i].disabled == listed[22 + i].disabled);
    }
    assert(kui_destination_list("/Browse/Deep/Nested", 0, &page, log_line)); assert(!page.count && !page.has_more);
    assert(!kui_destination_list("/Browse/occupied.dat", 0, &page, log_line));
    assert(!kui_destination_list("/NotPresent", 0, &page, log_line));
    assert(kui_destination_list("/", 0, &page, log_line));
    bool root_has_kui = false, root_has_browse = false;
    for(unsigned i = 0; i < page.count; ++i) {
        assert(!page.entries[i].disabled);
        root_has_kui |= !strcmp(page.entries[i].name, "KUI");
        root_has_browse |= !strcmp(page.entries[i].name, "Browse");
    }
    assert(root_has_kui && root_has_browse);
    char parent[KUI_DEST_ROOT_CAP];
    assert(kui_destination_parent(parent, "/Browse/Deep/Nested") && !strcmp(parent, "/Browse/Deep"));
    assert(kui_destination_parent(parent, "/Browse") && !strcmp(parent, "/"));
    assert(kui_destination_parent(parent, "/") && !strcmp(parent, "/"));
    assert(test.writes == before);

    /* A short directory name can still be unusable when its full root path
     * would exceed the saved destination capacity. Do not silently shorten it. */
    char long_root[122]; long_root[0] = '/'; memset(long_root + 1, 'p', 120); long_root[121] = 0;
    assert(kui_destination_mkdirs(long_root, log_line));
    snprintf(full, sizeof(full), "0:%s/ExtendsTooFar", long_root); assert(f_mkdir(full) == FR_OK);
    snprintf(full, sizeof(full), "0:%s/ok", long_root); assert(f_mkdir(full) == FR_OK);
    assert(kui_destination_list(long_root, 0, &page, log_line)); assert(page.count == 2 && !page.has_more);
    bool saw_blocked = false, saw_ok = false;
    for(unsigned i = 0; i < page.count; ++i) {
        if(!strcmp(page.entries[i].name, "ExtendsTooFar")) { assert(page.entries[i].disabled); saw_blocked = true; }
        else { assert(!strcmp(page.entries[i].name, "ok") && !page.entries[i].disabled); saw_ok = true; }
    }
    assert(saw_blocked && saw_ok);
}
int main(int argc, char **argv) {
    if(argc != 3) return 2;
    struct stat st;
    if(lstat(argv[1], &st) || !S_ISREG(st.st_mode) || st.st_size % 512) return 2;
    test.image = fopen(argv[1], "r+b"); if(!test.image) return 2;
    test.blocks = (uint64_t)st.st_size / 512;
    kui_media_set(&media); assert(kui_mount(&fs, log_line));
    const char *name = argv[2];
    if(!strcmp(name, "missing")) {
        check_loaded("/Games"); assert(!test.writes);
    } else if(!strcmp(name, "seed")) {
        check_loaded("/Games"); assert(!test.writes);
        assert(kui_destination_save(previous, log_line));
        FILINFO info; assert(f_stat("0:/Saved", &info) == FR_NO_FILE);
        assert(f_mkdir("0:/KUI/dumps") == FR_OK && f_mkdir("0:/KUI/dumps/keep") == FR_OK);
        for(unsigned i = 0; i < 2; ++i) {
            write_file(kept[i], sentinel, sizeof(sentinel));
            uint8_t record[KUI_SETTINGS_RECORD_SIZE]; assert(kui_settings_encode(record, &settings[i], 9 + i));
            write_file(i ? KUI_SETTINGS_PATH_B : KUI_SETTINGS_PATH_A, record, sizeof(record));
        }
        remount(); check_loaded(previous); check_slot(0, previous, 1);
        assert(f_stat(slots[1], &info) == FR_NO_FILE);
    } else if(!strcmp(name, "alternate")) {
        check_loaded(previous); assert(kui_destination_save(changed, log_line));
        remount(); check_loaded(changed); check_slot(0, previous, 1); check_slot(1, changed, 2);
        assert(kui_destination_save(third, log_line));
        remount(); check_loaded(third); check_slot(0, third, 3); check_slot(1, changed, 2);
    } else if(!strcmp(name, "both-invalid")) {
        uint8_t bad[KUI_DEST_RECORD_SIZE] = {0}; write_file(slots[0], bad, sizeof(bad)); write_file(slots[1], bad, sizeof(bad));
        remount(); check_loaded("/Games"); assert(kui_destination_save(third, log_line));
        remount(); check_loaded(third); check_slot(0, third, 1);
    } else if(!strcmp(name, "directories")) {
        directories(); check_loaded(previous); check_slot(0, previous, 1);
    } else if(!strcmp(name, "invalid-save")) {
        unsigned before = test.writes;
        assert(!kui_destination_save("/../bad", log_line));
        assert(!kui_destination_save("/CON", log_line)); assert(test.writes == before);
        check_loaded(previous); check_slot(0, previous, 1);
    } else if(!strcmp(name, "overflow")) {
        uint8_t record[KUI_DEST_RECORD_SIZE];
        assert(kui_destination_encode(record, previous, UINT64_MAX - 1)); write_file(slots[0], record, sizeof(record));
        assert(kui_destination_encode(record, changed, UINT64_MAX)); write_file(slots[1], record, sizeof(record));
        remount(); check_loaded(changed); unsigned before = test.writes;
        assert(!kui_destination_save(third, log_line)); assert(test.writes == before);
        remount(); check_slot(0, previous, UINT64_MAX - 1); check_slot(1, changed, UINT64_MAX); check_loaded(changed);
    } else if(!strcmp(name, "read-fail") || !strcmp(name, "save-read-fail")) {
        remount(); test.fault = "read-fail"; test.payload_seen = false;
        unsigned before = test.writes; char actual[KUI_DEST_ROOT_CAP];
        if(!strcmp(name, "read-fail")) { assert(!kui_destination_load(actual, log_line)); assert(!strcmp(actual, "/Games")); }
        else assert(!kui_destination_save(changed, log_line));
        assert(test.injected && test.writes == before);
        test.fault = NULL; remount(); check_slot(0, previous, 1); check_loaded(previous);
    } else if(!strcmp(name, "write-fail") || !strcmp(name, "sync-fail") ||
              !strcmp(name, "readback-fail") || !strcmp(name, "readback-corrupt")) {
        test.fault = name; test.payload_seen = false;
        assert(!kui_destination_save(changed, log_line)); assert(test.injected);
        test.fault = NULL; remount(); check_slot(0, previous, 1);
        char actual[KUI_DEST_ROOT_CAP]; assert(kui_destination_load(actual, log_line));
        if(!strcmp(name, "write-fail")) assert(!strcmp(actual, previous));
        else assert(!strcmp(actual, previous) || !strcmp(actual, changed));
    } else {
        assert(kui_destination_save(changed, log_line)); check_slot(1, changed, 2);
        damage_newest(name); remount(); check_loaded(previous); check_slot(0, previous, 1);
        assert(kui_destination_save(third, log_line));
        remount(); check_loaded(third); check_slot(0, previous, 1); check_slot(1, third, 2);
    }
    if(strcmp(name, "missing")) {
        for(unsigned i = 0; i < 2; ++i) {
            check_file(kept[i], sentinel, sizeof(sentinel));
            uint8_t record[KUI_SETTINGS_RECORD_SIZE]; assert(kui_settings_encode(record, &settings[i], 9 + i));
            check_file(i ? KUI_SETTINGS_PATH_B : KUI_SETTINGS_PATH_A, record, sizeof(record));
        }
    }
    assert(f_mount(NULL, "0:", 0) == FR_OK && fclose(test.image) == 0);
    printf("PASS destination %s: existing settings and captures preserved\n", name);
    return 0;
}
