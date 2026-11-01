/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_boot.h"
#include "kui/hash.h"
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
    return count && fad>=p->source->first_fad && fad<p->source->end_fad &&
        count<=p->source->end_fad-fad;
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
static void sha256(const uint8_t *data,size_t bytes,uint8_t digest[32]) {
    struct kui_sha256 sha;kui_sha256_init(&sha);kui_sha256_update(&sha,data,bytes);
    kui_sha256_digest(&sha,digest);
}
static enum kui_game_metadata_io_result metadata_sector(void *context,uint32_t lba,
                                                       uint8_t data[2048]) {
    struct preparation *p=context;
    if(lba>UINT32_MAX-150u)return KUI_GAME_METADATA_IO_ERROR;
    uint32_t fad=lba+150u;
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
    return lba<=UINT32_MAX-150u && fad_range(p,lba+150u,count);
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
    uint8_t sector[2048];
    for(uint32_t done=0u;done<bytes;) {
        enum kui_disc_boot_status r=read_fad(p,fad+done/2048u,sector);
        if(r!=KUI_DISC_BOOT_OK)return r;
        if(ip && !done) {
            uint8_t current[32];sha256(sector,sizeof(sector),current);
            if(!p->have_ip || memcmp(current,p->first_ip_sha256,sizeof(current)))
                return KUI_DISC_BOOT_CHANGED;
        }
        uint32_t amount=bytes-done;if(amount>2048u)amount=2048u;
        checked=kui_crc32(checked,sector,amount);kui_sha256_update(&sha,sector,amount);
        done+=amount;
    }
    *crc=checked;kui_sha256_digest(&sha,digest);return KUI_DISC_BOOT_OK;
}
enum kui_disc_boot_status kui_disc_boot_prepare(const struct kui_disc_boot_source *source,
                                               struct kui_disc_boot_plan *out) {
    if(!source || !out || !disjoint(source,sizeof(*source),out,sizeof(*out)))
        return KUI_DISC_BOOT_ARGUMENT;
    memset(out,0,sizeof(*out));
    /* Firmware TOC FADs are 24-bit. Require the observed high-density session
     * and enough room for its IP and first ISO descriptor before any read. */
    if(!source->read_sector || !source->cancelled)return KUI_DISC_BOOT_ARGUMENT;
    if(source->disc_type!=KUI_DISC_BOOT_GD_TYPE)return KUI_DISC_BOOT_MEDIA;
    if(source->first_fad<45150u ||
       source->end_fad>UINT32_C(0x01000000) || source->first_fad>=source->end_fad ||
       source->end_fad-source->first_fad<18u)return KUI_DISC_BOOT_ARGUMENT;
    struct preparation p={.source=source};
    struct kui_disc_boot_plan plan={0};
    const struct kui_game_metadata_ops ops={&p,metadata_sector,metadata_range};
    enum kui_disc_boot_status r=metadata_result(kui_game_metadata_read(&ops,
        source->first_fad-150u,&plan.metadata));
    if(r!=KUI_DISC_BOOT_OK)return r;
    if(plan.metadata.windows_ce)return KUI_DISC_BOOT_WINDOWS_CE;
    if(!plan.metadata.native_gd)return KUI_DISC_BOOT_MEDIA;
    if(plan.metadata.boot_bytes>KUI_DISC_BOOT_MAX_EXEC_BYTES)return KUI_DISC_BOOT_LIMIT;
    uint32_t root_sectors=(plan.metadata.root_bytes+2047u)/2048u;
    uint32_t boot_sectors=(plan.metadata.boot_bytes+2047u)/2048u;
    uint32_t root_fad=plan.metadata.root_lba+150u,boot_fad=plan.metadata.boot_lba+150u;
    /* These ranges have already passed metadata_range. Keep IP/descriptors,
     * the complete directory and executable physically disjoint as well. */
    if(!p.primary_fad || root_fad<=p.primary_fad || boot_fad<=p.primary_fad ||
       overlap(root_fad,root_sectors,boot_fad,boot_sectors))return KUI_DISC_BOOT_ISO;
    r=hash_extent(&p,source->first_fad,KUI_DISC_BOOT_IP_BYTES,true,
        &plan.ip_crc32,plan.ip_sha256);
    if(r!=KUI_DISC_BOOT_OK)return r;
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
