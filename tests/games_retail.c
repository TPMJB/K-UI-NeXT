/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/games_retail.h"
#include "kui/games.h"
#include "kui/media.h"
#include "kui/retail_image.h"
#include "kui/retail_cursor.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Real FatFs preparation, followed by complete reads through the freestanding
 * reader using physical card blocks only. No open FatFs file survives handoff. */
static struct {
    FILE *image;
    uint64_t blocks;
    const char *fault;
    FIL *track;
    unsigned writes, connects, disconnects, files, read_calls, physical_reads;
    uint32_t first_sector;
    bool boot_read;
    char track_name[96];
    bool active, connected, injected, cancelled;
} test;
static FATFS fs;
static const char *const selected = "/Games/Reader Test/disc.gdi";
static const char *const package = "0:/KUI/apps/games/retail-boot.kui";
static const char *const ce_package = "0:/KUI/apps/games/ce-probe.kui";
static const char *const names[] = {"track01.bin", "music track02.raw", "track03.bin", "music track04.raw"};
static const uint32_t starts[] = {0, 4, 45000, 45064};
static const uint32_t counts[] = {4, 4, 64, 4};
/* Tracks 5 on are copies of track 2's audio, four sectors each, end to end. */
static unsigned track_count(void) {
    return !strcmp(test.fault, "tracks-99") ? 99u :
        !strcmp(test.fault, "tracks-31") || !strcmp(test.fault, "async-tracks-31") ? 31u :
        !strcmp(test.fault, "async-tracks-40") ? 40u :
        !strcmp(test.fault, "cdda-warning") ? 4u : 3u;
}
static const char *track_name(unsigned i, char generated[32]) {
    snprintf(generated, 32, "track%02u.raw", i + 1);
    return i < 4 ? names[i] : generated;
}
static uint32_t track_start(unsigned i) { return i < 4 ? starts[i] : 45064u + (i - 3u) * 4u; }
static uint32_t track_sectors(unsigned i) { return i < 4 ? counts[i] : 4u; }
/* Preserve a raw data track alongside cooked high-density data; the async
 * case also cooks track 1, while audio always remains raw. */
static bool cooked_track(unsigned i) {
    bool cooked = !strcmp(test.fault, "cooked-2048") || !strcmp(test.fault, "cooked-boot-tail") ||
        !strcmp(test.fault, "async-cooked-2048") || !strcmp(test.fault, "ce-probe-cooked");
    return cooked && (i == 2 || (i == 0 && !strcmp(test.fault, "async-cooked-2048")));
}
static uint32_t track_stride(unsigned i) { return cooked_track(i) ? 2048u : 2352u; }
/* Maps whose tracks with their files exceed the reader's slots list audio
 * tracks without them: 99 + 99 > 160, and the background reader's 40 + 40 > 64. */
static bool audio_unmapped(void) {
    return !strcmp(test.fault, "tracks-99") || !strcmp(test.fault, "async-tracks-40");
}
static bool fault(const char *name) { return test.active && !strcmp(test.fault, name); }
static bool format_case(void) { return !strncmp(test.fault, "format-", 7); }
struct format_track {
    uint32_t lba, sectors, control, stride, mode, header;
    unsigned long long offset;
};
struct format_expect {
    char selected[KUI_GAME_NAME_CAP];
    unsigned valid, session, boot_lba, boot_bytes, flags, count;
    struct format_track track[KUI_GAME_TRACK_MAX];
};
static struct format_expect format_expect(const char *directory) {
    struct format_expect expected = {0};
    char path[1024];
    assert(snprintf(path, sizeof(path), "%s/format.expect", directory) < (int)sizeof(path));
    FILE *file = fopen(path, "r"); assert(file);
    assert(fgets(expected.selected, sizeof(expected.selected), file));
    size_t len = strlen(expected.selected);
    assert(len && expected.selected[len - 1] == '\n'); expected.selected[len - 1] = 0;
    assert(fscanf(file, "%u %u %u %u %u %u", &expected.valid, &expected.session,
        &expected.boot_lba, &expected.boot_bytes, &expected.flags, &expected.count) == 6);
    assert(expected.count && expected.count <= KUI_GAME_TRACK_MAX);
    for(unsigned i = 0; i < expected.count; ++i) {
        struct format_track *track = &expected.track[i];
        assert(fscanf(file, "%u %u %u %u %u %u %llu", &track->lba, &track->sectors,
            &track->control, &track->stride, &track->mode, &track->header, &track->offset) == 7);
        assert(track->sectors && track->sectors <= 64);
    }
    assert(!ferror(file) && !fclose(file));
    return expected;
}
static void log_line(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
static uint64_t blocks(void *ctx) { (void)ctx; return test.blocks; }
static int read_image(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    (void)ctx;
    assert(block <= test.blocks && count <= test.blocks - block);
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
unsigned kui_storage_active(void) {
    /* The CE boot test needs SCI; ce-probe-scif checks the refusal. */
    return !strcmp(test.fault, "async-on-sci") || !strcmp(test.fault, "async-cooked-2048") ||
        !strncmp(test.fault, "async-tracks", 12) ||
        (format_case() && strstr(test.fault, "-async")) ||
        (!strncmp(test.fault, "ce-probe", 8) && strcmp(test.fault, "ce-probe-scif")) ?
        KUI_STORAGE_SCI : KUI_STORAGE_SCIF;
}
bool kui_sd_connect(void) {
    assert(!test.connected); ++test.connects;
    test.connected = true; kui_media_set(&media); return true;
}
void kui_sd_disconnect(void) {
    assert(test.connected && !test.files);
    test.connected = false; ++test.disconnects; kui_media_set(NULL);
}
static bool cancel(void) { return test.cancelled || fault("cancel-before"); }
FRESULT __real_f_open(FIL *, const TCHAR *, BYTE);
FRESULT __wrap_f_open(FIL *file, const TCHAR *path, BYTE flags) {
    if(test.active) assert(!(flags & (FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_OPEN_APPEND)));
    FRESULT result = __real_f_open(file, path, flags);
    if(test.active && result == FR_OK) {
        ++test.files;
        if(strstr(path, "track")) {
            test.track = file;
            snprintf(test.track_name, sizeof(test.track_name), "%s", path);
            if(fault("size-change")) { ++file->obj.objsize; test.injected = true; }
        }
    }
    return result;
}
FRESULT __real_f_read(FIL *, void *, UINT, UINT *);
FRESULT __wrap_f_read(FIL *file, void *buffer, UINT bytes, UINT *got) {
    FRESULT result = __real_f_read(file, buffer, bytes, got);
    if(test.active) {
        ++test.read_calls;
        if(file == test.track) {
            if(fault("read-fail")) { test.injected = true; return FR_DISK_ERR; }
            /* Raw boot files are read only by the detached stage; cooked
             * preparation reads their exact logical bytes for expected CRC. */
            FSIZE_t end = f_tell(file), start = end - *got;
            if(strstr(test.track_name, "track03") && start < 23u * track_stride(2) &&
               end > 21u * track_stride(2))
                test.boot_read = true;
            /* The last IP sector: the launcher's final read of track data. */
            if(bytes == 2352 && f_tell(file) == 16u * 2352u && fault("cancel-ip")) {
                test.cancelled = true; test.injected = true;
            }
        }
    }
    return result;
}
FRESULT __real_f_lseek(FIL *, FSIZE_t);
FRESULT __wrap_f_lseek(FIL *file, FSIZE_t offset) {
    if(file == test.track && fault("seek-fail")) { test.injected = true; return FR_DISK_ERR; }
    FRESULT result = __real_f_lseek(file, offset);
    if(test.active && file == test.track && offset == CREATE_LINKMAP && result == FR_OK) {
        /* Corrupt the allocation-table runs the launcher maps from: a cluster
         * before the data area, past the volume, or aliasing the first track. */
        DWORD *run = file->cltbl + 1;
        if(fault("cancel-map")) { test.cancelled = true; test.injected = true; }
        if(fault("sector-beforedata")) { run[1] = 1; test.injected = true; }
        if(fault("sector-aftercard")) { run[1] = file->obj.fs->n_fatent; test.injected = true; }
        if(fault("sector-repeat")) {
            if(!test.first_sector) test.first_sector = run[1];
            run[1] = test.first_sector; test.injected = true;
        }
    }
    return result;
}
FRESULT __real_f_close(FIL *);
FRESULT __wrap_f_close(FIL *file) {
    bool track = file == test.track;
    FRESULT result = __real_f_close(file);
    if(test.active) {
        assert(test.files); --test.files;
        if(track) test.track = NULL;
        if(track && fault("close-fail")) { test.injected = true; return FR_DISK_ERR; }
    }
    return result;
}
FRESULT __real_f_mount(FATFS *, const TCHAR *, BYTE);
FRESULT __wrap_f_mount(FATFS *volume, const TCHAR *path, BYTE option) {
    FRESULT result = __real_f_mount(volume, path, option);
    if(!volume && fault("unmount-fail")) { test.injected = true; return FR_DISK_ERR; }
    return result;
}
FRESULT __real_f_write(FIL *, const void *, UINT, UINT *);
FRESULT __wrap_f_write(FIL *file, const void *data, UINT bytes, UINT *written) {
    assert(!test.active); return __real_f_write(file, data, bytes, written);
}
FRESULT __real_f_mkdir(const TCHAR *);
FRESULT __wrap_f_mkdir(const TCHAR *path) { assert(!test.active); return __real_f_mkdir(path); }
FRESULT __real_f_unlink(const TCHAR *);
FRESULT __wrap_f_unlink(const TCHAR *path) { assert(!test.active); return __real_f_unlink(path); }
FRESULT __real_f_rename(const TCHAR *, const TCHAR *);
FRESULT __wrap_f_rename(const TCHAR *a, const TCHAR *b) { assert(!test.active); return __real_f_rename(a, b); }

static uint8_t *host_file(const char *directory, const char *name, size_t *bytes) {
    char path[1024]; struct stat st;
    assert(snprintf(path, sizeof(path), "%s/%s", directory, name) < (int)sizeof(path));
    assert(!stat(path, &st) && st.st_size > 0 && st.st_size < 1000000);
    *bytes = (size_t)st.st_size;
    uint8_t *data = malloc(*bytes); assert(data);
    FILE *file = fopen(path, "rb"); assert(file);
    assert(fread(data, 1, *bytes, file) == *bytes && !ferror(file) && !fclose(file));
    return data;
}
static void write_file(const char *path, const void *data, size_t bytes) {
    FIL file; UINT written;
    assert(bytes <= UINT32_MAX);
    assert(f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    assert(f_write(&file, data, (UINT)bytes, &written) == FR_OK && written == bytes);
    assert(f_close(&file) == FR_OK);
}
static void write_format_file(const char *path, const void *data, size_t bytes) {
    if(!strstr(test.fault, "-fragmented") || !strstr(path, ".bin")) {
        write_file(path, data, bytes);
        return;
    }
    FIL file; assert(f_open(&file, path, FA_WRITE | FA_CREATE_NEW) == FR_OK);
    size_t chunk = (size_t)fs.csize * 512u;
    uint8_t *blocker = malloc(chunk); assert(blocker); memset(blocker, 0xAD, chunk);
    for(size_t offset = 0; offset < bytes; ) {
        size_t take = bytes - offset; if(take > chunk) take = chunk;
        UINT written;
        assert(f_write(&file, (const uint8_t *)data + offset, (UINT)take, &written) == FR_OK && written == take);
        assert(f_sync(&file) == FR_OK);
        if(offset / chunk < 32u) {
            char blocker_path[96];
            snprintf(blocker_path, sizeof(blocker_path), "0:/Games/format-block-%04u.dat", (unsigned)(offset / chunk));
            write_file(blocker_path, blocker, chunk);
        }
        offset += take;
    }
    assert(f_close(&file) == FR_OK); free(blocker);
}
static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
static void seed(const char *directory) {
    assert(f_mkdir("0:/KUI") == FR_OK && f_mkdir("0:/KUI/apps") == FR_OK);
    assert(f_mkdir("0:/KUI/apps/games") == FR_OK && f_mkdir("0:/Games") == FR_OK);
    assert(f_mkdir("0:/Games/Reader Test") == FR_OK);
    size_t size; uint8_t *data = host_file(directory, "retail-boot.kui", &size);
    if(!strcmp(test.fault, "layout-manifest")) {
        put32(data + KUI_RUNTIME_HEADER_BYTES + 0x100 + 20, 4092);
        put32(data + 32, kui_crc32(0, data + KUI_RUNTIME_HEADER_BYTES, size - KUI_RUNTIME_HEADER_BYTES));
        put32(data + 60, kui_crc32(0, data, 60));
    }
    if(!strcmp(test.fault, "layout-entry")) put32(data + 64 + 0x100 + 32, 0x8ce00004);
    if(!strcmp(test.fault, "layout-size")) put32(data + 64 + 0x100 + 28, 20);
    if(!strcmp(test.fault, "layout-resident")) put32(data + 64 + 0x100 + 52, 0x8c008304);
    if(!strcmp(test.fault, "layout-flags")) put32(data + 64 + 0x100 + 60, 1);
    if(!strcmp(test.fault, "manifest-not-empty")) data[64 + 0x1000] = 1;
    if(!strncmp(test.fault, "layout-", 7) || !strcmp(test.fault, "manifest-not-empty")) {
        put32(data + 32, kui_crc32(0, data + 64, size - 64));
        put32(data + 60, kui_crc32(0, data, 60));
    }
    if(!strcmp(test.fault, "payload-checksum")) data[size - 1] ^= 1;
    write_file(package, data, size);
    /* The CE boot test reads its own package; ce-probe-package gives it
     * the native one instead. */
    if(!strcmp(test.fault, "ce-probe-package")) write_file(ce_package, data, size);
    free(data);
    if(strcmp(test.fault, "ce-probe-package")) {
        data = host_file(directory, "ce-probe.kui", &size);
        write_file(ce_package, data, size); free(data);
    }
    if(format_case()) {
        char list_path[1024], name[KUI_GAME_NAME_CAP], path[256];
        assert(snprintf(list_path, sizeof(list_path), "%s/format.files", directory) < (int)sizeof(list_path));
        FILE *list = fopen(list_path, "r"); assert(list);
        while(fgets(name, sizeof(name), list)) {
            size_t len = strlen(name);
            assert(len && name[len - 1] == '\n'); name[len - 1] = 0;
            assert(!strchr(name, '/'));
            assert(snprintf(path, sizeof(path), "0:/Games/Reader Test/%s", name) < (int)sizeof(path));
            data = host_file(directory, name, &size);
            write_format_file(path, data, size); free(data);
        }
        assert(!ferror(list) && !fclose(list));
        return;
    }
    data = host_file(directory, "disc.gdi", &size);
    write_file("0:/Games/Reader Test/disc.gdi", data, size); free(data);
    for(unsigned i = 0; i < track_count(); ++i) {
        if(i == 1 && !strcmp(test.fault, "missing-track")) continue;
        char path[256], generated[32];
        const char *name = track_name(i, generated);
        snprintf(path, sizeof(path), "0:/Games/Reader Test/%s", name);
        data = host_file(directory, name, &size);
        if(i == 2 && !strcmp(test.fault, "bad-ip")) data[16] ^= 1;
        if(i == 2 && !strcmp(test.fault, "bad-bootfile")) data[16 + 96] = 'X';
        if(i == 2 && !strcmp(test.fault, "bad-media")) data[16 + 37] = 'X';
        if(i == 2 && !strcmp(test.fault, "windows-ce")) data[16 + 62] = '1';
        if(i == 2 && !strcmp(test.fault, "bad-flags")) data[16 + 60] = 'G';
        if(i == 2 && !strcmp(test.fault, "fragment-limit")) {
            size_t prior = size;
            size_t minimum = (KUI_RETAIL_IMAGE_SLOTS + 2u) * (size_t)fs.csize * 512u;
            size = ((minimum + 2351u) / 2352u) * 2352u;
            if(size < prior) size = prior;
            data = realloc(data, size); assert(data);
            memset(data + prior, 0, size - prior);
        }
        if(i == 2 && (!strcmp(test.fault, "fragmented") || !strcmp(test.fault, "fragment-limit"))) {
            FIL target; assert(f_open(&target, path, FA_WRITE | FA_CREATE_NEW) == FR_OK);
            size_t chunk = (size_t)fs.csize * 512;
            uint8_t *blocker = malloc(chunk); assert(blocker); memset(blocker, 0xd7, chunk);
            for(size_t offset = 0; offset < size; ) {
                size_t count = size - offset; if(count > chunk) count = chunk;
                UINT written;
                assert(f_write(&target, data + offset, (UINT)count, &written) == FR_OK && written == count);
                assert(f_sync(&target) == FR_OK);
                char block_path[96];
                snprintf(block_path, sizeof(block_path), "0:/Games/block-%04u.dat", (unsigned)(offset / chunk));
                if(offset / chunk < 16u || !strcmp(test.fault, "fragment-limit"))
                    write_file(block_path, blocker, chunk);
                offset += count;
            }
            assert(f_close(&target) == FR_OK); free(blocker);
        } else write_file(path, data, size);
        free(data);
    }
}
static int detached_block(void *ctx, uint32_t lba, uint8_t out[512]) {
    (void)ctx; assert(!test.connected && !test.files); ++test.physical_reads;
    return read_image(NULL, lba, 1, out);
}
static void cursor_write(void *context, uint32_t offset, const uint8_t *data, uint32_t bytes) {
    assert(offset <= 64u * KUI_GAME_SUBCHANNEL_BYTES && bytes && bytes <= 512u &&
        bytes <= 64u * KUI_GAME_SUBCHANNEL_BYTES - offset);
    memcpy((uint8_t *)context + offset, data, bytes);
}
static void detached_cursor_read(const struct kui_retail_manifest *map, uint32_t lba,
    uint32_t count, enum kui_game_sector_format format, void *out) {
    struct kui_retail_cursor cursor;
    assert(kui_retail_cursor_begin(&cursor, map, lba, count, format, cursor_write, out) == KUI_GAME_OK);
    uint8_t block[512]; unsigned fed = 0;
    while(cursor.done < count) {
        bool bounded = false;
        for(unsigned i = 0; i < map->extent_count; ++i) {
            const struct kui_retail_extent *extent = &map->slots[map->track_count + i].extent;
            if(cursor.block >= extent->card_lba && cursor.block - extent->card_lba < extent->blocks &&
                cursor.run && cursor.run <= extent->blocks - (cursor.block - extent->card_lba))
                bounded = true;
        }
        assert(bounded && ++fed <= 64u * 6u);
        assert(!detached_block(NULL, cursor.block, block));
        assert(kui_retail_cursor_feed(&cursor, block) == KUI_GAME_OK);
    }
    assert(cursor.done == count);
}
static void check_format_mapping(const char *directory, const struct format_expect *expected,
    const struct kui_runtime_image *image) {
    struct kui_retail_manifest *map = malloc(sizeof(*map)); assert(map);
    assert(image->data && image->info.payload_bytes >= 0x2004);
    assert(kui_retail_manifest_decode((const uint8_t *)image->data + 0x1000, map) == KUI_GAME_OK);
    assert(map->track_count == expected->count && map->session_lba == expected->session);
    if(strstr(test.fault, "-fragmented")) assert(map->extent_count > expected->count);
    assert(map->boot_lba == expected->boot_lba && map->boot_bytes == expected->boot_bytes);
    assert(map->flags == expected->flags && map->reader == (strstr(test.fault, "-async") ?
        KUI_RETAIL_READER_ASYNC : KUI_RETAIL_READER_STANDARD));
    assert(map->storage_transport == kui_storage_active());
    assert(map->partition_end <= map->card_sectors && map->card_sectors <= test.blocks);
    assert(map->partition_start == 0 || map->partition_start == 2048);
    assert(map->partition_end - map->partition_start == 96u * 1024u * 1024u / 512u);
    size_t selected_bytes;
    uint8_t *selected_data = host_file(directory, expected->selected, &selected_bytes);
    if(selected_bytes > KUI_GAME_GDI_LIMIT) selected_bytes = KUI_GAME_GDI_LIMIT;
    assert(map->gdi_crc32 == kui_retail_crc32(0, selected_data, selected_bytes));
    free(selected_data);
    struct kui_retail_image reader;
    assert(kui_retail_image_init(&reader, map, detached_block, NULL) == KUI_GAME_OK);
    uint8_t *actual = malloc(64u * KUI_GAME_SUBCHANNEL_BYTES); assert(actual);
    uint32_t expected_boot_crc = 0, expected_ip_crc = 0;
    bool boot_checked = false, ip_checked = false;
    for(unsigned i = 0; i < expected->count; ++i) {
        const struct format_track *e = &expected->track[i];
        const struct kui_retail_track *t = &map->slots[i].track;
        char name[64]; size_t bytes;
        snprintf(name, sizeof(name), "expected-track-%02u.bin", i + 1);
        uint8_t *source = host_file(directory, name, &bytes);
        assert(bytes == (size_t)e->sectors * e->stride);
        assert(t->start_lba == e->lba && t->end_lba == e->lba + e->sectors);
        assert(kui_retail_track_control(t) == e->control && kui_retail_track_sector_bytes(t) == e->stride);
        assert(kui_retail_track_header_bytes(t) == e->header);
        assert(kui_retail_track_file_offset(t) == e->offset % 512u);
        assert((bool)(t->control & KUI_RETAIL_TRACK_MODE2) == (e->mode == 2));
        uint32_t mapped_blocks = 0, next_block = 0;
        assert(t->extent_count);
        for(unsigned n = 0; n < t->extent_count; ++n) {
            const struct kui_retail_extent *extent = &map->slots[kui_retail_track_first_extent(t) + n].extent;
            assert(extent->file_block == next_block && extent->blocks);
            next_block += extent->blocks; mapped_blocks += extent->blocks;
        }
        assert(mapped_blocks == (e->offset % 512u + bytes + 511u) / 512u);
        if(e->stride == 2352u || e->stride == 2448u) {
            assert(kui_retail_image_read(&reader, e->lba, e->sectors, KUI_GAME_SECTOR_RAW,
                actual, 64u * KUI_GAME_SUBCHANNEL_BYTES) == KUI_GAME_OK);
            for(uint32_t n = 0; n < e->sectors; ++n)
                assert(!memcmp(actual + n * 2352u, source + n * e->stride, 2352u));
            detached_cursor_read(map, e->lba, e->sectors, KUI_GAME_SECTOR_RAW, actual);
            for(uint32_t n = 0; n < e->sectors; ++n)
                assert(!memcmp(actual + n * 2352u, source + n * e->stride, 2352u));
        } else {
            unsigned reads = test.physical_reads;
            memset(actual, 0x73, 64u * KUI_GAME_SUBCHANNEL_BYTES);
            assert(kui_retail_image_read(&reader, e->lba, 1, KUI_GAME_SECTOR_RAW,
                actual, 2352) == KUI_GAME_UNSUPPORTED);
            assert(test.physical_reads == reads && actual[0] == 0x73);
        }
        if(e->control == 4) {
            assert(kui_retail_image_read(&reader, e->lba, e->sectors, KUI_GAME_SECTOR_MODE1,
                actual, 64u * KUI_GAME_SUBCHANNEL_BYTES) == KUI_GAME_OK);
            for(uint32_t n = 0; n < e->sectors; ++n)
                assert(!memcmp(actual + n * 2048u, source + n * e->stride + e->header, 2048));
            detached_cursor_read(map, e->lba, e->sectors, KUI_GAME_SECTOR_MODE1, actual);
            for(uint32_t n = 0; n < e->sectors; ++n)
                assert(!memcmp(actual + n * 2048u, source + n * e->stride + e->header, 2048));
            if(e->lba == expected->session) {
                for(uint32_t n = 0; n < 16; ++n)
                    expected_ip_crc = kui_retail_crc32(expected_ip_crc, source + n * e->stride + e->header, 2048);
                assert(kui_retail_image_read(&reader, e->lba, 16, KUI_GAME_SECTOR_MODE1,
                    actual, 32768) == KUI_GAME_OK);
                assert(kui_retail_crc32(0, actual, 32768) == expected_ip_crc);
                ip_checked = true;
            }
            if(expected->boot_lba >= e->lba && expected->boot_lba < e->lba + e->sectors) {
                for(uint32_t done = 0; done < expected->boot_bytes; ) {
                    uint32_t n = done / 2048u, take = expected->boot_bytes - done;
                    if(take > 2048u) take = 2048u;
                    const uint8_t *wanted = source + (expected->boot_lba - e->lba + n) * e->stride + e->header;
                    assert(kui_retail_image_read(&reader, expected->boot_lba + n, 1,
                        KUI_GAME_SECTOR_MODE1, actual, 2048) == KUI_GAME_OK);
                    assert(!memcmp(actual, wanted, take));
                    expected_boot_crc = kui_retail_crc32(expected_boot_crc, wanted, take);
                    done += take;
                }
                boot_checked = true;
            }
        } else {
            unsigned reads = test.physical_reads;
            assert(kui_retail_image_read(&reader, e->lba, 1, KUI_GAME_SECTOR_MODE1,
                actual, 2048) == KUI_GAME_AUDIO);
            assert(test.physical_reads == reads);
        }
        free(source);
    }
    assert(ip_checked && boot_checked && test.physical_reads);
    assert(map->ip_crc32 == expected_ip_crc && map->boot_crc32 ==
        ((expected->flags & KUI_RETAIL_IMAGE_BOOT_CRC) ? expected_boot_crc : 0));
    unsigned reads = test.physical_reads;
    assert(kui_retail_image_read(&reader, map->slots[expected->count - 1].track.end_lba,
        1, KUI_GAME_SECTOR_MODE1, actual, 2048) == KUI_GAME_RANGE);
    assert(test.physical_reads == reads);
    printf("Detached generic format PASS: full IP CRC, exact boot bytes and headers, normalized selected extents; boot CRC %08x, %u SD blocks\n",
        expected_boot_crc, test.physical_reads);
    free(actual); free(map);
}
static void check_mapping(const char *directory, const struct kui_runtime_image *image) {
    struct kui_retail_manifest *map = malloc(sizeof(*map)); assert(map);
    assert(image->data && image->info.payload_bytes >= 0x2004);
    assert(kui_retail_manifest_decode((const uint8_t *)image->data + 0x1000, map) == KUI_GAME_OK);
    assert(map->card_sectors <= test.blocks && map->partition_end <= map->card_sectors);
    assert(map->partition_start == 0 || map->partition_start == 2048);
    assert(map->partition_end - map->partition_start == 96u * 1024u * 1024u / 512u);
    assert(map->track_count == track_count() &&
           map->extent_count >= (audio_unmapped() ? 2u : track_count()));
    assert(map->session_lba == 45000 && map->boot_lba == 45021 &&
           map->boot_bytes == (!strcmp(test.fault, "boot-tail") || !strcmp(test.fault, "cooked-boot-tail") ? 3001u : 4096u));
    assert(!strcmp(map->title, !strcmp(test.fault, "other-title") ? "Independent Native Game" :
        !strcmp(test.fault, "blank-title") ? "Untitled game" : "DEAD OR ALIVE 2"));
    assert(!strcmp(map->bootfile, !strcmp(test.fault, "alternate-bootfile") ? "ALT_BOOT.BIN" : "1ST_READ.BIN"));
    if(!strcmp(test.fault, "fragmented")) assert(map->slots[2].track.extent_count > 1);
    /* The background reader is granted only on SCI; SCIF falls back. The
     * Windows CE boot test's comes from its own package. */
    assert(map->reader == (!strcmp(test.fault, "async-on-sci") || !strcmp(test.fault, "async-cooked-2048") ||
                           !strncmp(test.fault, "async-tracks", 12) ||
                           !strcmp(test.fault, "ce-probe-async") ?
                           KUI_RETAIL_READER_ASYNC : KUI_RETAIL_READER_STANDARD));
    assert(map->storage_transport == kui_storage_active());
    size_t size; uint8_t *gdi = host_file(directory, "disc.gdi", &size);
    assert(map->gdi_crc32 == kui_crc32(0, gdi, size)); free(gdi);
    struct kui_retail_image reader;
    assert(kui_retail_image_init(&reader, map, detached_block, NULL) == KUI_GAME_OK);
    uint8_t *expected[KUI_RETAIL_IMAGE_TRACKS] = {0}; size_t sizes[KUI_RETAIL_IMAGE_TRACKS];
    uint8_t *actual = malloc(KUI_GAME_RAW_BYTES * 64u); assert(actual);
    for(unsigned i = 0; i < track_count(); ++i) {
        char generated[32];
        const struct kui_retail_track *t = &map->slots[i].track;
        expected[i] = host_file(directory, track_name(i, generated), &sizes[i]);
        uint32_t stride = kui_retail_track_sector_bytes(t);
        assert(stride == track_stride(i) && sizes[i] == (size_t)track_sectors(i) * stride);
        assert(t->start_lba == track_start(i) && t->end_lba == track_start(i) + track_sectors(i));
        assert(kui_retail_track_control(t) == (i == 0 || i == 2 ? 4u : 0u));
        assert((bool)(t->control & KUI_RETAIL_TRACK_COOKED) == cooked_track(i));
        if(kui_retail_track_control(t) == 0 && audio_unmapped()) {
            /* Listed without its file: refused before any card read. */
            unsigned before = test.physical_reads;
            assert(!t->extent_count && kui_retail_image_read(&reader, t->start_lba, 1,
                KUI_GAME_SECTOR_RAW, actual, KUI_GAME_RAW_BYTES) == KUI_GAME_AUDIO);
            assert(test.physical_reads == before);
            continue;
        }
        assert(t->extent_count);
        if(cooked_track(i)) {
            unsigned before = test.physical_reads;
            memset(actual, 0x77, KUI_GAME_RAW_BYTES * 64u);
            assert(kui_retail_image_read(&reader, track_start(i), track_sectors(i), KUI_GAME_SECTOR_RAW,
                actual, KUI_GAME_RAW_BYTES * 64u) == KUI_GAME_UNSUPPORTED);
            assert(test.physical_reads == before);
            for(size_t j = 0; j < KUI_GAME_RAW_BYTES * 64u; ++j) assert(actual[j] == 0x77);
        } else {
            assert(kui_retail_image_read(&reader, track_start(i), track_sectors(i), KUI_GAME_SECTOR_RAW,
                actual, sizes[i]) == KUI_GAME_OK && !memcmp(actual, expected[i], sizes[i]));
        }
        if(i == 0 || i == 2) {
            assert(kui_retail_image_read(&reader, starts[i], counts[i], KUI_GAME_SECTOR_MODE1,
                actual, (size_t)counts[i] * 2048u) == KUI_GAME_OK);
            for(uint32_t n = 0; n < counts[i]; ++n)
                assert(!memcmp(actual + n * 2048u, expected[i] + n * stride + kui_retail_track_header_bytes(t), 2048));
        }
    }
    /* Raw preparation skips the executable and preserves zero boot CRC.
     * Cooked preparation provides its exact expected logical-byte CRC. */
    const struct kui_retail_track *boot_track = &map->slots[2].track;
    uint32_t boot_stride = kui_retail_track_sector_bytes(boot_track);
    uint32_t boot_header = kui_retail_track_header_bytes(boot_track), boot_crc = 0;
    for(uint32_t done = 0; done < map->boot_bytes; ) {
        uint32_t n = done / 2048u, bytes = map->boot_bytes - done;
        if(bytes > 2048u) bytes = 2048u;
        assert(kui_retail_image_read(&reader, map->boot_lba + n, 1,
            KUI_GAME_SECTOR_MODE1, actual, 2048) == KUI_GAME_OK);
        const uint8_t *source = expected[2] + (21u + n) * boot_stride + boot_header;
        assert(!memcmp(actual, source, bytes));
        boot_crc = kui_retail_crc32(boot_crc, source, bytes);
        if(!cooked_track(2)) {
            assert(kui_retail_image_read(&reader, map->boot_lba + n, 1,
                KUI_GAME_SECTOR_RAW, actual, 2352) == KUI_GAME_OK);
            assert(kui_retail_sector_header(actual, map->boot_lba + n) == KUI_RETAIL_HEADER_OK);
            assert(kui_retail_sector_header(actual, map->boot_lba + n + 1u) == KUI_RETAIL_HEADER_ADDRESS);
        }
        done += bytes;
    }
    assert(map->boot_crc32 == (cooked_track(2) ? boot_crc : 0));
    if(cooked_track(2))
        assert(boot_crc == (!strcmp(test.fault, "cooked-boot-tail") ? 0x4a3efbf8u : 0xbaf6ad9cu));
    assert(kui_retail_image_read(&reader, map->session_lba, 16,
        KUI_GAME_SECTOR_MODE1, actual, 32768) == KUI_GAME_OK);
    uint32_t ip_crc = kui_retail_crc32(0, actual, 32768), expected_ip_crc = 0;
    for(uint32_t n = 0; n < 16; ++n)
        expected_ip_crc = kui_crc32(expected_ip_crc, expected[2] + n * boot_stride + boot_header, 2048);
    assert(ip_crc == map->ip_crc32 && expected_ip_crc == map->ip_crc32);
    /* Adjacent data/audio boundary, hole and cooked audio rejection. */
    if(cooked_track(0)) {
        unsigned before = test.physical_reads;
        assert(kui_retail_image_read(&reader, 3, 2, KUI_GAME_SECTOR_RAW, actual, 4704) == KUI_GAME_UNSUPPORTED);
        assert(test.physical_reads == before);
    } else if(audio_unmapped()) {
        assert(kui_retail_image_read(&reader, 3, 2, KUI_GAME_SECTOR_RAW, actual, 4704) == KUI_GAME_AUDIO);
    } else {
        assert(kui_retail_image_read(&reader, 3, 2, KUI_GAME_SECTOR_RAW, actual, 4704) == KUI_GAME_OK);
        assert(!memcmp(actual, expected[0] + 3u * 2352u, 2352) && !memcmp(actual + 2352, expected[1], 2352));
    }
    unsigned reads = test.physical_reads;
    assert(kui_retail_image_read(&reader, 7, 2, KUI_GAME_SECTOR_RAW, actual, 4704) ==
           (audio_unmapped() ? KUI_GAME_AUDIO : KUI_GAME_GAP));
    assert(kui_retail_image_read(&reader, 4, 1, KUI_GAME_SECTOR_MODE1, actual, 2048) == KUI_GAME_AUDIO);
    assert(test.physical_reads == reads && reads > 0);
    for(unsigned i = 0; i < track_count(); ++i) free(expected[i]);
    free(actual); free(map);
    printf("Detached selected-image read PASS: all raw tracks, cooked data, full IP CRC, exact boot bytes and headers; %u SD blocks\n", reads);
    if(cooked_track(2)) printf("Cooked data RAW requests refused before IO; exact boot CRC %08x\n", boot_crc);
}
static void check(const char *directory) {
    struct kui_runtime_image image = {0};
    if(format_case()) {
        struct format_expect expected = format_expect(directory);
        char path[KUI_GAMES_FILE_CAP];
        assert(snprintf(path, sizeof(path), "/Games/Reader Test/%s", expected.selected) < (int)sizeof(path));
        uint32_t request = strstr(test.fault, "-async") ? KUI_RETAIL_READER_ASYNC : KUI_RETAIL_READER_STANDARD;
        if(strstr(test.fault, "force-scramble")) request |= KUI_GAMES_RETAIL_DESCRAMBLE;
        if(strstr(test.fault, "force-plain")) request |= KUI_GAMES_RETAIL_BOOT_PLAIN;
        if(strstr(test.fault, "bad-encoding"))
            request |= KUI_GAMES_RETAIL_DESCRAMBLE | KUI_GAMES_RETAIL_BOOT_PLAIN;
        if(strstr(test.fault, "bad-ce-scramble")) request |= KUI_GAMES_RETAIL_CE_PROBE | KUI_GAMES_RETAIL_DESCRAMBLE;
        if(strstr(test.fault, "bad-ce-plain")) request |= KUI_GAMES_RETAIL_CE_PROBE | KUI_GAMES_RETAIL_BOOT_PLAIN;
        bool result = kui_games_retail_prepare_reader(path, request, &image, log_line, cancel);
        assert(result == (bool)expected.valid);
        if(result) check_format_mapping(directory, &expected, &image);
        else assert(!image.data && !image.info.payload_bytes && !image.info.memory_bytes);
        if(strstr(test.fault, "bad-encoding") || strstr(test.fault, "bad-ce-"))
            assert(!test.connects && !test.read_calls && !test.physical_reads);
        kui_runtime_free(&image);
        return;
    }
    bool result = !strncmp(test.fault, "ce-probe", 8) ?
        kui_games_retail_prepare_reader(selected, KUI_GAMES_RETAIL_CE_PROBE |
            (!strcmp(test.fault, "ce-probe-async") ? KUI_RETAIL_READER_ASYNC : KUI_RETAIL_READER_STANDARD),
            &image, log_line, cancel) :
        strncmp(test.fault, "async-", 6) ? kui_games_retail_prepare(selected, &image, log_line, cancel) :
        kui_games_retail_prepare_reader(selected, KUI_RETAIL_READER_ASYNC, &image, log_line, cancel);
    bool valid = !strncmp(test.fault, "async-", 6) || !strcmp(test.fault, "valid") || !strcmp(test.fault, "fragmented") ||
        !strcmp(test.fault, "ce-probe") || !strcmp(test.fault, "ce-probe-async") || !strcmp(test.fault, "ce-probe-cooked") ||
        !strcmp(test.fault, "cooked-2048") || !strcmp(test.fault, "cooked-boot-tail") ||
        !strcmp(test.fault, "boot-tail") || !strcmp(test.fault, "other-title") ||
        !strcmp(test.fault, "alternate-bootfile") || !strcmp(test.fault, "cdda-warning") ||
        !strcmp(test.fault, "blank-title") || !strncmp(test.fault, "tracks-", 7);
    assert(result == valid);
    assert(test.boot_read == (valid && cooked_track(2)));
    if(valid) check_mapping(directory, &image);
    else assert(!image.data && !image.info.payload_bytes && !image.info.memory_bytes);
    kui_runtime_free(&image);
    if(!strcmp(test.fault, "cancel-before")) assert(!test.read_calls && !test.connects);
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if(argc != 5) return 2;
    struct stat st; if(lstat(argv[1], &st) || !S_ISREG(st.st_mode) || st.st_size % 512) return 2;
    test.image = fopen(argv[1], "r+b"); assert(test.image);
    test.blocks = (uint64_t)st.st_size / 512; test.fault = argv[4];
    if(!strcmp(argv[3], "seed")) {
        kui_media_set(&media); assert(kui_mount(&fs, log_line)); seed(argv[2]);
        assert(f_mount(NULL, "0:", 0) == FR_OK); kui_media_set(NULL);
    } else {
        assert(!strcmp(argv[3], "check")); test.active = true; check(argv[2]);
        assert(!test.writes && !test.files && !test.connected && test.connects == test.disconnects);
        if(strstr(test.fault, "-fail") || !strncmp(test.fault, "sector-", 7) ||
            !strcmp(test.fault, "cancel-map") || !strcmp(test.fault, "cancel-ip") || !strcmp(test.fault, "size-change")) assert(test.injected);
    }
    assert(!fclose(test.image));
    printf("PASS retail preparation %s %s; no active-operation writes\n", argv[3], test.fault);
    return 0;
}
