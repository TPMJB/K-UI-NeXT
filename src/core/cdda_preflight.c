/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_preflight.h"
#include "kui/retail_image.h"
#include "kui/hash.h"
#include <string.h>
#include <stddef.h>

struct reader {
    const struct kui_game_image *image;
    const struct kui_cdda_disc_map *map;
    struct kui_cdda_preflight_ops ops;
    enum kui_cdda_preflight_phase phase;
    enum kui_cdda_preflight_result error;
    uint32_t metadata_reads,hash_reads;
};
static bool overlaps(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(an>UINTPTR_MAX-x || bn>UINTPTR_MAX-y) return true;
    return x<=y?y-x<an:x-y<bn;
}
static bool cancelled(struct reader *r) {
    if(r->ops.cancelled && r->ops.cancelled(r->ops.context)) {
        r->error=KUI_CDDA_PREFLIGHT_CANCELLED;return true;
    }
    return false;
}
static bool progress(struct reader *r,uint32_t done,uint32_t total) {
    if(cancelled(r) || (r->ops.progress && !r->ops.progress(r->ops.context,r->phase,done,total))) {
        r->error=KUI_CDDA_PREFLIGHT_CANCELLED;return false;
    }
    return true;
}
static bool range(void *context,uint32_t lba,uint32_t count) {
    struct reader *r=context;struct kui_cdda_disc_data span;
    return lba<=UINT32_MAX-150u && kui_cdda_disc_data_range(r->map,lba+150u,count,&span)==KUI_CDDA_DISC_OK;
}
static enum kui_game_metadata_io_result read_sector(void *context,uint32_t lba,uint8_t output[2048]) {
    struct reader *r=context;struct kui_cdda_disc_data span;
    if(cancelled(r)) return KUI_GAME_METADATA_IO_CANCELLED;
    if(lba>UINT32_MAX-150u || kui_cdda_disc_data_range(r->map,lba+150u,1u,&span)!=KUI_CDDA_DISC_OK) {
        r->error=KUI_CDDA_PREFLIGHT_RANGE;return KUI_GAME_METADATA_IO_ERROR;
    }
    const struct kui_cdda_disc_track *track;
    if(kui_cdda_disc_track(r->map,span.track,&track)!=KUI_CDDA_DISC_OK) {
        r->error=KUI_CDDA_PREFLIGHT_MAP;return KUI_GAME_METADATA_IO_ERROR;
    }
    enum kui_game_result result;
    if(track->stride==2352u) {
        uint8_t raw[2352];
        result=kui_game_image_read(r->image,lba,1u,KUI_GAME_SECTOR_RAW,raw,sizeof(raw));
        if(result==KUI_GAME_OK) {
            if(kui_retail_sector_header(raw,lba)!=KUI_RETAIL_HEADER_OK) {
                r->error=KUI_CDDA_PREFLIGHT_HEADER;return KUI_GAME_METADATA_IO_ERROR;
            }
            memcpy(output,raw+16u,2048u);
        }
    } else result=kui_game_image_read(r->image,lba,1u,KUI_GAME_SECTOR_MODE1,output,2048u);
    if(result!=KUI_GAME_OK) {
        r->error=result==KUI_GAME_CANCELLED?KUI_CDDA_PREFLIGHT_CANCELLED:KUI_CDDA_PREFLIGHT_IO;
        return result==KUI_GAME_CANCELLED?KUI_GAME_METADATA_IO_CANCELLED:KUI_GAME_METADATA_IO_ERROR;
    }
    if(r->phase==KUI_CDDA_PREFLIGHT_INSPECT) {
        r->metadata_reads++;
        if(!progress(r,r->metadata_reads,145u)) return KUI_GAME_METADATA_IO_CANCELLED;
    } else r->hash_reads++;
    return KUI_GAME_METADATA_IO_OK;
}
static enum kui_cdda_preflight_result hash_range(struct reader *r,uint32_t lba,uint32_t bytes,
    enum kui_cdda_preflight_phase phase,uint32_t *crc,uint8_t digest[32]) {
    r->phase=phase;struct kui_sha256 sha;kui_sha256_init(&sha);uint32_t value=0;
    uint8_t data[2048];
    if(!progress(r,0,bytes)) return r->error;
    for(uint32_t done=0;done<bytes;) {
        if(read_sector(r,lba+done/2048u,data)!=KUI_GAME_METADATA_IO_OK) return r->error;
        uint32_t take=bytes-done;if(take>sizeof(data)) take=sizeof(data);
        value=kui_retail_crc32(value,data,take);kui_sha256_update(&sha,data,take);done+=take;
        if(!progress(r,done,bytes)) return r->error;
    }
    *crc=value;kui_sha256_digest(&sha,digest);return KUI_CDDA_PREFLIGHT_OK;
}
enum kui_cdda_preflight_result kui_cdda_preflight_read(const struct kui_game_image *image,
    const struct kui_cdda_preflight_ops *ops,struct kui_cdda_preflight_report *out) {
    if(!image || !out || image->format!=KUI_GAME_IMAGE_GDI ||
       overlaps(out,sizeof(*out),image,sizeof(*image)) || (ops && overlaps(out,sizeof(*out),ops,sizeof(*ops))))
        return KUI_CDDA_PREFLIGHT_INVALID;
    struct kui_cdda_preflight_report report={0};
    if(kui_cdda_disc_from_image(image,&report.map)!=KUI_CDDA_DISC_OK || !report.map.complete)
        return KUI_CDDA_PREFLIGHT_MAP;
    struct reader r={.image=image,.map=&report.map,.phase=KUI_CDDA_PREFLIGHT_INSPECT};
    if(ops) r.ops=*ops;
    /* Validate the boot-session selection rather than accepting an arbitrary
     * caller-controlled LBA that happens to contain plausible ISO records. */
    uint32_t session=0;bool selected=false;
    for(uint32_t i=0;i<report.map.count;i++) {
        const struct kui_cdda_disc_track *t=&report.map.tracks[i];
        if(t->control==4u && (!selected || (session<45000u && t->start_fad>=45150u))) {
            session=t->start_fad-150u;selected=true;
        }
    }
    if(!selected || image->data_lba!=session) return KUI_CDDA_PREFLIGHT_MAP;
    if(!range(&r,session,16u)) return KUI_CDDA_PREFLIGHT_RANGE;
    if(!progress(&r,0,145u)) return r.error;
    const struct kui_game_metadata_ops metadata_ops={&r,read_sector,range};
    enum kui_game_metadata_status metadata=kui_game_metadata_read(&metadata_ops,session,&report.metadata);
    if(metadata!=KUI_GAME_METADATA_OK) {
        if(r.error!=KUI_CDDA_PREFLIGHT_OK) return r.error;
        return metadata==KUI_GAME_METADATA_CANCELLED?KUI_CDDA_PREFLIGHT_CANCELLED:KUI_CDDA_PREFLIGHT_METADATA;
    }
    uint32_t sectors=(report.metadata.boot_bytes+2047u)/2048u;
    if(!report.metadata.ip_valid || !report.metadata.boot_valid || !report.metadata.boot_bytes ||
       report.metadata.boot_lba<session+16u ||
       report.metadata.boot_bytes>KUI_GAME_METADATA_MAX_BOOT_BYTES ||
       !range(&r,session,16u) || !range(&r,report.metadata.boot_lba,sectors))
        return KUI_CDDA_PREFLIGHT_RANGE;
    report.ip_lba=session;report.ip_bytes=KUI_CDDA_PREFLIGHT_IP_BYTES;report.scrambled=image->scrambled;
    enum kui_cdda_preflight_result result=hash_range(&r,session,report.ip_bytes,KUI_CDDA_PREFLIGHT_HASH_IP,
        &report.ip_crc32,report.ip_sha256);
    if(result!=KUI_CDDA_PREFLIGHT_OK) return result;
    result=hash_range(&r,report.metadata.boot_lba,report.metadata.boot_bytes,KUI_CDDA_PREFLIGHT_HASH_BOOT,
        &report.boot_crc32,report.boot_sha256);
    if(result!=KUI_CDDA_PREFLIGHT_OK) return result;
    if(cancelled(&r)) return r.error;
    report.metadata_reads=r.metadata_reads;report.hash_reads=r.hash_reads;*out=report;
    return KUI_CDDA_PREFLIGHT_OK;
}
const char *kui_cdda_preflight_result_name(enum kui_cdda_preflight_result result) {
    switch(result) {
    case KUI_CDDA_PREFLIGHT_OK:return "Complete image preflight passed";
    case KUI_CDDA_PREFLIGHT_INVALID:return "Invalid preflight request";
    case KUI_CDDA_PREFLIGHT_MAP:return "Incomplete or unsupported image metadata";
    case KUI_CDDA_PREFLIGHT_METADATA:return "Invalid boot metadata";
    case KUI_CDDA_PREFLIGHT_RANGE:return "IP or boot extent outside backed data";
    case KUI_CDDA_PREFLIGHT_HEADER:return "Raw sector header or address mismatch";
    case KUI_CDDA_PREFLIGHT_IO:return "Image sector read failed";
    case KUI_CDDA_PREFLIGHT_CANCELLED:return "Image preflight cancelled";
    default:return "Unknown preflight result";
    }
}
