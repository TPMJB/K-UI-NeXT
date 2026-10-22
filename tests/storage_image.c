/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/media.h"
#include "kui/probe.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct image { FILE *file; uint64_t blocks; unsigned writes;
               unsigned fail_write; bool fail_sync, cancel, cancel_before; };
static struct image image;
static void log_line(const char *format, ...) {
    va_list args; va_start(args, format); vprintf(format, args); va_end(args); puts("");
}
static bool cancelled(void) { return image.cancel_before || (image.cancel && image.writes >= 8); }
static uint64_t blocks(void *p) { return ((struct image *)p)->blocks; }
static int read_image(void *p, uint32_t sector, size_t count, uint8_t *data) {
    struct image *i = p;
    return fseeko(i->file, (off_t)sector * 512, SEEK_SET) ||
           fread(data, 512, count, i->file) != count ? -1 : 0;
}
static int write_image(void *p, uint32_t sector, size_t count, const uint8_t *data) {
    struct image *i = p;
    if(++i->writes == i->fail_write) return -1;
    return fseeko(i->file, (off_t)sector * 512, SEEK_SET) ||
           fwrite(data, 512, count, i->file) != count ? -1 : 0;
}
static int sync_image(void *p) {
    struct image *i = p;
    return i->fail_sync || fflush(i->file) || fsync(fileno(i->file)) ? -1 : 0;
}
int main(int argc, char **argv) {
    if(argc < 2 || argc > 3) { fprintf(stderr, "Usage: %s REGULAR_IMAGE [write-fail|sync-fail|cancel|cancel-before|full]\n", argv[0]); return 2; }
    /* Test harness only: refuse block devices and symlinks. */
    struct stat st;
    if(lstat(argv[1], &st) || !S_ISREG(st.st_mode) || st.st_size % 512) return 2;
    image.file = fopen(argv[1], "r+b");
    if(!image.file) return 2;
    image.blocks = (uint64_t)st.st_size / 512;
    if(argc == 3) {
        if(!strcmp(argv[2], "write-fail")) image.fail_write = 8;
        else if(!strcmp(argv[2], "sync-fail")) image.fail_sync = true;
        else if(!strcmp(argv[2], "cancel")) image.cancel = true;
        else if(!strcmp(argv[2], "cancel-before")) image.cancel_before = true;
        else if(strcmp(argv[2], "full")) { fclose(image.file); return 2; }
    }
    struct kui_media_ops ops = {&image, blocks, read_image, write_image, sync_image};
    kui_media_set(&ops);
    if(argc == 3 && !strcmp(argv[2], "full")) {
        FATFS fs, *mounted;
        FIL fill;
        DWORD free_clusters;
        uint8_t zeros[32768] = {0};
        if(!kui_mount(&fs, log_line) || f_getfree("0:", &free_clusters, &mounted) != FR_OK ||
           f_open(&fill, "0:/occupy.bin", FA_CREATE_NEW | FA_WRITE) != FR_OK) return 2;
        uint64_t remaining = (uint64_t)free_clusters * mounted->csize * 512 - 1024 * 1024;
        while(remaining) {
            UINT done, n = remaining > sizeof(zeros) ? sizeof(zeros) : (UINT)remaining;
            if(f_write(&fill, zeros, n, &done) != FR_OK || done != n) return 2;
            remaining -= n;
        }
        if(f_close(&fill) != FR_OK) return 2;
        f_mount(NULL, "0:", 0);
    }
    bool result = kui_storage_probe(log_line, cancelled);
    fclose(image.file);
    return result ? 0 : 1;
}
