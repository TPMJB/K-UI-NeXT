/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/games_retail.h"
#include "kui/media.h"
#include "kui/retail_image.h"
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
static const char *const names[] = {"track01.bin", "music track02.raw", "track03.bin", "music track04.raw"};
static const uint32_t starts[] = {0, 4, 45000, 45064};
static const uint32_t counts[] = {4, 4, 64, 4};
static unsigned track_count(void) {
    return !strcmp(test.fault, "track-limit") ? 17u : !strcmp(test.fault, "cdda-warning") ? 4u : 3u;
}
static bool fault(const char *name) { return test.active && !strcmp(test.fault, name); }
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
            /* The boot executable (track sectors 21 and 22) is read only by
             * the stage after handoff, never during preparation. */
            FSIZE_t end = f_tell(file), start = end - *got;
            if(strstr(test.track_name, "track03") && start < 23u * 2352u && end > 21u * 2352u)
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
    write_file(package, data, size); free(data);
    data = host_file(directory, "disc.gdi", &size);
    write_file("0:/Games/Reader Test/disc.gdi", data, size); free(data);
    for(unsigned i = 0; i < track_count(); ++i) {
        if(i == 1 && !strcmp(test.fault, "missing-track")) continue;
        char path[256], generated[32];
        snprintf(generated, sizeof(generated), "track%02u.raw", i + 1);
        const char *name = i < 4 ? names[i] : generated;
        snprintf(path, sizeof(path), "0:/Games/Reader Test/%s", name);
        data = host_file(directory, name, &size);
        if(i == 2 && !strcmp(test.fault, "bad-ip")) data[16] ^= 1;
        if(i == 2 && !strcmp(test.fault, "bad-bootfile")) data[16 + 96] = 'X';
        if(i == 2 && !strcmp(test.fault, "bad-media")) data[16 + 37] = 'C';
        if(i == 2 && !strcmp(test.fault, "windows-ce")) data[16 + 62] = '1';
        if(i == 2 && !strcmp(test.fault, "bad-flags")) data[16 + 60] = 'G';
        if(i == 2 && !strcmp(test.fault, "fragment-limit")) {
            size_t prior = size;
            size_t minimum = (KUI_RETAIL_IMAGE_EXTENTS + 2u) * (size_t)fs.csize * 512u;
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
static void check_mapping(const char *directory, const struct kui_runtime_image *image) {
    struct kui_retail_manifest *map = malloc(sizeof(*map)); assert(map);
    assert(image->data && image->info.payload_bytes >= 0x2004);
    assert(kui_retail_manifest_decode((const uint8_t *)image->data + 0x1000, map) == KUI_GAME_OK);
    assert(map->card_sectors <= test.blocks && map->partition_end <= map->card_sectors);
    assert(map->partition_start == 0 || map->partition_start == 2048);
    assert(map->partition_end - map->partition_start == 96u * 1024u * 1024u / 512u);
    assert(map->track_count == track_count() && map->extent_count >= track_count());
    assert(map->session_lba == 45000 && map->boot_lba == 45021 && map->boot_bytes == (!strcmp(test.fault, "boot-tail") ? 3001u : 4096u));
    assert(!strcmp(map->title, !strcmp(test.fault, "other-title") ? "Independent Native Game" :
        !strcmp(test.fault, "blank-title") ? "Untitled game" : "DEAD OR ALIVE 2"));
    assert(!strcmp(map->bootfile, !strcmp(test.fault, "alternate-bootfile") ? "ALT_BOOT.BIN" : "1ST_READ.BIN"));
    if(!strcmp(test.fault, "fragmented")) assert(map->tracks[2].extent_count > 1);
    size_t size; uint8_t *gdi = host_file(directory, "disc.gdi", &size);
    assert(map->gdi_crc32 == kui_crc32(0, gdi, size)); free(gdi);
    struct kui_retail_image reader;
    assert(kui_retail_image_init(&reader, map, detached_block, NULL) == KUI_GAME_OK);
    uint8_t *expected[4]; size_t sizes[4];
    uint8_t *actual = malloc(KUI_GAME_RAW_BYTES * 64u); assert(actual);
    for(unsigned i = 0; i < track_count(); ++i) {
        expected[i] = host_file(directory, names[i], &sizes[i]);
        assert(sizes[i] == (size_t)counts[i] * KUI_GAME_RAW_BYTES);
        assert(map->tracks[i].number == i + 1 && map->tracks[i].start_lba == starts[i]);
        assert(map->tracks[i].end_lba == starts[i] + counts[i]);
        assert(kui_retail_image_read(&reader, starts[i], counts[i], KUI_GAME_SECTOR_RAW,
            actual, sizes[i]) == KUI_GAME_OK && !memcmp(actual, expected[i], sizes[i]));
        if(i == 0 || i == 2) {
            assert(kui_retail_image_read(&reader, starts[i], counts[i], KUI_GAME_SECTOR_MODE1,
                actual, (size_t)counts[i] * 2048u) == KUI_GAME_OK);
            for(uint32_t n = 0; n < counts[i]; ++n)
                assert(!memcmp(actual + n * 2048u, expected[i] + n * KUI_GAME_RAW_BYTES + 16, 2048));
        }
    }
    /* K-UI no longer reads the executable before launch; the stage checks
     * each boot sector's header as it loads. The detached reader must still
     * return the exact logical boot bytes, compared with the fixture itself. */
    assert(map->boot_crc32 == 0);
    for(uint32_t done = 0; done < map->boot_bytes; ) {
        uint32_t n = done / 2048u, bytes = map->boot_bytes - done;
        if(bytes > 2048u) bytes = 2048u;
        assert(kui_retail_image_read(&reader, map->boot_lba + n, 1,
            KUI_GAME_SECTOR_MODE1, actual, 2048) == KUI_GAME_OK);
        assert(!memcmp(actual, expected[2] + (21u + n) * 2352u + 16u, bytes));
        assert(kui_retail_image_read(&reader, map->boot_lba + n, 1,
            KUI_GAME_SECTOR_RAW, actual, 2352) == KUI_GAME_OK);
        assert(kui_retail_sector_header(actual, map->boot_lba + n) == KUI_RETAIL_HEADER_OK);
        assert(kui_retail_sector_header(actual, map->boot_lba + n + 1u) == KUI_RETAIL_HEADER_ADDRESS);
        done += bytes;
    }
    assert(kui_retail_image_read(&reader, map->session_lba, 16,
        KUI_GAME_SECTOR_MODE1, actual, 32768) == KUI_GAME_OK);
    uint32_t ip_crc = kui_retail_crc32(0, actual, 32768), expected_ip_crc = 0;
    for(uint32_t n = 0; n < 16; ++n)
        expected_ip_crc = kui_crc32(expected_ip_crc, expected[2] + n * 2352u + 16u, 2048);
    assert(ip_crc == map->ip_crc32 && expected_ip_crc == map->ip_crc32);
    /* Adjacent data/audio boundary, hole and cooked audio rejection. */
    assert(kui_retail_image_read(&reader, 3, 2, KUI_GAME_SECTOR_RAW, actual, 4704) == KUI_GAME_OK);
    assert(!memcmp(actual, expected[0] + 3u * 2352u, 2352) && !memcmp(actual + 2352, expected[1], 2352));
    unsigned reads = test.physical_reads;
    assert(kui_retail_image_read(&reader, 7, 2, KUI_GAME_SECTOR_RAW, actual, 4704) == KUI_GAME_GAP);
    assert(kui_retail_image_read(&reader, 4, 1, KUI_GAME_SECTOR_MODE1, actual, 2048) == KUI_GAME_AUDIO);
    assert(test.physical_reads == reads && reads > 0);
    for(unsigned i = 0; i < track_count(); ++i) free(expected[i]);
    free(actual); free(map);
    printf("Detached selected-image read PASS: all raw tracks, cooked data, full IP CRC, exact boot bytes and headers; %u SD blocks\n", reads);
}
static void check(const char *directory) {
    struct kui_runtime_image image = {0};
    bool result = kui_games_retail_prepare(selected, &image, log_line, cancel);
    bool valid = !strcmp(test.fault, "valid") || !strcmp(test.fault, "fragmented") ||
        !strcmp(test.fault, "boot-tail") || !strcmp(test.fault, "other-title") ||
        !strcmp(test.fault, "alternate-bootfile") || !strcmp(test.fault, "cdda-warning") ||
        !strcmp(test.fault, "blank-title");
    assert(result == valid);
    assert(!test.boot_read);
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
