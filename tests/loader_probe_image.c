/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/games_probe.h"
#include "kui/loader_probe.h"
#include "kui/media.h"
#include "kui/runtime.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Exercise the production adapter on real FatFs. The final check deliberately
 * bypasses FatFs and the media abstraction: it rereads physical card sectors
 * from the detached FILE according to the exported manifest. */
static struct {
    FILE *image;
    uint64_t blocks, fixture_read;
    const char *fault;
    FIL *fixture;
    unsigned writes, connects, disconnects, files, read_calls;
    uint32_t first_sector;
    bool active, connected, injected, cancelled;
} test;
static FATFS fs;
static const char *const payload_path = "0:/KUI/apps/games/probe.kui";
static const char *const fixture_path = "0:/KUI/apps/games/probe.dat";
static bool fault(const char *name) { return test.active && !strcmp(test.fault, name); }
static void log_line(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
static uint64_t blocks(void *ctx) { (void)ctx; return test.blocks; }
static int read_image(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    (void)ctx;
    if(fault("mount-fail")) { test.injected = true; return -1; }
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
    if(fault("connect-fail")) { test.injected = true; return false; }
    test.connected = true; kui_media_set(&media); return true;
}
void kui_sd_disconnect(void) {
    assert(test.connected && !test.files);
    test.connected = false; ++test.disconnects; kui_media_set(NULL);
}
static bool cancel(void) { return test.cancelled || fault("cancel-before"); }

FRESULT __real_f_open(FIL *file, const TCHAR *path, BYTE flags);
FRESULT __wrap_f_open(FIL *file, const TCHAR *path, BYTE flags) {
    bool fixture = !strcmp(path, fixture_path);
    if(test.active) {
        assert(!(flags & (FA_WRITE | FA_CREATE_NEW | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_OPEN_APPEND)));
        if(fault(fixture ? "fixture-open-fail" : "payload-open-fail")) {
            test.injected = true; return FR_DISK_ERR;
        }
    }
    FRESULT result = __real_f_open(file, path, flags);
    if(test.active && result == FR_OK) {
        ++test.files;
        if(fixture) test.fixture = file;
    }
    return result;
}
FRESULT __real_f_read(FIL *file, void *buffer, UINT bytes, UINT *read);
FRESULT __wrap_f_read(FIL *file, void *buffer, UINT bytes, UINT *read) {
    FRESULT result = __real_f_read(file, buffer, bytes, read);
    if(test.active) {
        bool fixture = file == test.fixture;
        ++test.read_calls;
        if(fixture) {
            test.fixture_read += *read;
            if(fault("sector-beforedata")) { file->sect = 0; test.injected = true; }
            if(fault("sector-aftercard")) { file->sect = (LBA_t)test.blocks; test.injected = true; }
            if(fault("sector-repeat")) {
                if(!test.first_sector) test.first_sector = (uint32_t)file->sect;
                file->sect = test.first_sector; test.injected = true;
            }
        }
        if(fault(fixture ? "fixture-read-fail" : "payload-read-fail")) {
            test.injected = true; return FR_DISK_ERR;
        }
        if(fault(fixture ? "fixture-short-read" : "payload-short-read") && *read) {
            --*read; test.injected = true;
        }
        if(fault(fixture ? "cancel-fixture" : "cancel-payload")) {
            test.cancelled = true; test.injected = true;
        }
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
    bool fixture = file == test.fixture;
    FRESULT result = __real_f_close(file);
    if(test.active) {
        assert(test.files); --test.files;
        if(fixture) test.fixture = NULL;
        if(fault(fixture ? "fixture-close-fail" : "payload-close-fail")) {
            test.injected = true; return FR_DISK_ERR;
        }
        if(fixture && fault("cancel-close")) {
            test.cancelled = true; test.injected = true;
        }
    }
    return result;
}
FRESULT __real_f_mount(FATFS *volume, const TCHAR *path, BYTE option);
FRESULT __wrap_f_mount(FATFS *volume, const TCHAR *path, BYTE option) {
    FRESULT result = __real_f_mount(volume, path, option);
    if(!volume && fault("unmount-fail")) { test.injected = true; return FR_DISK_ERR; }
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
static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
static void seed(const char *directory) {
    assert(f_mkdir("0:/KUI") == FR_OK);
    assert(f_mkdir("0:/KUI/apps") == FR_OK);
    assert(f_mkdir("0:/KUI/apps/games") == FR_OK);
    size_t payload_size, fixture_size;
    uint8_t *payload = host_file(directory, "probe.kui", &payload_size);
    uint8_t *fixture = host_file(directory, "probe.dat", &fixture_size);
    assert(fixture_size == KUI_LOADER_PROBE_FILE_BYTES);
    bool layout_change = true;
    uint8_t *header = payload + KUI_RUNTIME_HEADER_BYTES + 0x100;
    if(!strcmp(test.fault, "layout-magic")) header[0] ^= 1;
    else if(!strcmp(test.fault, "layout-manifest")) put32(header + 16, 0x1ff0);
    else if(!strcmp(test.fault, "layout-entry")) put32(header + 32, 0x8ce00004);
    else if(!strcmp(test.fault, "layout-overrun")) put32(header + 28, 0x100004);
    else if(!strcmp(test.fault, "layout-reserved")) put32(header + 60, 1);
    else if(!strcmp(test.fault, "manifest-not-empty")) payload[KUI_RUNTIME_HEADER_BYTES + 0x1000] = 1;
    else if(!strcmp(test.fault, "payload-memory")) put32(payload + 28, (uint32_t)(payload_size - KUI_RUNTIME_HEADER_BYTES + 4));
    else layout_change = false;
    if(layout_change) {
        put32(payload + 32, kui_crc32(0, payload + KUI_RUNTIME_HEADER_BYTES, payload_size - KUI_RUNTIME_HEADER_BYTES));
        put32(payload + 60, kui_crc32(0, payload, 60));
    }
    if(!strcmp(test.fault, "payload-checksum")) payload[payload_size - 1] ^= 1;
    if(!strcmp(test.fault, "payload-truncated")) --payload_size;
    if(strcmp(test.fault, "missing-payload")) write_file(payload_path, payload, payload_size);
    if(!strcmp(test.fault, "fixture-corrupt")) fixture[fixture_size / 2 + 31] ^= 1;
    if(!strcmp(test.fault, "fixture-short")) --fixture_size;
    if(!strcmp(test.fault, "fragmented")) {
        /* Alternate fixture clusters with live blocker files. This forces both
         * FAT32 and exFAT to encode a non-contiguous file; no mock cluster map. */
        FIL target; assert(f_open(&target, fixture_path, FA_WRITE | FA_CREATE_NEW) == FR_OK);
        size_t chunk = (size_t)fs.csize * 512;
        uint8_t *blocker = malloc(chunk); assert(blocker); memset(blocker, 0xd7, chunk);
        for(size_t offset = 0; offset < fixture_size; ) {
            size_t count = fixture_size - offset; if(count > chunk) count = chunk;
            UINT written; assert(f_write(&target, fixture + offset, (UINT)count, &written) == FR_OK && written == count);
            assert(f_sync(&target) == FR_OK);
            char path[96]; snprintf(path, sizeof(path), "0:/KUI/apps/games/block-%03u.dat", (unsigned)(offset / chunk));
            write_file(path, blocker, chunk); offset += count;
        }
        assert(f_close(&target) == FR_OK); free(blocker);
    } else if(strcmp(test.fault, "missing-fixture")) {
        write_file(fixture_path, fixture, fixture_size);
        if(!strcmp(test.fault, "fixture-long")) {
            FIL file; UINT written; const uint8_t extra = 0;
            assert(f_open(&file, fixture_path, FA_WRITE | FA_OPEN_APPEND) == FR_OK);
            assert(f_write(&file, &extra, 1, &written) == FR_OK && written == 1);
            assert(f_close(&file) == FR_OK);
        }
    }
    free(fixture); free(payload);
}
static void check_mapping(const char *directory, const struct kui_runtime_image *image) {
    struct kui_loader_probe_manifest manifest;
    assert(image->data && image->info.payload_bytes >= 0x1000 + KUI_LOADER_PROBE_MANIFEST_BYTES);
    assert(kui_loader_probe_manifest_decode((const uint8_t *)image->data + 0x1000, &manifest) == KUI_LP_OK);
    assert(manifest.card_sectors <= test.blocks);
    assert(manifest.extent_count > 0);
    if(!strcmp(test.fault, "fragmented")) assert(manifest.extent_count > 1);
    assert(!test.connected && !test.files);
    size_t size; uint8_t *expected = host_file(directory, "probe.dat", &size);
    if(!strcmp(test.fault, "fixture-corrupt")) expected[size / 2 + 31] ^= 1;
    uint8_t *actual = calloc(1, size); assert(actual);
    uint32_t covered = 0;
    for(uint32_t i = 0; i < manifest.extent_count; ++i) {
        const struct kui_loader_probe_extent *extent = &manifest.extents[i];
        assert(extent->file_block == covered && extent->blocks > 0);
        assert(extent->card_lba < manifest.card_sectors && extent->blocks <= manifest.card_sectors - extent->card_lba);
        assert((uint64_t)(covered + extent->blocks) * 512 <= size);
        /* Physical sectors, no FatFs, no mapper, no cached data, no connected SD. */
        assert(!fseeko(test.image, (off_t)extent->card_lba * 512, SEEK_SET));
        assert(fread(actual + (size_t)covered * 512, 512, extent->blocks, test.image) == extent->blocks);
        covered += extent->blocks;
    }
    assert((uint64_t)covered * 512 == size && !memcmp(actual, expected, size));
    free(actual); free(expected);
    printf("Physical manifest PASS: %u blocks, %u extents\n", covered, manifest.extent_count);
}
static void check(const char *directory) {
    struct kui_runtime_image image = {0};
    bool result = kui_games_probe_prepare(&image, log_line, cancel);
    bool valid = !strcmp(test.fault, "valid") || !strcmp(test.fault, "fragmented") ||
        !strcmp(test.fault, "fixture-corrupt");
    assert(result == valid);
    if(valid) check_mapping(directory, &image);
    else assert(!image.data && !image.info.payload_bytes && !image.info.memory_bytes);
    kui_runtime_free(&image);
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
        assert(!strcmp(argv[3], "check")); test.active = true; check(argv[2]);
        assert(!test.writes && !test.files && !test.connected);
        assert(test.connects == test.disconnects ||
            (!strcmp(test.fault, "connect-fail") && test.connects == test.disconnects + 1));
        if(strstr(test.fault, "-fail") || strstr(test.fault, "-short-read") ||
           !strcmp(test.fault, "cancel-fixture") || !strcmp(test.fault, "cancel-payload") ||
           !strcmp(test.fault, "cancel-close") || !strncmp(test.fault, "sector-", 7)) assert(test.injected);
    }
    assert(!fclose(test.image));
    printf("PASS loader probe %s %s; no active-operation writes\n", argv[3], test.fault);
    return 0;
}
