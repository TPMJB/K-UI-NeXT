/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/media.h"
#include "kui/runtime.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static struct { FILE *file; uint64_t blocks; unsigned reads, writes, polls;
                bool fail_read, loading; const char *fault; } image;
static void log_line(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
static uint64_t blocks(void *unused) { (void)unused; return image.blocks; }
static int read_blocks(void *unused, uint32_t sector, size_t count, uint8_t *data) {
    (void)unused;
    if(image.loading && ++image.reads == 8 && image.fail_read) return -1;
    return fseeko(image.file, (off_t)sector * 512, SEEK_SET) ||
        fread(data, 512, count, image.file) != count ? -1 : 0;
}
static int write_blocks(void *unused, uint32_t sector, size_t count, const uint8_t *data) {
    (void)unused; ++image.writes;
    return fseeko(image.file, (off_t)sector * 512, SEEK_SET) ||
        fwrite(data, 512, count, image.file) != count ? -1 : 0;
}
static int sync_card(void *unused) { (void)unused; return fflush(image.file); }
static bool cancelled(void) {
    ++image.polls;
    return !strcmp(image.fault, "cancel-before") ||
           (!strcmp(image.fault, "cancel-during") && image.polls >= 4);
}
int main(int argc, char **argv) {
    if(argc != 4) return 2;
    struct stat st;
    if(lstat(argv[1], &st) || !S_ISREG(st.st_mode) || st.st_size % 512) return 2;
    image.file = fopen(argv[1], "r+b");
    if(!image.file) return 2;
    image.blocks = (uint64_t)st.st_size / 512;
    struct kui_media_ops ops = {NULL, blocks, read_blocks, write_blocks, sync_card};
    kui_media_set(&ops);
    FATFS fs;
    if(!kui_mount(&fs, log_line)) return 2;
    if(!strcmp(argv[2], "seed")) {
        FILE *source = fopen(argv[3], "rb");
        if(!source) return 2;
        FRESULT r = f_mkdir("0:/KUI");
        if(r != FR_OK && r != FR_EXIST) return 2;
        FIL target;
        if(f_open(&target, KUI_RUNTIME_PATH, FA_WRITE | FA_CREATE_NEW) != FR_OK) return 2;
        uint8_t buffer[32768];
        size_t n;
        while((n = fread(buffer, 1, sizeof(buffer), source))) {
            UINT done;
            if(f_write(&target, buffer, (UINT)n, &done) != FR_OK || done != n) return 2;
        }
        if(ferror(source) || fclose(source) || f_close(&target) != FR_OK) return 2;
        f_mount(NULL, "0:", 0);
        return fclose(image.file) ? 2 : 0;
    }
    if(strcmp(argv[2], "load")) return 2;
    image.loading = true;
    image.fault = argv[3];
    image.fail_read = !strcmp(image.fault, "read-fail");
    unsigned previous_writes = image.writes;
    struct kui_runtime_image runtime;
    enum kui_runtime_result result = kui_runtime_read(KUI_RUNTIME_PATH, &runtime, log_line, cancelled);
    if(result == KUI_RUNTIME_OK) {
        assert(runtime.data != NULL);
        assert(kui_crc32(0, runtime.data, runtime.info.payload_bytes) == runtime.info.crc32);
    } else {
        assert(runtime.data == NULL && runtime.info.payload_bytes == 0);
    }
    kui_runtime_free(&runtime);
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    assert(previous_writes == image.writes);
    fclose(image.file);
    printf("RESULT: %s\n", kui_runtime_result_name(result));
    return result == KUI_RUNTIME_OK ? 0 : 1;
}
