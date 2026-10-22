/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_probe.h"
#include "kui/loader_probe.h"
#include "kui/media.h"
#include "platform.h"
#include <string.h>

/* The package layout is intentionally separate from a general game loader.
 * It contains our own executable and never accepts a game's executable here. */
#define HEADER_OFFSET 0x100u
#define MANIFEST_OFFSET 0x1000u
#define RESIDENT_OFFSET 0x2000u
#define RESIDENT_LIMIT 0x100000u
static bool stopped(kui_cancel_fn cancel) { return cancel && cancel(); }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static bool package_layout(const struct kui_runtime_image *image) {
    if(image->info.payload_bytes<RESIDENT_OFFSET+4u ||
        image->info.payload_bytes>RESIDENT_OFFSET+RESIDENT_LIMIT ||
        image->info.memory_bytes!=image->info.payload_bytes) return false;
    const uint8_t *h=(const uint8_t *)image->data+HEADER_OFFSET;
    uint32_t resident=le32(h+28);
    if(memcmp(h,"KUILDR01",8) || le32(h+8)!=1 || le32(h+12)!=64 ||
        le32(h+16)!=MANIFEST_OFFSET || le32(h+20)!=KUI_LOADER_PROBE_MANIFEST_BYTES ||
        le32(h+24)!=0x8ce00000u || !resident || resident%4 || resident>RESIDENT_LIMIT ||
        resident+RESIDENT_OFFSET!=image->info.payload_bytes || le32(h+32)!=0x8ce00000u ||
        le32(h+36)!=0x8c010000u || le32(h+40)!=0x8cd00000u ||
        le32(h+44)!=0x8cff0000u || le32(h+48)!=RESIDENT_OFFSET ||
        le32(h+52)!=RESIDENT_LIMIT || le32(h+56) || le32(h+60)) return false;
    /* A reused map is never trusted: every launch prepares fresh card extents. */
    for(unsigned i=0;i<KUI_LOADER_PROBE_MANIFEST_BYTES;i++)
        if(((const uint8_t *)image->data)[MANIFEST_OFFSET+i]) return false;
    return true;
}
bool kui_games_probe_prepare(struct kui_runtime_image *image,kui_log_fn log,
    kui_cancel_fn cancelled) {
    if(!image || !log || !cancelled) return false;
    *image=(struct kui_runtime_image){0};
    if(stopped(cancelled)) return false;
    if(!kui_sd_connect()) {log("Loader probe: SD unavailable");return false;}
    FATFS fs;FIL file;bool opened=false,ok=false;
    const char *problem="cannot mount SD";
    struct kui_loader_probe_manifest map={0};
    if(!kui_mount(&fs,log)) goto done;
    enum kui_runtime_result loaded=kui_runtime_read(KUI_GAMES_PROBE_PACKAGE,image,log,cancelled);
    if(loaded!=KUI_RUNTIME_OK) {problem=kui_runtime_result_name(loaded);goto done;}
    if(!package_layout(image)) {problem="unsupported resident probe package layout";goto done;}
    if(f_open(&file,"0:" KUI_LOADER_PROBE_PATH,FA_READ)!=FR_OK) {
        problem="probe.dat missing or unreadable; copy KUI/apps/games";goto done;
    }
    opened=true;
    if(f_size(&file)!=KUI_LOADER_PROBE_FILE_BYTES) {problem="probe.dat has the wrong size";goto done;}
    const struct kui_volume volume=*kui_media_volume();
    if(!volume.count || (uint64_t)volume.start+volume.count>UINT32_MAX) {
        problem="invalid SD volume bounds";goto done;
    }
    map.card_sectors=volume.start+volume.count;
    for(uint32_t block=0;block<KUI_LOADER_PROBE_BLOCKS;block++) {
        if(stopped(cancelled)) {problem="cancelled before handoff";goto done;}
        /* FatFs R0.16 FIL.sect describes its cached sector. A full 512-byte
         * direct read need not update that member. Seek and read ONE byte to
         * force the cache path, then record its volume-relative sector. */
        if(f_lseek(&file,(FSIZE_t)block*512u)!=FR_OK || f_tell(&file)!=(FSIZE_t)block*512u) {
            problem="cannot seek probe fixture";goto done;
        }
        uint8_t byte;UINT got=0;
        if(f_read(&file,&byte,1,&got)!=FR_OK || got!=1) {
            problem="cannot map probe fixture";goto done;
        }
        if(file.sect<fs.database || file.sect>=volume.count) {
            problem="probe fixture sector outside data area";goto done;
        }
        uint32_t card=volume.start+(uint32_t)file.sect;
        struct kui_loader_probe_extent *last=map.extent_count?&map.extents[map.extent_count-1]:NULL;
        if(last && (uint64_t)last->card_lba+last->blocks==card) ++last->blocks;
        else {
            if(map.extent_count==KUI_LOADER_PROBE_EXTENTS) {problem="too many probe extents";goto done;}
            map.extents[map.extent_count++]=(struct kui_loader_probe_extent){block,card,1};
        }
    }
    if(kui_loader_probe_manifest_encode(&map,(uint8_t *)image->data+MANIFEST_OFFSET)!=KUI_LP_OK) {
        problem="invalid or overlapping probe extents";goto done;
    }
    ok=true;
done:
    if(opened && f_close(&file)!=FR_OK) {ok=false;problem="cannot close probe fixture";}
    if(f_mount(NULL,"0:",0)!=FR_OK) {ok=false;problem="cannot release SD filesystem";}
    kui_sd_disconnect();
    if(stopped(cancelled)) {ok=false;problem="cancelled before handoff";}
    if(!ok) {kui_runtime_free(image);log("Loader probe: %s",problem);return false;}
    log("Loader probe prepared: build=%s; %u blocks in %u extents; SD released",
        image->info.build,KUI_LOADER_PROBE_BLOCKS,map.extent_count);
    log("Resident probe replaces the launcher. Photograph PASS/failure; power cycle to return.");
    return true;
}
