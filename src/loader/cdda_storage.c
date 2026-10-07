/* SPDX-License-Identifier: GPL-3.0-only */
#include "cdda_storage.h"
#include "sd_reader.h"
#include "sci_sd_bus.h"
#include "kui/boot_volume.h"
#include "kui/storage_policy.h"
#include "ff.h"
#include "diskio.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The bootstrap patches precisely one complete initialized marker after
 * checking the original image checksum. KEEP prevents section GC. This
 * harness deliberately supports the SCI transport only. */
volatile struct kui_storage_boot_marker cdda_storage_boot_marker
    __attribute__((section(".cdda_boot_marker"), used)) = KUI_STORAGE_BOOT_INITIALIZER;

static struct kui_loader_sd card;
static struct kui_volume volume;
static FATFS fs;
static FIL file;
static FIL data_file;
static uint32_t successful_blocks, file_size;
static uint32_t data_file_size;
static int leased, mounted, opened;
static int data_opened;
static const char *failure;

/* Read-only FatFs needs this one additional libc primitive. This image has
 * no libc dependency; keep its implementation local to the detached image. */
char *strchr(const char *text, int value) {
    const unsigned char *at = (const unsigned char *)text;
    unsigned char wanted = (unsigned char)value;
    while(*at != wanted) {
        if(!*at) return NULL;
        ++at;
    }
    return (char *)at;
}

static int fail(const char *message) { failure = message; return -1; }
static const char *fatfs_failure(FRESULT result) {
    switch(result) {
        case FR_OK: return "OK";
        case FR_DISK_ERR: return "Filesystem I/O";
        case FR_INT_ERR: return "Filesystem internal";
        case FR_NOT_READY: return "Storage not ready";
        case FR_NO_FILE: return "File missing";
        case FR_NO_PATH: return "Directory missing";
        case FR_INVALID_NAME: return "Invalid path";
        case FR_DENIED: return "File denied";
        case FR_INVALID_OBJECT: return "Invalid file";
        case FR_NO_FILESYSTEM: return "Unsupported filesystem";
        default: return "Filesystem failure";
    }
}

static int raw_read(void *ctx, uint32_t lba, size_t count, uint8_t *out) {
    (void)ctx;
    if(!card.ready || !out || !kui_block_range(lba, count, card.blocks))
        return fail("SD block range");
    while(count) {
        uint32_t take = count > KUI_LOADER_SD_MAX_READ_BLOCKS ?
            KUI_LOADER_SD_MAX_READ_BLOCKS : (uint32_t)count;
        enum kui_loader_sd_result result = kui_loader_sd_read_multi(&card, lba, take, out);
        if(result != KUI_LOADER_SD_OK) return fail(kui_loader_sd_result_name(result));
        successful_blocks += take;
        lba += take;
        out += take * 512u;
        count -= take;
    }
    return 0;
}
static uint64_t raw_blocks(void *ctx) { (void)ctx; return card.blocks; }

DSTATUS disk_initialize(BYTE drive) {
    return drive || !card.ready || !volume.count ? STA_NOINIT : STA_PROTECT;
}
DSTATUS disk_status(BYTE drive) { return disk_initialize(drive); }
DRESULT disk_read(BYTE drive, BYTE *out, LBA_t sector, UINT count) {
    if(drive || !out || !kui_block_range(sector, count, volume.count)) {
        (void)fail("Filesystem block range");
        return RES_PARERR;
    }
    if(!card.ready) return RES_NOTRDY;
    return raw_read(NULL, volume.start + sector, count, out) ? RES_ERROR : RES_OK;
}
/* Defense in depth even though FF_FS_READONLY removes every writer. */
DRESULT disk_write(BYTE drive, const BYTE *out, LBA_t sector, UINT count) {
    (void)drive; (void)out; (void)sector; (void)count;
    return RES_WRPRT;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *out) {
    if(drive) return RES_PARERR;
    if(!card.ready || !volume.count) return RES_NOTRDY;
    switch(command) {
        case CTRL_SYNC: return RES_OK;
        case GET_SECTOR_COUNT:
            if(!out) return RES_PARERR;
            *(LBA_t *)out = volume.count;
            return RES_OK;
        case GET_SECTOR_SIZE:
            if(!out) return RES_PARERR;
            *(WORD *)out = 512;
            return RES_OK;
        case GET_BLOCK_SIZE:
            if(!out) return RES_PARERR;
            *(DWORD *)out = 1;
            return RES_OK;
        default: return RES_PARERR;
    }
}

int cdda_storage_init(void) {
    if(mounted) return 0;
    failure = NULL;
    /* The main harness requires the bootstrap's SCI selection before this
     * entry. Also validate it at this storage boundary; a SCIF/IDE card
     * cannot accidentally be read through the SCI pins. */
    unsigned selected = cdda_storage_boot_marker.transport;
    if(cdda_storage_boot_marker.magic1 != KUI_STORAGE_BOOT_MAGIC1 ||
       cdda_storage_boot_marker.magic2 != KUI_STORAGE_BOOT_MAGIC2 ||
       cdda_storage_boot_marker.version != 1u ||
       cdda_storage_boot_marker.inverse != ~selected ||
       selected != KUI_STORAGE_SCI)
        return fail("This harness requires SCI");
    enum kui_loader_sd_result sd = kui_sci_sd_acquire();
    if(sd != KUI_LOADER_SD_OK) return fail(kui_loader_sd_result_name(sd));
    leased = 1;
    sd = kui_loader_sd_init_bus(&card, kui_sci_sd_bus());
    if(sd != KUI_LOADER_SD_OK) {
        (void)fail(kui_loader_sd_result_name(sd));
        cdda_storage_shutdown();
        return -1;
    }
    const struct kui_media_ops media = {NULL, raw_blocks, raw_read, NULL, NULL};
    struct kui_boot_layout layout;
    enum kui_boot_volume_result scan = kui_boot_volume_scan(&media, &layout);
    if(scan != KUI_BOOT_VOLUME_OK) {
        if(!failure) (void)fail("Invalid or ambiguous partitions");
        cdda_storage_shutdown();
        return -1;
    }
    unsigned found = 0;
    for(unsigned i = 0; i < layout.count; ++i) {
        if(layout.candidates[i].kind != KUI_BOOT_VOLUME_LINUX_CANDIDATE) {
            volume = layout.candidates[i].volume;
            ++found;
        }
    }
    if(found != 1) {
        (void)fail("No unique FAT/exFAT volume");
        cdda_storage_shutdown();
        return -1;
    }
    FRESULT result = f_mount(&fs, "0:", 1);
    if(result != FR_OK) {
        if(!failure) (void)fail(fatfs_failure(result));
        cdda_storage_shutdown();
        return -1;
    }
    mounted = 1;
    return 0;
}
int cdda_storage_open(const char *path, uint32_t *bytes) {
    if(!path || !bytes) return fail("Invalid file arguments");
    cdda_storage_close();
    if(cdda_storage_init()) return -1;
    failure = NULL;
    FRESULT result = f_open(&file, path, FA_READ);
    if(result != FR_OK) return fail(fatfs_failure(result));
    opened = 1;
    if(f_size(&file) > UINT32_MAX) {
        cdda_storage_close();
        return fail("File exceeds 4 GiB harness limit");
    }
    file_size = (uint32_t)f_size(&file);
    *bytes = file_size;
    return 0;
}
int cdda_storage_read_at(uint32_t offset, void *out, uint32_t bytes) {
    if(!opened || offset > file_size || bytes > file_size - offset || (!out && bytes))
        return fail("File read range");
    if(!bytes) return 0;
    failure = NULL;
    FRESULT result = FR_OK;
    if(f_tell(&file) != offset) result = f_lseek(&file, offset);
    if(result != FR_OK) return fail(fatfs_failure(result));
    if(f_tell(&file) != offset) return fail("Incomplete file seek");
    UINT got = 0;
    result = f_read(&file, out, bytes, &got);
    if(result != FR_OK) {
        if(!failure) (void)fail(fatfs_failure(result));
        return -1;
    }
    return got == bytes ? 0 : fail("Short file read");
}
void cdda_storage_close(void) {
    if(opened) {
        (void)f_close(&file);
        opened = 0;
    }
    file_size = 0;
}
int cdda_storage_data_open(uint32_t *bytes) {
    if(!bytes) return fail("Invalid data file arguments");
    cdda_storage_data_close();
    if(cdda_storage_init()) return -1;
    failure = NULL;
    FRESULT result = f_open(&data_file, "0:/KUI/tests/cdda/stress.bin", FA_READ);
    if(result != FR_OK) return fail(fatfs_failure(result));
    data_opened = 1;
    if(f_size(&data_file) > UINT32_MAX) {
        cdda_storage_data_close();
        return fail("Data file exceeds 4 GiB harness limit");
    }
    data_file_size = (uint32_t)f_size(&data_file);
    *bytes = data_file_size;
    return 0;
}
int cdda_storage_data_read_at(uint32_t offset, uint8_t *out, uint32_t bytes) {
    if(!data_opened || offset > data_file_size || bytes > data_file_size - offset || (!out && bytes))
        return fail("Data file read range");
    if(!bytes) return 0;
    failure = NULL;
    FRESULT result = FR_OK;
    if(f_tell(&data_file) != offset) result = f_lseek(&data_file, offset);
    if(result != FR_OK) return fail(fatfs_failure(result));
    if(f_tell(&data_file) != offset) return fail("Incomplete data file seek");
    UINT got = 0;
    result = f_read(&data_file, out, bytes, &got);
    if(result != FR_OK) {
        if(!failure) (void)fail(fatfs_failure(result));
        return -1;
    }
    return got == bytes ? 0 : fail("Short data file read");
}
void cdda_storage_data_close(void) {
    if(data_opened) {
        (void)f_close(&data_file);
        data_opened = 0;
    }
    data_file_size = 0;
}
void cdda_storage_shutdown(void) {
    cdda_storage_close();
    cdda_storage_data_close();
    (void)f_mount(NULL, "0:", 0);
    mounted = 0;
    memset(&volume, 0, sizeof(volume));
    if(leased) {
        kui_loader_sd_shutdown(&card);
        kui_sci_sd_release();
        leased = 0;
    }
}
uint32_t cdda_storage_blocks_read(void) { return successful_blocks; }
const char *cdda_storage_last_failure(void) { return failure ? failure : "OK"; }
