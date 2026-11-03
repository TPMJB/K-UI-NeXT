/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_boot.h"
#include "kui/hash.h"
#include "../loader/stage_fast_crc32.h"
#include <stddef.h>
#include <string.h>

struct preparation {
    const struct kui_disc_boot_source *source;
    uint32_t reads, primary_fad;
    bool have_ip;
    uint8_t first_ip_sha256[32];
};
static bool disjoint(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t ap=(uintptr_t)a,bp=(uintptr_t)b;
    return an<=UINTPTR_MAX-ap && bn<=UINTPTR_MAX-bp &&
        (ap+an<=bp || bp+bn<=ap);
}
static bool stopped(const struct preparation *p) {
    return p->source->cancelled(p->source->context);
}
static bool fad_range(const struct preparation *p,uint32_t fad,uint32_t count) {
    const struct kui_disc_boot_source *s=p->source;
    if(!count || fad<s->first_fad || fad>=s->end_fad || count>s->end_fad-fad)
        return false;
    if(!s->data_range_count)return true;
    for(uint32_t i=0u;i<s->data_range_count;i++) {
        const struct kui_disc_boot_data_range *r=&s->data_ranges[i];
        if(fad<r->first_fad)return false;
        if(fad<r->end_fad)return count<=r->end_fad-fad;
    }
    return false;
}
static enum kui_disc_boot_status read_fad(struct preparation *p,uint32_t fad,
                                        uint8_t data[2048]) {
    if(!fad_range(p,fad,1u))return KUI_DISC_BOOT_ISO;
    if(stopped(p))return KUI_DISC_BOOT_CANCELLED;
    enum kui_disc_boot_io_result r=p->source->read_sector(p->source->context,fad,data);
    if(r==KUI_DISC_BOOT_IO_CANCELLED)return KUI_DISC_BOOT_CANCELLED;
    if(r!=KUI_DISC_BOOT_IO_OK)return KUI_DISC_BOOT_IO;
    if(stopped(p))return KUI_DISC_BOOT_CANCELLED;
    ++p->reads;
    return KUI_DISC_BOOT_OK;
}
static enum kui_disc_boot_status read_hash_run(struct preparation *p,uint32_t fad,
                                              uint32_t count,uint8_t *data) {
    if(!p->source->read_run)return read_fad(p,fad,data);
    if(count>KUI_DISC_BOOT_RUN_SECTORS || !fad_range(p,fad,count))return KUI_DISC_BOOT_ISO;
    if(stopped(p))return KUI_DISC_BOOT_CANCELLED;
    enum kui_disc_boot_io_result r=p->source->read_run(p->source->context,fad,count,data);
    if(r==KUI_DISC_BOOT_IO_CANCELLED)return KUI_DISC_BOOT_CANCELLED;
    /* Preserve a primary device error even when the callback also observes a
     * cancellation. The device result is needed to diagnose failed media. */
    if(r!=KUI_DISC_BOOT_IO_OK)return KUI_DISC_BOOT_IO;
    if(stopped(p))return KUI_DISC_BOOT_CANCELLED;
    p->reads+=count;
    return KUI_DISC_BOOT_OK;
}
static void sha256(const uint8_t *data,size_t bytes,uint8_t digest[32]) {
    struct kui_sha256 sha;kui_sha256_init(&sha);kui_sha256_update(&sha,data,bytes);
    kui_sha256_digest(&sha,digest);
}
static enum kui_game_metadata_io_result metadata_sector(void *context,uint32_t lba,
                                                       uint8_t data[2048]) {
    struct preparation *p=context;
    if(lba>UINT32_MAX-150u)return KUI_GAME_METADATA_IO_ERROR;
    uint32_t fad=lba+150u;
    if(p->source->read_phase)p->source->read_phase(p->source->context,
        fad==p->source->first_fad?"IP header":
        fad>=p->source->first_fad+16u && fad<p->source->first_fad+32u?
            "ISO descriptors":"ISO directory");
    enum kui_disc_boot_status r=read_fad(p,fad,data);
    if(r==KUI_DISC_BOOT_CANCELLED)return KUI_GAME_METADATA_IO_CANCELLED;
    if(r!=KUI_DISC_BOOT_OK)return KUI_GAME_METADATA_IO_ERROR;
    if(fad==p->source->first_fad && !p->have_ip) {
        sha256(data,2048u,p->first_ip_sha256);p->have_ip=true;
    }
    if(!p->primary_fad && fad>=p->source->first_fad+16u &&
       fad<p->source->first_fad+32u && data[0]==1u &&
       !memcmp(data+1u,"CD001",5u) && data[6]==1u)p->primary_fad=fad;
    return KUI_GAME_METADATA_IO_OK;
}
static bool metadata_range(void *context,uint32_t lba,uint32_t count) {
    struct preparation *p=context;
    bool valid=lba<=UINT32_MAX-150u && fad_range(p,lba+150u,count);
    if(!valid && p->source->extent_refused)
        p->source->extent_refused(p->source->context,lba,count);
    return valid;
}
static enum kui_disc_boot_status metadata_result(enum kui_game_metadata_status r) {
    switch(r) {
    case KUI_GAME_METADATA_OK:return KUI_DISC_BOOT_OK;
    case KUI_GAME_METADATA_ARGUMENT:return KUI_DISC_BOOT_ARGUMENT;
    case KUI_GAME_METADATA_IO:return KUI_DISC_BOOT_IO;
    case KUI_GAME_METADATA_CANCELLED:return KUI_DISC_BOOT_CANCELLED;
    case KUI_GAME_METADATA_IP_HEADER:return KUI_DISC_BOOT_IP;
    case KUI_GAME_METADATA_ISO:return KUI_DISC_BOOT_ISO;
    case KUI_GAME_METADATA_BOOT_NOT_FOUND:return KUI_DISC_BOOT_NOT_FOUND;
    case KUI_GAME_METADATA_LIMIT:return KUI_DISC_BOOT_LIMIT;
    default:return KUI_DISC_BOOT_UNSUPPORTED;
    }
}
static bool overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
    return (uint64_t)a+an>b && (uint64_t)b+bn>a;
}
static enum kui_disc_boot_status hash_extent(struct preparation *p,uint32_t fad,
    uint32_t bytes,bool ip,uint32_t *crc,uint8_t digest[32]) {
    struct kui_sha256 sha;kui_sha256_init(&sha);uint32_t checked=0u;
    uint8_t sectors[KUI_DISC_BOOT_RUN_SECTORS*2048u];
    for(uint32_t done=0u;done<bytes;) {
        uint32_t count=(bytes-done+2047u)/2048u;
        if(count>KUI_DISC_BOOT_RUN_SECTORS)count=KUI_DISC_BOOT_RUN_SECTORS;
        if(!p->source->read_run)count=1u;
        enum kui_disc_boot_status r=read_hash_run(p,fad+done/2048u,count,sectors);
        if(r!=KUI_DISC_BOOT_OK)return r;
        if(ip && !done) {
            uint8_t current[32];sha256(sectors,2048u,current);
            if(!p->have_ip || memcmp(current,p->first_ip_sha256,sizeof(current)))
                return KUI_DISC_BOOT_CHANGED;
        }
        uint32_t amount=bytes-done;
        if(amount>count*2048u)amount=count*2048u;
        checked=kui_stage_crc32(checked,sectors,amount);kui_sha256_update(&sha,sectors,amount);
        done+=amount;
    }
    *crc=checked;kui_sha256_digest(&sha,digest);return KUI_DISC_BOOT_OK;
}
enum kui_disc_boot_status kui_disc_boot_prepare(const struct kui_disc_boot_source *source,
                                               struct kui_disc_boot_plan *out) {
    if(!source || !out || !disjoint(source,sizeof(*source),out,sizeof(*out)))
        return KUI_DISC_BOOT_ARGUMENT;
    if(source->data_range_count && source->data_range_count<=KUI_DISC_BOOT_MAX_DATA_RANGES &&
       source->data_ranges && !disjoint(source->data_ranges,
        (size_t)source->data_range_count*sizeof(*source->data_ranges),out,sizeof(*out)))
        return KUI_DISC_BOOT_ARGUMENT;
    memset(out,0,sizeof(*out));
    /* Firmware TOC FADs are 24-bit. Require the observed high-density session
     * and enough room for its IP and first ISO descriptor before any read. */
    if(!source->read_sector || !source->cancelled ||
       source->max_exec_bytes>KUI_DISC_BOOT_MAX_EXEC_BYTES)return KUI_DISC_BOOT_ARGUMENT;
    if(source->disc_type!=KUI_DISC_BOOT_GD_TYPE)return KUI_DISC_BOOT_MEDIA;
    if(source->first_fad<45150u ||
       source->end_fad>UINT32_C(0x01000000) || source->first_fad>=source->end_fad ||
       source->end_fad-source->first_fad<18u)return KUI_DISC_BOOT_ARGUMENT;
    if(source->data_range_count>KUI_DISC_BOOT_MAX_DATA_RANGES ||
       !!source->data_ranges!=!!source->data_range_count)return KUI_DISC_BOOT_ARGUMENT;
    for(uint32_t i=0u;i<source->data_range_count;i++) {
        const struct kui_disc_boot_data_range *range=&source->data_ranges[i];
        if(range->first_fad<source->first_fad || range->first_fad>=range->end_fad ||
           range->end_fad>source->end_fad ||
           (i && source->data_ranges[i-1u].end_fad>range->first_fad) ||
           (!i && (range->first_fad!=source->first_fad ||
                    range->end_fad-range->first_fad<18u)) ||
           (i+1u==source->data_range_count && range->end_fad!=source->end_fad))
            return KUI_DISC_BOOT_ARGUMENT;
    }
    struct preparation p={.source=source};
    struct kui_disc_boot_plan plan={0};
    const struct kui_game_metadata_ops ops={&p,metadata_sector,metadata_range};
    enum kui_disc_boot_status r=metadata_result(kui_game_metadata_read(&ops,
        source->first_fad-150u,&plan.metadata));
    if(r!=KUI_DISC_BOOT_OK)return r;
    if(plan.metadata.windows_ce)return KUI_DISC_BOOT_WINDOWS_CE;
    if(!plan.metadata.native_gd)return KUI_DISC_BOOT_MEDIA;
    uint32_t root_sectors=(plan.metadata.root_bytes+2047u)/2048u;
    uint32_t boot_sectors=(plan.metadata.boot_bytes+2047u)/2048u;
    uint32_t root_fad=plan.metadata.root_lba+150u,boot_fad=plan.metadata.boot_lba+150u;
    /* These ranges have already passed metadata_range. Keep IP/descriptors,
     * the complete directory and executable physically disjoint as well. */
    if(!p.primary_fad || root_fad<=p.primary_fad || boot_fad<=p.primary_fad ||
       overlap(root_fad,root_sectors,boot_fad,boot_sectors))return KUI_DISC_BOOT_ISO;
    uint32_t max_exec_bytes=source->max_exec_bytes?source->max_exec_bytes:
        KUI_DISC_BOOT_MAX_EXEC_BYTES;
    if(plan.metadata.boot_bytes>max_exec_bytes)return KUI_DISC_BOOT_LIMIT;
    if(source->read_phase)source->read_phase(source->context,"IP.BIN verification");
    r=hash_extent(&p,source->first_fad,KUI_DISC_BOOT_IP_BYTES,true,
        &plan.ip_crc32,plan.ip_sha256);
    if(r!=KUI_DISC_BOOT_OK)return r;
    if(source->read_phase)source->read_phase(source->context,"boot executable verification");
    r=hash_extent(&p,boot_fad,plan.metadata.boot_bytes,false,
        &plan.boot_crc32,plan.boot_sha256);
    if(r!=KUI_DISC_BOOT_OK)return r;
    if(stopped(&p))return KUI_DISC_BOOT_CANCELLED;
    plan.first_fad=source->first_fad;plan.end_fad=source->end_fad;
    plan.boot_fad=boot_fad;plan.boot_sectors=boot_sectors;plan.sectors_read=p.reads;
    *out=plan;return KUI_DISC_BOOT_OK;
}
const char *kui_disc_boot_status_text(enum kui_disc_boot_status status) {
    switch(status) {
    case KUI_DISC_BOOT_OK:return "Physical GD boot bytes verified";
    case KUI_DISC_BOOT_ARGUMENT:return "Invalid physical GD boot request or data extent";
    case KUI_DISC_BOOT_MEDIA:return "This launch path requires a native GD-ROM disc";
    case KUI_DISC_BOOT_IO:return "Physical disc boot data could not be read";
    case KUI_DISC_BOOT_CANCELLED:return "Physical disc boot preparation stopped";
    case KUI_DISC_BOOT_IP:return "Physical disc boot header missing or invalid";
    case KUI_DISC_BOOT_ISO:return "Invalid physical disc ISO9660 metadata or boot extent";
    case KUI_DISC_BOOT_NOT_FOUND:return "Boot executable not found on the physical disc";
    case KUI_DISC_BOOT_LIMIT:return "Physical disc boot data exceeds launch limits";
    case KUI_DISC_BOOT_UNSUPPORTED:return "Unsupported physical disc boot layout";
    case KUI_DISC_BOOT_WINDOWS_CE:return "Windows CE needs its separate disc launch path";
    case KUI_DISC_BOOT_CHANGED:return "Physical disc boot header changed during preparation";
    default:return "Unknown physical disc boot result";
    }
}
