/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/system_settings.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Exercise the real system preferences adapter on a disposable image.
 * Every fault is introduced at the block adapter, below FatFs. */
static struct {
    FILE *image;
    uint64_t blocks;
    unsigned writes;
    const char *fault;
    bool payload_seen, injected;
} test;
static FATFS fs;
static const char *slots[] = {KUI_SYSTEM_SETTINGS_PATH_A, KUI_SYSTEM_SETTINGS_PATH_B};
static const char *kept[] = {"0:/KUI/settings-a.bin", "0:/KUI/settings-b.bin"};
static const char sentinel[] = "Existing ripper settings must remain unchanged.\n";
static const struct kui_system_settings previous = {KUI_VIDEO_NTSC60, false, true, 40, false, KUI_STARTUP_VMU, true, 1};
static const struct kui_system_settings changed = {KUI_VIDEO_AUTO, true, false, 75, true, KUI_STARTUP_HOME, false, 0};
static const struct kui_system_settings third = {KUI_VIDEO_PAL50, false, true, 100, true, KUI_STARTUP_MUSIC, true, 2};

static bool fault(const char *name) { return test.fault && !strcmp(test.fault, name); }
static void log_line(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
static uint64_t blocks(void *ctx) { (void)ctx; return test.blocks; }
static bool contains_record(const uint8_t *data, size_t count) {
    for(size_t i = 0; i < count; ++i)
        if(!memcmp(data + i * 512, "KUISYS01", 8)) return true;
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
        if(fault("readback-substitute")) {
            /* CRC-valid, same-sequence but different content must still fail
             * the exact reread check. Only the returned buffer is changed. */
            for(size_t i=0;i<count;i++) if(!memcmp(data+i*512,"KUISYS01",8)) {
                uint8_t *record=data+i*512;record[26]=20;
                uint32_t crc=kui_crc32(0,record,36);
                for(unsigned n=0;n<4;n++) record[36+n]=(uint8_t)(crc>>(8*n));
                test.injected=true;
            }
        }
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
static void equals(const struct kui_system_settings *actual, const struct kui_system_settings *expected) {
    assert(actual->video_mode == expected->video_mode);
    assert(actual->music_enabled == expected->music_enabled);
    assert(actual->music_volume == expected->music_volume);
    assert(actual->show_memory == expected->show_memory);
    assert(actual->startup_chime == expected->startup_chime);
    assert(actual->startup_app == expected->startup_app);
    assert(actual->screen_inset == expected->screen_inset);
    assert(actual->menu_sounds == expected->menu_sounds);
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
static void check_slot(unsigned slot, const struct kui_system_settings *expected, uint64_t seq) {
    uint8_t record[KUI_SYSTEM_SETTINGS_RECORD_SIZE]; assert(kui_system_settings_encode(record, expected, seq));
    check_file(slots[slot], record, sizeof(record));
}
static void check_loaded(const struct kui_system_settings *expected) {
    struct kui_system_settings actual = {0};
    assert(kui_system_settings_load(&actual, true, log_line)); equals(&actual, expected);
}
static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}
static void damage_newest(const char *kind) {
    uint8_t record[KUI_SYSTEM_SETTINGS_RECORD_SIZE + 1] = {0};
    assert(kui_system_settings_encode(record, &changed, 2));
    size_t size = KUI_SYSTEM_SETTINGS_RECORD_SIZE;
    if(!strcmp(kind, "corrupt")) record[24] ^= 1;
    else if(!strcmp(kind, "truncated")) size = KUI_SYSTEM_SETTINGS_RECORD_SIZE - 1;
    else if(!strcmp(kind, "oversize")) size = KUI_SYSTEM_SETTINGS_RECORD_SIZE + 1;
    else if(!strcmp(kind, "version")) { put32(record + 8, 4); put32(record + 36, kui_crc32(0, record, 36)); }
    else if(!strcmp(kind, "flags")) { record[25] = 16; put32(record + 36, kui_crc32(0, record, 36)); }
    else if(!strcmp(kind, "startup-app")) { record[27] = KUI_STARTUP_APP_COUNT; put32(record + 36, kui_crc32(0, record, 36)); }
    else if(!strcmp(kind, "safe-area")) { record[28] = 3; put32(record + 36, kui_crc32(0, record, 36)); }
    else if(!strcmp(kind, "reserved")) { record[29] = 1; put32(record + 36, kui_crc32(0, record, 36)); }
    else assert(false);
    write_file(slots[1], record, size);
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
        struct kui_system_settings migrated=previous,expected=changed;expected.show_memory=false;
        assert(kui_system_settings_load(&migrated,false,log_line));equals(&migrated,&expected);
        assert(!test.writes);
    } else if(!strcmp(name, "seed")) {
        check_loaded(&changed); assert(!test.writes);
        assert(kui_system_settings_save(&previous, log_line));
        for(unsigned i = 0; i < 2; ++i) write_file(kept[i], sentinel, sizeof(sentinel));
        remount(); check_loaded(&previous); check_slot(0, &previous, 1);
        FILINFO info; assert(f_stat(slots[1], &info) == FR_NO_FILE);
    } else if(!strcmp(name, "alternate")) {
        check_loaded(&previous);
        assert(kui_system_settings_save(&changed, log_line));
        remount(); check_loaded(&changed); check_slot(0, &previous, 1); check_slot(1, &changed, 2);
        assert(kui_system_settings_save(&third, log_line));
        remount(); check_loaded(&third); check_slot(0, &third, 3); check_slot(1, &changed, 2);
    } else if(!strcmp(name, "v1-migrate") || !strcmp(name, "v1-fallback") || !strcmp(name,"v2-migrate")) {
        uint8_t legacy[KUI_SYSTEM_SETTINGS_RECORD_SIZE];
        assert(kui_system_settings_encode(legacy,&previous,1));
        bool v2=!strcmp(name,"v2-migrate");
        put32(legacy+8,v2?2:1);legacy[25]&=7u;if(!v2) {legacy[25]&=3u;legacy[27]=0;}legacy[28]=0;
        put32(legacy+36,kui_crc32(0,legacy,36));write_file(slots[0],legacy,sizeof(legacy));
        if(!strcmp(name,"v1-fallback")) damage_newest("version");
        remount();unsigned before=test.writes;
        struct kui_system_settings migrated=previous;
        if(!v2) {migrated.startup_chime=true;migrated.startup_app=KUI_STARTUP_HOME;}migrated.screen_inset=0;migrated.menu_sounds=false;
        check_loaded(&migrated);assert(test.writes==before);
        check_file(slots[0],legacy,sizeof(legacy));
        assert(kui_system_settings_save(&third,log_line));
        remount();check_loaded(&third);check_slot(1,&third,2);
        check_file(slots[0],legacy,sizeof(legacy));
    } else if(!strcmp(name, "both-invalid")) {
        uint8_t bad[KUI_SYSTEM_SETTINGS_RECORD_SIZE] = {0}; write_file(slots[0], bad, sizeof(bad)); write_file(slots[1], bad, sizeof(bad));
        remount(); check_loaded(&changed);
        struct kui_system_settings actual=previous;unsigned before=test.writes;
        assert(kui_system_settings_load(&actual,false,log_line));equals(&actual,&changed);assert(test.writes==before);
        assert(kui_system_settings_save(&third, log_line));
        remount(); check_loaded(&third); check_slot(0, &third, 1);
    } else if(!strcmp(name, "invalid-save")) {
        unsigned before=test.writes;struct kui_system_settings invalid=previous;
        invalid.video_mode=KUI_VIDEO_MODE_COUNT;
        assert(!kui_system_settings_save(&invalid,log_line));
        invalid=previous;invalid.music_volume=101;
        assert(!kui_system_settings_save(&invalid,log_line));
        invalid=previous;invalid.startup_app=KUI_STARTUP_APP_COUNT;
        assert(!kui_system_settings_save(&invalid,log_line));
        assert(!kui_system_settings_save(NULL,log_line));assert(test.writes==before);
        remount();check_loaded(&previous);check_slot(0,&previous,1);
    } else if(!strcmp(name, "parent-is-file")) {
        assert(f_unlink(slots[0])==FR_OK);
        assert(f_unlink("0:/KUI/apps/system")==FR_OK);
        assert(f_unlink("0:/KUI/apps")==FR_OK);
        write_file("0:/KUI/apps",sentinel,sizeof(sentinel));
        unsigned before=test.writes;
        assert(!kui_system_settings_save(&third,log_line));assert(test.writes==before);
        check_file("0:/KUI/apps",sentinel,sizeof(sentinel));
    } else if(!strcmp(name, "overflow")) {
        uint8_t record[KUI_SYSTEM_SETTINGS_RECORD_SIZE];
        assert(kui_system_settings_encode(record, &previous, UINT64_MAX - 1));
        write_file(slots[0], record, sizeof(record));
        assert(kui_system_settings_encode(record, &changed, UINT64_MAX));
        write_file(slots[1], record, sizeof(record));
        remount(); check_loaded(&changed);
        unsigned before = test.writes;
        assert(!kui_system_settings_save(&third, log_line)); assert(test.writes == before);
        remount(); check_slot(0, &previous, UINT64_MAX - 1); check_slot(1, &changed, UINT64_MAX);
        check_loaded(&changed);
    } else if(!strcmp(name, "read-fail")) {
        remount(); test.fault = name; test.payload_seen = false;
        struct kui_system_settings actual = previous;
        assert(!kui_system_settings_load(&actual, true, log_line)); assert(test.injected);
        equals(&actual, &changed);
        test.fault = NULL; remount(); check_slot(0, &previous, 1); check_loaded(&previous);
    } else if(!strcmp(name, "save-read-fail")) {
        remount(); test.fault = "read-fail"; test.payload_seen = false;
        unsigned before = test.writes;
        assert(!kui_system_settings_save(&changed, log_line)); assert(test.injected && test.writes == before);
        test.fault = NULL; remount(); check_slot(0, &previous, 1); check_loaded(&previous);
    } else if(!strcmp(name, "write-fail") || !strcmp(name, "sync-fail") ||
              !strcmp(name, "readback-fail") || !strcmp(name, "readback-corrupt") || !strcmp(name,"readback-substitute")) {
        test.fault = name; test.payload_seen = false;
        assert(!kui_system_settings_save(&changed, log_line)); assert(test.injected);
        test.fault = NULL; remount(); check_slot(0, &previous, 1);
        /* A reported sync/readback error can leave the replacement valid on
         * disk. It must never damage the previous confirmed record. */
        struct kui_system_settings actual;
        assert(kui_system_settings_load(&actual, true, log_line));
        if(!strcmp(name, "write-fail")) equals(&actual, &previous);
        if(actual.video_mode==previous.video_mode) equals(&actual,&previous);
        else equals(&actual,&changed);
    } else {
        assert(kui_system_settings_save(&changed, log_line)); check_slot(1, &changed, 2);
        damage_newest(name); remount(); check_loaded(&previous); check_slot(0, &previous, 1);
        assert(kui_system_settings_save(&third, log_line));
        remount(); check_loaded(&third); check_slot(0, &previous, 1); check_slot(1, &third, 2);
    }
    if(strcmp(name, "missing"))
        for(unsigned i = 0; i < 2; ++i) check_file(kept[i], sentinel, sizeof(sentinel));
    assert(f_mount(NULL, "0:", 0) == FR_OK && fclose(test.image) == 0);
    printf("PASS system settings %s: ripper settings preserved\n", name);
    return 0;
}
