/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/media.h"
#include "ff.h"
#include "diskio.h"
#include <string.h>

static struct kui_media_ops media;
static struct kui_volume volume;
static DSTATUS state = STA_NOINIT;
static bool attempted;
static const char *problem;

void kui_media_set(const struct kui_media_ops *ops) {
    media = ops ? *ops : (struct kui_media_ops){0};
    memset(&volume, 0, sizeof(volume));
    state = STA_NOINIT;
    attempted = false;
    problem = NULL;
}
const struct kui_volume *kui_media_volume(void) { return &volume; }
const char *kui_media_problem(void) { return problem; }

DSTATUS disk_status(BYTE drive) { return drive == 0 ? state : STA_NOINIT; }
DSTATUS disk_initialize(BYTE drive) {
    if(drive != 0) return STA_NOINIT;
    if(attempted) return state; /* A failed card cannot silently recover mid-job. */
    attempted = true;
    if(!media.blocks || !media.read || !media.write || !media.sync) {
        problem = "Storage device is not connected"; return state;
    }
    uint8_t sector[512];
    uint64_t blocks = media.blocks(media.ctx);
    if(!blocks || blocks > UINT32_MAX) {
        problem = "Cannot read capacity, or card exceeds 32-bit block limit"; return state;
    }
    if(media.read(media.ctx, 0, 1, sector)) {
        problem = "Cannot read SD sector zero"; return state;
    }
    if(!kui_select_volume(sector, blocks, &volume)) {
        problem = "Unsupported/ambiguous layout: use one MBR FAT32/exFAT partition or superfloppy";
        return state;
    }
    state = 0;
    return state;
}

static bool valid(BYTE drive, LBA_t sector, UINT count) {
    return drive == 0 && !state && kui_block_range(sector, count, volume.count);
}
static DRESULT finish(int result) {
    if(!result) return RES_OK;
    state = STA_NOINIT;
    problem = "Storage I/O or sync failed; reconnect before another test";
    return RES_ERROR;
}
DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count) {
    if(!buffer || !valid(drive, sector, count)) return RES_PARERR;
    return finish(media.read(media.ctx, volume.start + sector, count, buffer));
}
DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count) {
    if(!buffer || !valid(drive, sector, count)) return RES_PARERR;
    return finish(media.write(media.ctx, volume.start + sector, count, buffer));
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer) {
    if(drive != 0 || state) return RES_NOTRDY;
    switch(command) {
        case CTRL_SYNC: return finish(media.sync(media.ctx));
        case GET_SECTOR_COUNT:
            if(!buffer) return RES_PARERR;
            *(LBA_t *)buffer = volume.count; return RES_OK;
        case GET_SECTOR_SIZE:
            if(!buffer) return RES_PARERR;
            *(WORD *)buffer = 512; return RES_OK;
        case GET_BLOCK_SIZE:
            if(!buffer) return RES_PARERR;
            *(DWORD *)buffer = 1; return RES_OK;
        default: return RES_PARERR;
    }
}
