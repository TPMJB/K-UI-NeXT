/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_sd_profile.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include <string.h>

static int zero(const uint8_t *p,size_t bytes) {
    for(size_t i=0;i<bytes;i++) if(p[i]) return 0;
    return 1;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t get64(const uint8_t *p) {return get32(p)|(uint64_t)get32(p+4u)<<32;}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(8u*i));
}
static void put64(uint8_t *p,uint64_t value) {
    put32(p,(uint32_t)value);put32(p+4u,(uint32_t)(value>>32));
}
static uint32_t wire_crc(const uint8_t *wire) {
    const uint8_t empty[4]={0};
    uint32_t crc=kui_retail_crc32(0,wire,16u);
    crc=kui_retail_crc32(crc,empty,4u);
    return kui_retail_crc32(crc,wire+20u,KUI_VMU_SD_PROFILE_WIRE_BYTES-20u);
}

static int validate(const struct kui_vmu_sd_profile *p,int require_hash) {
    if(!p) return KUI_VMU_SD_PROFILE_ARGUMENT;
    if(p->transport!=KUI_VMU_SD_PROFILE_TRANSPORT ||
       p->image_bytes!=KUI_VMU_SD_PROFILE_IMAGE_BYTES || p->high_capacity>1u)
        return KUI_VMU_SD_PROFILE_FORMAT;
    if(!p->card_sectors || p->card_sectors>(UINT64_C(1)<<32) ||
       (!p->high_capacity && p->card_sectors>(UINT64_C(1)<<23)))
        return KUI_VMU_SD_PROFILE_IDENTITY;
    unsigned ones=0;
    for(unsigned i=0;i<16u;i++) ones+=p->cid[i]==0xffu;
    if(zero(p->cid,16u) || ones==16u || zero(p->image_id,16u) ||
       (require_hash && zero(p->sha256,32u))) return KUI_VMU_SD_PROFILE_IDENTITY;
    if(p->partition_first>=p->partition_end || p->partition_end>p->card_sectors ||
       !p->extent_count || p->extent_count>KUI_VMU_SD_PROFILE_MAX_EXTENTS)
        return KUI_VMU_SD_PROFILE_MAPPING;
    uint32_t logical=0;
    for(unsigned i=0;i<p->extent_count;i++) {
        const struct kui_vmu_sd_profile_extent *e=&p->extents[i];
        uint64_t end=(uint64_t)e->lba+e->blocks;
        if(e->logical_block!=logical || !e->blocks || e->blocks>256u-logical ||
           e->lba<p->partition_first || end>p->partition_end || end>(UINT64_C(1)<<32))
            return KUI_VMU_SD_PROFILE_MAPPING;
        for(unsigned j=0;j<i;j++) {
            const struct kui_vmu_sd_profile_extent *other=&p->extents[j];
            uint64_t other_end=(uint64_t)other->lba+other->blocks;
            if(e->lba<other_end && other->lba<end) return KUI_VMU_SD_PROFILE_MAPPING;
        }
        if(i && (uint64_t)p->extents[i-1u].lba+p->extents[i-1u].blocks==e->lba)
            return KUI_VMU_SD_PROFILE_MAPPING;
        logical+=e->blocks;
    }
    if(logical!=KUI_VMU_SD_PROFILE_BLOCKS) return KUI_VMU_SD_PROFILE_MAPPING;
    for(unsigned i=p->extent_count;i<KUI_VMU_SD_PROFILE_MAX_EXTENTS;i++)
        if(p->extents[i].logical_block || p->extents[i].lba || p->extents[i].blocks)
            return KUI_VMU_SD_PROFILE_FORMAT;
    return KUI_VMU_SD_PROFILE_OK;
}
int kui_vmu_sd_profile_validate(const struct kui_vmu_sd_profile *p) {return validate(p,1);}

int kui_vmu_sd_profile_encode(const struct kui_vmu_sd_profile *p,uint8_t wire[1536]) {
    if(!wire) return KUI_VMU_SD_PROFILE_ARGUMENT;
    memset(wire,0,KUI_VMU_SD_PROFILE_WIRE_BYTES);
    int result=validate(p,1);if(result) return result;
    memcpy(wire,"KUIVMUR1",8u);put32(wire+8u,1u);put32(wire+12u,1536u);
    put32(wire+20u,p->transport);put32(wire+24u,p->image_bytes);put32(wire+28u,p->high_capacity);
    put64(wire+32u,p->card_sectors);put64(wire+40u,p->partition_first);put64(wire+48u,p->partition_end);
    memcpy(wire+56u,p->cid,16u);memcpy(wire+72u,p->image_id,16u);memcpy(wire+88u,p->sha256,32u);
    put32(wire+120u,p->extent_count);
    for(unsigned i=0;i<p->extent_count;i++) {
        uint8_t *out=wire+128u+i*12u;
        put32(out,p->extents[i].logical_block);put32(out+4u,p->extents[i].lba);put32(out+8u,p->extents[i].blocks);
    }
    for(unsigned i=0;i<256u;i++) put32(wire+320u+i*4u,p->block_crc[i]);
    put32(wire+16u,wire_crc(wire));return KUI_VMU_SD_PROFILE_OK;
}

int kui_vmu_sd_profile_decode(struct kui_vmu_sd_profile *out,const uint8_t *wire,size_t bytes) {
    if(!out) return KUI_VMU_SD_PROFILE_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!wire || bytes!=KUI_VMU_SD_PROFILE_WIRE_BYTES) return KUI_VMU_SD_PROFILE_ARGUMENT;
    if(memcmp(wire,"KUIVMUR1",8u) || get32(wire+8u)!=1u || get32(wire+12u)!=1536u)
        return KUI_VMU_SD_PROFILE_WIRE;
    if(get32(wire+16u)!=wire_crc(wire)) return KUI_VMU_SD_PROFILE_CRC;
    if(!zero(wire+124u,4u) || !zero(wire+1344u,192u)) return KUI_VMU_SD_PROFILE_FORMAT;
    out->transport=get32(wire+20u);out->image_bytes=get32(wire+24u);out->high_capacity=get32(wire+28u);
    out->card_sectors=get64(wire+32u);out->partition_first=get64(wire+40u);out->partition_end=get64(wire+48u);
    memcpy(out->cid,wire+56u,16u);memcpy(out->image_id,wire+72u,16u);memcpy(out->sha256,wire+88u,32u);
    out->extent_count=get32(wire+120u);
    for(unsigned i=0;i<16u;i++) {
        const uint8_t *in=wire+128u+i*12u;
        out->extents[i]=(struct kui_vmu_sd_profile_extent){get32(in),get32(in+4u),get32(in+8u)};
    }
    for(unsigned i=0;i<256u;i++) out->block_crc[i]=get32(wire+320u+i*4u);
    int result=validate(out,1);if(result) memset(out,0,sizeof(*out));
    return result;
}

int kui_vmu_sd_profile_map_lba(const struct kui_vmu_sd_profile *p,uint16_t block,uint32_t *out) {
    if(!out) return KUI_VMU_SD_PROFILE_ARGUMENT;
    *out=0u;int result=validate(p,0);if(result) return result;
    if(block>=256u) return KUI_VMU_SD_PROFILE_MAPPING;
    for(unsigned i=0;i<p->extent_count;i++) {
        const struct kui_vmu_sd_profile_extent *e=&p->extents[i];
        if(block>=e->logical_block && block-e->logical_block<e->blocks) {
            *out=e->lba+(block-e->logical_block);return KUI_VMU_SD_PROFILE_OK;
        }
    }
    return KUI_VMU_SD_PROFILE_MAPPING;
}

static void clear_hashes(struct kui_vmu_sd_profile *p) {
    memset(p->sha256,0,sizeof(p->sha256));memset(p->block_crc,0,sizeof(p->block_crc));
}
int kui_vmu_sd_profile_collect_hashes(struct kui_vmu_sd_profile *p,
    kui_vmu_sd_profile_read_block read,void *context) {
    if(!p) return KUI_VMU_SD_PROFILE_ARGUMENT;
    clear_hashes(p);int result=validate(p,0);if(result) return result;
    if(!read) return KUI_VMU_SD_PROFILE_ARGUMENT;
    struct kui_sha256 hash;uint8_t block[512];kui_sha256_init(&hash);
    for(unsigned i=0;i<256u;i++) {
        if(read(context,(uint16_t)i,block)) {clear_hashes(p);return KUI_VMU_SD_PROFILE_IO;}
        p->block_crc[i]=kui_retail_crc32(0,block,sizeof(block));
        kui_sha256_update(&hash,block,sizeof(block));
    }
    kui_sha256_digest(&hash,p->sha256);return KUI_VMU_SD_PROFILE_OK;
}
int kui_vmu_sd_profile_verify_image(const struct kui_vmu_sd_profile *p,
    kui_vmu_sd_profile_read_block read,void *context) {
    int result=validate(p,1);if(result) return result;
    if(!read) return KUI_VMU_SD_PROFILE_ARGUMENT;
    struct kui_sha256 hash;uint8_t block[512],digest[32];kui_sha256_init(&hash);
    for(unsigned i=0;i<256u;i++) {
        if(read(context,(uint16_t)i,block)) return KUI_VMU_SD_PROFILE_IO;
        if(kui_retail_crc32(0,block,sizeof(block))!=p->block_crc[i]) return KUI_VMU_SD_PROFILE_VERIFY;
        kui_sha256_update(&hash,block,sizeof(block));
    }
    kui_sha256_digest(&hash,digest);
    return memcmp(digest,p->sha256,32u)?KUI_VMU_SD_PROFILE_VERIFY:KUI_VMU_SD_PROFILE_OK;
}
