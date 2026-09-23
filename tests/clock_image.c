/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/clock.h"
#include "kui/media.h"
#include "ff.h"
#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static FILE *image;
static uint64_t image_blocks;
static int64_t seconds;
static FATFS fs;
static unsigned warnings;
static uint64_t blocks(void *ctx) { (void)ctx; return image_blocks; }
static int read_image(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    (void)ctx;
    return fseeko(image, (off_t)block * 512, SEEK_SET) || fread(data, 512, count, image) != count ? -1 : 0;
}
static int write_image(void *ctx, uint32_t block, size_t count, const uint8_t *data) {
    (void)ctx;
    return fseeko(image, (off_t)block * 512, SEEK_SET) || fwrite(data, 512, count, image) != count ? -1 : 0;
}
static int sync_image(void *ctx) { (void)ctx; return fflush(image) || fsync(fileno(image)) ? -1 : 0; }
static bool read_clock(void *ctx, int64_t *out) { (void)ctx; *out = seconds; return true; }
static void log_clock(const char *format, ...) { (void)format; ++warnings; }
static const struct kui_media_ops media = {NULL, blocks, read_image, write_image, sync_image};
static void remount(void) {
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    kui_media_set(&media);
    assert(f_mount(&fs, "0:", 1) == FR_OK);
}
static void write_file(const char *path, BYTE flags) {
    FIL file; UINT done;
    assert(f_open(&file, path, flags | FA_WRITE) == FR_OK);
    assert(f_write(&file, "timestamp", 9, &done) == FR_OK && done == 9);
    assert(f_sync(&file) == FR_OK && f_close(&file) == FR_OK);
}
static void check(const char *path, uint32_t created, uint32_t modified) {
    FILINFO info;
    assert(f_stat(path, &info) == FR_OK);
    assert(info.crdate == (created >> 16) && info.crtime == (created & 0xffffu));
    assert(info.fdate == (modified >> 16) && info.ftime == (modified & 0xffffu));
}
int main(int argc, char **argv) {
    if(argc != 2) return 2;
    struct stat st;
    if(lstat(argv[1], &st) || !S_ISREG(st.st_mode) || st.st_size % 512) return 2;
    image = fopen(argv[1], "r+b"); if(!image) return 2;
    image_blocks = (uint64_t)st.st_size / 512;
    struct kui_datetime value = {2026,9,23,12,34,56};
    assert(kui_clock_to_seconds(&value, &seconds));
    kui_clock_configure(read_clock, NULL, log_clock);
    uint32_t created = 46u << 25 | 9u << 21 | 23u << 16 | 12u << 11 | 34u << 5 | 28u;
    remount();
    assert(f_mkdir("0:/clock") == FR_OK);
    write_file("0:/clock/time.bin", FA_CREATE_NEW);
    remount();
    check("0:/clock", created, created);
    check("0:/clock/time.bin", created, created);
    value = (struct kui_datetime){2026,10,1,1,2,4};
    assert(kui_clock_to_seconds(&value, &seconds));
    uint32_t modified = 46u << 25 | 10u << 21 | 1u << 16 | 1u << 11 | 2u << 5 | 2u;
    write_file("0:/clock/time.bin", FA_OPEN_APPEND);
    remount();
    check("0:/clock/time.bin", created, modified);
    /* A failed RTC is not fabricated as the build date; the fallback is both
     * representable and diagnosed, and old file creation dates stay intact. */
    seconds = INT64_MIN;
    write_file("0:/clock/invalid.bin", FA_CREATE_NEW);
    remount();
    check("0:/clock/invalid.bin", 0x00210000u, 0x00210000u);
    check("0:/clock/time.bin", created, modified);
    assert(warnings == 1);
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    assert(!fclose(image));
    puts("PASS timestamps: created, updated, preserved across remount, invalid RTC warning");
    return 0;
}
