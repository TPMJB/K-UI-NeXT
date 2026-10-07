/* SPDX-License-Identifier: GPL-3.0-only */
/* Integration test: actual FatFs and cdda_storage.c, with a file-backed
 * read-only SD boundary. SD wire protocol and hardware DMA are tested
 * separately; this mock deliberately does not claim to exercise either.
 * A separately compiled writer populates synthetic exFAT images. */
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *image;

#ifdef KUI_CDDA_TEST_POPULATE
DSTATUS disk_initialize(BYTE drive) { return drive ? STA_NOINIT : 0; }
DSTATUS disk_status(BYTE drive) { return disk_initialize(drive); }
DRESULT disk_read(BYTE drive, BYTE *out, LBA_t sector, UINT count) {
    if(drive || fseek(image, (long)sector * 512, SEEK_SET) ||
       fread(out, 512, count, image) != count) return RES_ERROR;
    return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE *out, LBA_t sector, UINT count) {
    if(drive || fseek(image, (long)sector * 512, SEEK_SET) ||
       fwrite(out, 512, count, image) != count) return RES_ERROR;
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *out) {
    (void)out;
    return drive || command != CTRL_SYNC ? RES_PARERR : RES_OK;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    image = fopen(argv[1], "r+b");
    FILE *raw = fopen(argv[2], "rb");
    assert(image && raw);
    FATFS fs;
    FIL file;
    assert(f_mount(&fs, "0:", 1) == FR_OK && fs.fs_type == FS_EXFAT);
    assert(f_mkdir("0:/KUI") == FR_OK);
    assert(f_mkdir("0:/KUI/tests") == FR_OK);
    assert(f_mkdir("0:/KUI/tests/cdda") == FR_OK);
    assert(f_open(&file, "0:/KUI/tests/cdda/stereo.raw", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK);
    BYTE bytes[8192];
    size_t got;
    while((got = fread(bytes, 1, sizeof(bytes), raw))) {
        UINT put = 0;
        assert(f_write(&file, bytes, (UINT)got, &put) == FR_OK && put == got);
    }
    assert(!ferror(raw));
    assert(f_close(&file) == FR_OK);
    assert(fclose(image) == 0 && fclose(raw) == 0);
    return 0;
}
#else
#include "cdda_storage.h"
#include "sd_reader.h"
#include "sci_sd_bus.h"
#include "kui/storage_policy.h"

extern volatile struct kui_storage_boot_marker cdda_storage_boot_marker;

static unsigned leases, releases, requests, largest, inject_crc;
static uint64_t blocks;
static const struct kui_loader_sd_bus bus = {0};
enum kui_loader_sd_result kui_sci_sd_acquire(void) {
    ++leases;
    return KUI_LOADER_SD_OK;
}
void kui_sci_sd_release(void) { ++releases; }
const struct kui_loader_sd_bus *kui_sci_sd_bus(void) { return &bus; }
enum kui_loader_sd_result kui_loader_sd_init_bus(struct kui_loader_sd *card,
        const struct kui_loader_sd_bus *source) {
    card->bus = *source;
    card->blocks = blocks;
    card->ready = true;
    return KUI_LOADER_SD_OK;
}
void kui_loader_sd_shutdown(struct kui_loader_sd *card) { card->ready = false; }
const char *kui_loader_sd_result_name(enum kui_loader_sd_result result) {
    return result == KUI_LOADER_SD_CRC ? "CRC" : "mock SD failure";
}
enum kui_loader_sd_result kui_loader_sd_read_multi(struct kui_loader_sd *card,
        uint32_t lba, uint32_t count, void *out) {
    assert(card->ready && count && count <= KUI_LOADER_SD_MAX_READ_BLOCKS);
    assert((uint64_t)lba + count <= blocks);
    ++requests;
    if(count > largest) largest = count;
    if(inject_crc) {
        inject_crc = 0;
        return KUI_LOADER_SD_CRC;
    }
    if(fseek(image, (long)lba * 512, SEEK_SET) ||
       fread(out, 512, count, image) != count) return KUI_LOADER_SD_RANGE;
    return KUI_LOADER_SD_OK;
}
int main(int argc, char **argv) {
    assert(argc == 4);
    image = fopen(argv[1], "rb"); /* No write-capable image descriptor. */
    FILE *raw = fopen(argv[2], "rb");
    assert(image && raw);
    assert(fseek(image, 0, SEEK_END) == 0);
    blocks = (uint64_t)ftell(image) / 512;
    assert(fseek(raw, 0, SEEK_END) == 0);
    uint32_t expected = (uint32_t)ftell(raw);
    assert(expected > 131072 && fseek(raw, 0, SEEK_SET) == 0);
    unsigned char *reference = malloc(expected), *got = malloc(expected + 1u);
    assert(reference && got && fread(reference, 1, expected, raw) == expected);
    assert(fclose(raw) == 0);

    uint32_t size = 0;
    /* The bootstrap must explicitly patch SCI after verifying the original
     * runtime checksum. An unpatched AUTO or another selected transport must
     * fail before touching the SCI lease or filesystem. */
    assert(cdda_storage_boot_marker.transport == KUI_STORAGE_AUTO);
    assert(cdda_storage_init() < 0 && leases == 0);
    cdda_storage_boot_marker.transport = KUI_STORAGE_SCIF;
    cdda_storage_boot_marker.inverse = ~(uint32_t)KUI_STORAGE_SCIF;
    assert(cdda_storage_init() < 0 && leases == 0);
    cdda_storage_boot_marker.transport = KUI_STORAGE_IDE;
    cdda_storage_boot_marker.inverse = ~(uint32_t)KUI_STORAGE_IDE;
    assert(cdda_storage_init() < 0 && leases == 0);
    cdda_storage_boot_marker.transport = KUI_STORAGE_SCI;
    cdda_storage_boot_marker.inverse = ~(uint32_t)KUI_STORAGE_SCI;
    assert(cdda_storage_init() == 0);
    assert(cdda_storage_open("0:/KUI/tests/cdda/nope.raw", &size) < 0);
    assert(!strcmp(cdda_storage_last_failure(), "File missing"));
    assert(cdda_storage_open("0:/KUI/tests/cdda/stereo.raw", &size) == 0 && size == expected);
    assert(cdda_storage_read_at(127, got + 1, 8191) == 0);
    assert(!memcmp(got + 1, reference + 127, 8191));
    assert(cdda_storage_read_at(size, got, 1) < 0);
    assert(cdda_storage_read_at(size, NULL, 0) == 0);
    assert(cdda_storage_read_at(0, got, size) == 0 && !memcmp(got, reference, size));
    assert(cdda_storage_read_at(5, got, 19) == 0 && !memcmp(got, reference + 5, 19));
    inject_crc = 1;
    assert(cdda_storage_read_at(65536, got, 65536) < 0);
    assert(!strcmp(cdda_storage_last_failure(), "CRC"));
    /* FatFs latches a failed read in FIL: no implicit retry on that object. */
    assert(cdda_storage_read_at(65536, got, 65536) < 0);
    cdda_storage_close();
    assert(cdda_storage_open("0:/KUI/tests/cdda/stereo.raw", &size) == 0);
    assert(cdda_storage_read_at(65536, got, 65536) == 0);
    assert(!memcmp(got, reference + 65536, 65536));
    assert(cdda_storage_blocks_read() > 0);
    assert(disk_write(0, got, 0, 1) == RES_WRPRT);
    assert(largest >= (unsigned)atoi(argv[3]) && largest <= 128);
    cdda_storage_shutdown();
    assert(leases == 1 && releases == 1);
    printf("PASS %s: %u exact bytes; %u SD calls, maximum %u blocks; "
           "missing/range/CRC errors, write protection, lease release\n",
           argv[1], size, requests, largest);
    assert(fclose(image) == 0);
    free(reference);
    free(got);
    return 0;
}
#endif
