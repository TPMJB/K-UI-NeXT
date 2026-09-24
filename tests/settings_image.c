/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/settings.h"
#include "kui/options.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Exercise the real settings and options file adapters on a disposable image.
 * Every fault is introduced at the block adapter, below FatFs. */
static struct {
    FILE *image;
    uint64_t blocks;
    unsigned writes;
    const char *fault;
    bool payload_seen, injected;
} test;
static FATFS fs;
static const char *slots[] = {"0:/KUI/settings-a.bin", "0:/KUI/settings-b.bin"};
static const char *kept[] = {"0:/KUI/dumps/keep/track03.bin", "0:/KUI/dumps/keep/checkpoint-a.bin"};
static const char sentinel[] = "Existing capture/checkpoint must remain unchanged.\n";
static const struct kui_settings previous = {false, true, false};
static const struct kui_settings changed = {true, false, true};
static const struct kui_settings third = {true, true, false};

static bool fault(const char *name) { return test.fault && !strcmp(test.fault, name); }
static void log_line(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
static uint64_t blocks(void *ctx) { (void)ctx; return test.blocks; }
static bool contains_record(const uint8_t *data, size_t count) {
    for(size_t i = 0; i < count; ++i)
        if(!memcmp(data + i * 512, "KUISET01", 8)) return true;
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
    kui_media_set(&media);
    assert(kui_mount(&fs, log_line));
}
static void equals(const struct kui_settings *actual, const struct kui_settings *expected) {
    assert(actual->crc_only == expected->crc_only);
    assert(actual->end_readback == expected->end_readback);
    assert(actual->show_memory == expected->show_memory);
}
static void check_file(const char *path, const void *expected, size_t size) {
    FIL file; UINT got; uint8_t data[128];
    assert(size <= sizeof(data));
    assert(f_open(&file, path, FA_READ) == FR_OK && f_size(&file) == size);
    assert(f_read(&file, data, (UINT)size, &got) == FR_OK && got == size);
    assert(!memcmp(data, expected, size));
    assert(f_close(&file) == FR_OK);
}
static void write_file(const char *path, const void *data, size_t size) {
    FIL file; UINT written;
    assert(f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    assert(f_write(&file, data, (UINT)size, &written) == FR_OK && written == size);
    assert(f_sync(&file) == FR_OK && f_close(&file) == FR_OK);
}
static void check_slot(unsigned slot, const struct kui_settings *expected, uint64_t seq) {
    uint8_t record[32]; assert(kui_settings_encode(record, expected, seq));
    check_file(slots[slot], record, sizeof(record));
}
static void check_loaded(const struct kui_settings *expected) {
    struct kui_settings actual = {0};
    assert(kui_settings_load(&actual, log_line)); equals(&actual, expected);
}
static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}
static void damage_newest(const char *kind) {
    uint8_t record[33] = {0};
    assert(kui_settings_encode(record, &changed, 2));
    size_t size = 32;
    if(!strcmp(kind, "corrupt")) record[24] ^= 1;
    else if(!strcmp(kind, "truncated")) size = 31;
    else if(!strcmp(kind, "oversize")) size = 33;
    else if(!strcmp(kind, "version")) { put32(record + 8, 2); put32(record + 28, kui_crc32(0, record, 28)); }
    else if(!strcmp(kind, "flags")) { put32(record + 24, 8); put32(record + 28, kui_crc32(0, record, 28)); }
    else assert(false);
    write_file(slots[1], record, size);
}
static void options_cases(void) {
    const char *path = "0:/KUI/bench.cfg";
    struct kui_options defaults, custom, actual;
    kui_options_default(&defaults);
    custom = defaults;
    custom.capture_crc_only[0] = true; custom.end_readback[0] = false;
    custom.sd_mib = 16; custom.ui_hz[0] = 4;
    actual = custom;
    assert(kui_options_overlay(&actual, path, log_line));
    assert(!memcmp(&actual, &custom, sizeof(actual)));
    actual = custom;
    assert(kui_options_load(&actual, path, log_line));
    assert(!memcmp(&actual, &defaults, sizeof(actual)));

    /* Only explicit bench keys replace saved capture choices. In particular,
     * a UI-only benchmark must not reset a nondefault hash/readback setting. */
    const char partial_options[] = "ui_hz=0\n";
    write_file(path, partial_options, sizeof(partial_options) - 1);
    struct kui_options persisted = custom;
    persisted.capture_crc_only[0] = false; persisted.end_readback[0] = true;
    actual = persisted;
    assert(kui_options_overlay(&actual, path, log_line));
    struct kui_options expected = persisted; expected.ui_hz[0] = 0;
    assert(!memcmp(&actual, &expected, sizeof(actual)));

    const char explicit_options[] = "capture_hash=both\nend_readback=on\n";
    write_file(path, explicit_options, sizeof(explicit_options) - 1);
    actual = custom;
    assert(kui_options_overlay(&actual, path, log_line));
    expected = custom;
    expected.capture_crc_only[0] = false; expected.end_readback[0] = true;
    assert(!memcmp(&actual, &expected, sizeof(actual)));
    actual = custom;
    assert(kui_options_load(&actual, path, log_line));
    expected = defaults;
    expected.capture_crc_only[0] = false; expected.end_readback[0] = true;
    assert(!memcmp(&actual, &expected, sizeof(actual)));

    const char malformed[] = "capture_hash=both\nend_readback=maybe\n";
    write_file(path, malformed, sizeof(malformed) - 1);
    actual = custom;
    assert(!kui_options_overlay(&actual, path, log_line));
    assert(!memcmp(&actual, &custom, sizeof(actual)));
    actual = custom;
    assert(!kui_options_load(&actual, path, log_line));
    assert(!memcmp(&actual, &defaults, sizeof(actual)));
    assert(f_unlink(path) == FR_OK);
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
        check_loaded(&changed); assert(!test.writes);
    } else if(!strcmp(name, "seed")) {
        check_loaded(&changed); assert(!test.writes);
        assert(kui_settings_save(&previous, log_line));
        assert(f_mkdir("0:/KUI/dumps") == FR_OK && f_mkdir("0:/KUI/dumps/keep") == FR_OK);
        for(unsigned i = 0; i < 2; ++i) write_file(kept[i], sentinel, sizeof(sentinel));
        remount(); check_loaded(&previous); check_slot(0, &previous, 1);
        FILINFO info; assert(f_stat(slots[1], &info) == FR_NO_FILE);
    } else if(!strcmp(name, "alternate")) {
        check_loaded(&previous);
        assert(kui_settings_save(&changed, log_line));
        remount(); check_loaded(&changed); check_slot(0, &previous, 1); check_slot(1, &changed, 2);
        assert(kui_settings_save(&third, log_line));
        remount(); check_loaded(&third); check_slot(0, &third, 3); check_slot(1, &changed, 2);
    } else if(!strcmp(name, "both-invalid")) {
        uint8_t bad[32] = {0}; write_file(slots[0], bad, sizeof(bad)); write_file(slots[1], bad, sizeof(bad));
        remount(); check_loaded(&changed);
        assert(kui_settings_save(&third, log_line));
        remount(); check_loaded(&third); check_slot(0, &third, 1);
    } else if(!strcmp(name, "options")) {
        options_cases(); check_loaded(&previous); check_slot(0, &previous, 1);
    } else if(!strcmp(name, "overflow")) {
        uint8_t record[32];
        assert(kui_settings_encode(record, &previous, UINT64_MAX - 1));
        write_file(slots[0], record, sizeof(record));
        assert(kui_settings_encode(record, &changed, UINT64_MAX));
        write_file(slots[1], record, sizeof(record));
        remount(); check_loaded(&changed);
        unsigned before = test.writes;
        assert(!kui_settings_save(&third, log_line)); assert(test.writes == before);
        remount(); check_slot(0, &previous, UINT64_MAX - 1); check_slot(1, &changed, UINT64_MAX);
        check_loaded(&changed);
    } else if(!strcmp(name, "read-fail")) {
        remount(); test.fault = name; test.payload_seen = false;
        struct kui_settings actual = previous;
        assert(!kui_settings_load(&actual, log_line)); assert(test.injected);
        equals(&actual, &changed);
        test.fault = NULL; remount(); check_slot(0, &previous, 1); check_loaded(&previous);
    } else if(!strcmp(name, "save-read-fail")) {
        remount(); test.fault = "read-fail"; test.payload_seen = false;
        unsigned before = test.writes;
        assert(!kui_settings_save(&changed, log_line)); assert(test.injected && test.writes == before);
        test.fault = NULL; remount(); check_slot(0, &previous, 1); check_loaded(&previous);
    } else if(!strcmp(name, "write-fail") || !strcmp(name, "sync-fail") ||
              !strcmp(name, "readback-fail") || !strcmp(name, "readback-corrupt")) {
        test.fault = name; test.payload_seen = false;
        assert(!kui_settings_save(&changed, log_line)); assert(test.injected);
        test.fault = NULL; remount(); check_slot(0, &previous, 1);
        /* A reported sync/readback error can leave the replacement valid on
         * disk. It must never damage the previous confirmed record. */
        struct kui_settings actual;
        assert(kui_settings_load(&actual, log_line));
        if(!strcmp(name, "write-fail")) equals(&actual, &previous);
        assert((actual.crc_only == previous.crc_only && actual.end_readback == previous.end_readback && actual.show_memory == previous.show_memory) ||
               (actual.crc_only == changed.crc_only && actual.end_readback == changed.end_readback && actual.show_memory == changed.show_memory));
    } else {
        assert(kui_settings_save(&changed, log_line)); check_slot(1, &changed, 2);
        damage_newest(name); remount(); check_loaded(&previous); check_slot(0, &previous, 1);
        assert(kui_settings_save(&third, log_line));
        remount(); check_loaded(&third); check_slot(0, &previous, 1); check_slot(1, &third, 2);
    }
    if(strcmp(name, "missing"))
        for(unsigned i = 0; i < 2; ++i) check_file(kept[i], sentinel, sizeof(sentinel));
    assert(f_mount(NULL, "0:", 0) == FR_OK && fclose(test.image) == 0);
    printf("PASS settings %s: old capture files preserved\n", name);
    return 0;
}
