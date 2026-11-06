/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/media.h"
#include "kui/clock.h"
#include "ff.h"
#include "diskio.h"
#include <string.h>

static struct kui_media_ops media;
static struct kui_volume volume;
static DSTATUS state = STA_NOINIT;
static bool attempted,boot_view;
static const char *problem;
static uint64_t mutation_epoch=1u;
uint64_t kui_media_mutation_epoch(void) {return mutation_epoch;}
static void changed(void) {if(++mutation_epoch==0u)++mutation_epoch;}
struct read_cache_sector {uint32_t sector;uint8_t data[512];};
static struct {
    struct read_cache_sector *entries;
    size_t capacity,count,next;
} read_cache;

void kui_media_read_cache_end(void) {memset(&read_cache,0,sizeof(read_cache));}
bool kui_media_read_cache_begin(void *scratch,size_t bytes) {
    if(read_cache.entries || !scratch || bytes<sizeof(struct read_cache_sector) ||
       (uintptr_t)scratch%_Alignof(struct read_cache_sector))return false;
    size_t capacity=bytes/sizeof(struct read_cache_sector);
    if(capacity>64u)capacity=64u;
    read_cache.entries=scratch;read_cache.capacity=capacity;
    read_cache.count=read_cache.next=0u;return true;
}

DWORD get_fattime(void) { return (DWORD)kui_clock_fattime(); }

void kui_media_set(const struct kui_media_ops *ops) {
    changed();
    kui_media_read_cache_end();
    media = ops ? *ops : (struct kui_media_ops){0};
    memset(&volume, 0, sizeof(volume));
    state = STA_NOINIT;
    attempted = false;
    boot_view = false;
    problem = NULL;
}
bool kui_media_boot_view(const struct kui_media_ops *raw,const struct kui_volume *view) {
    /* Copy first so aliases of the current volume cannot be cleared before
     * validation. Invalid requests must not leave a writable old view live. */
    struct kui_media_ops copy=raw?*raw:(struct kui_media_ops){0};
    struct kui_volume extent=view?*view:(struct kui_volume){0};
    kui_media_set(NULL);
    if(!copy.blocks || !copy.read || !view) return false;
    uint64_t total=copy.blocks(copy.ctx);
    if(!total || total>UINT32_MAX || !kui_block_range(extent.start,extent.count,total)) return false;
    copy.write=NULL;copy.sync=NULL;
    media=copy;volume=extent;boot_view=true;
    state=STA_NOINIT|STA_PROTECT;
    return true;
}
const struct kui_volume *kui_media_volume(void) { return &volume; }
const char *kui_media_problem(void) { return problem; }

DSTATUS disk_status(BYTE drive) { return drive == 0 ? state : STA_NOINIT; }
DSTATUS disk_initialize(BYTE drive) {
    if(drive != 0) {changed();return STA_NOINIT;}
    if(attempted) {
        if(state&STA_NOINIT)changed();
        return state; /* A failed card cannot silently recover mid-job. */
    }
    attempted = true;
    if(!media.blocks || !media.read || (!boot_view && (!media.write || !media.sync))) {
        changed();problem = "Storage device is not connected"; return state;
    }
    uint8_t sector[512];
    uint64_t blocks = media.blocks(media.ctx);
    if(!blocks || blocks > UINT32_MAX) {
        changed();problem = "Cannot read capacity, or card exceeds 32-bit block limit"; return state;
    }
    if(boot_view) {
        if(!kui_block_range(volume.start,volume.count,blocks)) {
            changed();problem="Boot partition no longer fits the connected device";return state;
        }
        state=STA_PROTECT;
        return state;
    }
    if(media.read(media.ctx, 0, 1, sector)) {
        changed();problem = "Cannot read SD sector zero"; return state;
    }
    if(!kui_select_volume(sector, blocks, &volume)) {
        changed();
        problem = "Unsupported/ambiguous layout: use one MBR FAT32/exFAT partition or superfloppy";
        return state;
    }
    state = 0;
    return state;
}

static bool valid(BYTE drive, LBA_t sector, UINT count) {
    return drive == 0 && !(state & (STA_NOINIT|STA_NODISK)) &&
        kui_block_range(sector, count, volume.count);
}
static DRESULT finish(int result) {
    if(!result) return RES_OK;
    changed();
    kui_media_read_cache_end();
    state = STA_NOINIT | (boot_view?STA_PROTECT:0);
    problem = "Storage I/O or sync failed; reconnect before another test";
    return RES_ERROR;
}
DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count) {
    if(!buffer || !valid(drive, sector, count)) {
        changed();
        kui_media_read_cache_end();return RES_PARERR;
    }
    if(count==1u && read_cache.entries)
        for(size_t i=0;i<read_cache.count;i++)
            if(read_cache.entries[i].sector==sector) {
                memcpy(buffer,read_cache.entries[i].data,512u);return RES_OK;
            }
    DRESULT result=finish(media.read(media.ctx, volume.start + sector, count, buffer));
    if(result==RES_OK && count==1u && read_cache.entries) {
        struct read_cache_sector *entry=read_cache.entries+read_cache.next;
        entry->sector=sector;memcpy(entry->data,buffer,512u);
        if(read_cache.count<read_cache.capacity)++read_cache.count;
        read_cache.next=(read_cache.next+1u)%read_cache.capacity;
    }
    return result;
}
DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count) {
    /* Even an unexpected/refused write ends the read-only scope. */
    kui_media_read_cache_end();
    changed();
    if(!buffer || !valid(drive, sector, count)) return RES_PARERR;
    if(boot_view) return RES_WRPRT;
    return finish(media.write(media.ctx, volume.start + sector, count, buffer));
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer) {
    if(drive != 0 || (state & (STA_NOINIT|STA_NODISK))) {
        changed();
        kui_media_read_cache_end();return RES_NOTRDY;
    }
    switch(command) {
        case CTRL_SYNC: return boot_view?RES_OK:finish(media.sync(media.ctx));
        case GET_SECTOR_COUNT:
            if(!buffer) {changed();kui_media_read_cache_end();return RES_PARERR;}
            *(LBA_t *)buffer = volume.count; return RES_OK;
        case GET_SECTOR_SIZE:
            if(!buffer) {changed();kui_media_read_cache_end();return RES_PARERR;}
            *(WORD *)buffer = 512; return RES_OK;
        case GET_BLOCK_SIZE:
            if(!buffer) {changed();kui_media_read_cache_end();return RES_PARERR;}
            *(DWORD *)buffer = 1; return RES_OK;
        default: changed();kui_media_read_cache_end();return RES_PARERR;
    }
}
