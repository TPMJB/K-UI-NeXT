/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_sd_profile.h"
#include "kui/retail_image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fixture {unsigned calls;int fail_at,corrupt_at;};
static struct kui_vmu_sd_profile good;
static uint8_t canonical[1536];

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t value) {for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(8u*i));}
static void repair_crc(uint8_t *wire) {put32(wire+16u,0u);put32(wire+16u,kui_retail_crc32(0,wire,1536u));}
static void assert_zero(const void *bytes,size_t n) {
    const uint8_t *p=bytes;for(size_t i=0;i<n;i++) assert(!p[i]);
}
static void pattern(unsigned block,uint8_t out[512]) {
    for(unsigned i=0;i<512u;i++) out[i]=(uint8_t)(block*17u+i*3u+(i>>4));
}
static int read_block(void *context,uint16_t block,uint8_t out[512]) {
    struct fixture *f=context;assert(block==f->calls && block<256u);++f->calls;
    if((int)block==f->fail_at) return -1;
    pattern(block,out);if((int)block==f->corrupt_at) out[123]^=1u;
    return 0;
}
static struct fixture fixture(void) {return (struct fixture){0u,-1,-1};}
struct raw_fixture {struct fixture reads;struct kui_vmu_sd_profile *profile;};
static int mapped_raw_read(void *context,uint16_t block,uint8_t out[512]) {
    struct raw_fixture *f=context;uint32_t lba=0;
    if(kui_vmu_sd_profile_map_lba(f->profile,block,&lba)) return -1;
    assert(lba==(block<128u?1000u+block:3000u+block-128u));
    return read_block(&f->reads,block,out);
}

static void canonical_roundtrip_and_lba_map(void) {
    memset(&good,0,sizeof(good));
    good.card_sectors=100000u;good.partition_first=100u;good.partition_end=90000u;
    good.transport=1u;good.image_bytes=131072u;good.high_capacity=1u;good.extent_count=2u;
    for(unsigned i=0;i<16u;i++) {good.cid[i]=(uint8_t)(i+1u);good.image_id[i]=(uint8_t)(0x80u+i);}
    good.extents[0]=(struct kui_vmu_sd_profile_extent){0u,1000u,128u};
    good.extents[1]=(struct kui_vmu_sd_profile_extent){128u,3000u,128u};
    struct fixture f=fixture();assert(kui_vmu_sd_profile_collect_hashes(&good,read_block,&f)==0 && f.calls==256u);
    /* Independent Python hashlib/zlib fixture, not a second C hash parser. */
    static const uint8_t sha[32]={
        0x93,0x34,0xba,0xf8,0x78,0x1f,0xa5,0xc8,0xc7,0x25,0x02,0x14,0xc8,0xd5,0xe9,0x3c,
        0xae,0xac,0x6f,0x34,0x03,0xda,0xdc,0xfb,0xff,0x1f,0xd4,0x9e,0x53,0x6c,0xb3,0xab};
    assert(!memcmp(good.sha256,sha,32u));
    assert(kui_vmu_sd_profile_encode(&good,canonical)==0);
    assert(get32(canonical+16u)==0x96df8da4u && good.block_crc[0]==0x99af3ffcu);
    assert(!memcmp(canonical,"KUIVMUR1",8u) && get32(canonical+8u)==1u && get32(canonical+12u)==1536u);
    assert(get32(canonical+20u)==1u && get32(canonical+24u)==131072u && get32(canonical+28u)==1u);
    assert(get32(canonical+32u)==100000u && !get32(canonical+36u));
    assert(!memcmp(canonical+56u,good.cid,16u) && !memcmp(canonical+72u,good.image_id,16u));
    assert(!memcmp(canonical+88u,good.sha256,32u) && get32(canonical+120u)==2u);
    assert(get32(canonical+128u)==0u && get32(canonical+132u)==1000u && get32(canonical+136u)==128u);
    assert(get32(canonical+140u)==128u && get32(canonical+144u)==3000u && get32(canonical+148u)==128u);
    assert_zero(canonical+124u,4u);assert_zero(canonical+152u,168u);assert_zero(canonical+1344u,192u);
    struct kui_vmu_sd_profile decoded;
    assert(kui_vmu_sd_profile_decode(&decoded,canonical,sizeof(canonical))==0);
    assert(!memcmp(&decoded,&good,sizeof(good)) && sizeof(good)<=2048u);
    for(unsigned block=0;block<256u;block++) {
        uint32_t lba=0;assert(kui_vmu_sd_profile_map_lba(&decoded,(uint16_t)block,&lba)==0);
        assert(lba==(block<128u?1000u+block:3000u+block-128u));
    }
    f=fixture();assert(kui_vmu_sd_profile_verify_image(&decoded,read_block,&f)==0 && f.calls==256u);
    /* A raw collector maps this same profile while its digest is still empty. */
    memset(decoded.sha256,0,sizeof(decoded.sha256));memset(decoded.block_crc,0,sizeof(decoded.block_crc));
    struct raw_fixture raw={fixture(),&decoded};
    assert(kui_vmu_sd_profile_collect_hashes(&decoded,mapped_raw_read,&raw)==0 && raw.reads.calls==256u);
    assert(!memcmp(&decoded,&good,sizeof(good)));
    raw.reads=fixture();assert(kui_vmu_sd_profile_verify_image(&decoded,mapped_raw_read,&raw)==0 && raw.reads.calls==256u);
}

static void decode_fails_clear(const uint8_t *wire,size_t bytes,int expected) {
    struct kui_vmu_sd_profile out;memset(&out,0xa5,sizeof(out));
    int result=kui_vmu_sd_profile_decode(&out,wire,bytes);
    assert(result==expected);assert_zero(&out,sizeof(out));
}
static void wire_corruption_and_reserved_bytes(void) {
    uint8_t wire[1537];
    for(unsigned byte=0;byte<1536u;byte++) {
        memcpy(wire,canonical,1536u);wire[byte]^=1u;
        decode_fails_clear(wire,1536u,byte<16u?KUI_VMU_SD_PROFILE_WIRE:KUI_VMU_SD_PROFILE_CRC);
    }
    const unsigned reserved[]={124u,125u,126u,127u,152u,159u,319u,1344u,1500u,1535u};
    for(unsigned i=0;i<sizeof(reserved)/sizeof(reserved[0]);i++) {
        memcpy(wire,canonical,1536u);wire[reserved[i]]=1u;repair_crc(wire);
        decode_fails_clear(wire,1536u,KUI_VMU_SD_PROFILE_FORMAT);
    }
    for(size_t bytes=0;bytes<1536u;bytes++) {
        uint8_t *short_wire=malloc(bytes?bytes:1u);assert(short_wire);
        memcpy(short_wire,canonical,bytes);decode_fails_clear(short_wire,bytes,KUI_VMU_SD_PROFILE_ARGUMENT);free(short_wire);
    }
    memcpy(wire,canonical,1536u);wire[1536]=0;
    decode_fails_clear(wire,1537u,KUI_VMU_SD_PROFILE_ARGUMENT);
    decode_fails_clear(NULL,1536u,KUI_VMU_SD_PROFILE_ARGUMENT);
    memcpy(wire,canonical,1536u);put32(wire+120u,17u);repair_crc(wire);
    decode_fails_clear(wire,1536u,KUI_VMU_SD_PROFILE_MAPPING);
}

static void invalid(struct kui_vmu_sd_profile *p,int expected,int digest_only) {
    uint8_t wire[1536];uint32_t lba=999u;
    assert(kui_vmu_sd_profile_validate(p)==expected);
    memset(wire,0xa5,sizeof(wire));assert(kui_vmu_sd_profile_encode(p,wire)==expected);assert_zero(wire,sizeof(wire));
    if(!digest_only) assert(kui_vmu_sd_profile_map_lba(p,0u,&lba)==expected && lba==0u);
    else assert(kui_vmu_sd_profile_map_lba(p,0u,&lba)==0 && lba==1000u);
    struct fixture f=fixture();assert(kui_vmu_sd_profile_verify_image(p,read_block,&f)==expected && !f.calls);
    if(!digest_only) {
        assert(kui_vmu_sd_profile_collect_hashes(p,read_block,&f)==expected && !f.calls);
        assert_zero(p->sha256,sizeof(p->sha256));assert_zero(p->block_crc,sizeof(p->block_crc));
    }
}
static void identity_and_map_refusals_before_io(void) {
    for(unsigned fault=0;fault<26u;fault++) {
        struct kui_vmu_sd_profile p=good;int expected=KUI_VMU_SD_PROFILE_MAPPING;
        switch(fault) {
        case 0:p.transport=0u;expected=KUI_VMU_SD_PROFILE_FORMAT;break;
        case 1:p.image_bytes--;expected=KUI_VMU_SD_PROFILE_FORMAT;break;
        case 2:p.high_capacity=2u;expected=KUI_VMU_SD_PROFILE_FORMAT;break;
        case 3:p.card_sectors=0u;expected=KUI_VMU_SD_PROFILE_IDENTITY;break;
        case 4:p.card_sectors=(UINT64_C(1)<<32)+1u;expected=KUI_VMU_SD_PROFILE_IDENTITY;break;
        case 5:p.high_capacity=0u;p.card_sectors=(UINT64_C(1)<<23)+1u;expected=KUI_VMU_SD_PROFILE_IDENTITY;break;
        case 6:memset(p.cid,0,16u);expected=KUI_VMU_SD_PROFILE_IDENTITY;break;
        case 7:memset(p.cid,0xff,16u);expected=KUI_VMU_SD_PROFILE_IDENTITY;break;
        case 8:memset(p.image_id,0,16u);expected=KUI_VMU_SD_PROFILE_IDENTITY;break;
        case 9:memset(p.sha256,0,32u);expected=KUI_VMU_SD_PROFILE_IDENTITY;break;
        case 10:p.partition_first=p.partition_end;break;
        case 11:p.partition_end=p.card_sectors+1u;break;
        case 12:p.partition_end=UINT64_MAX;break;
        case 13:p.extent_count=0u;break;
        case 14:p.extent_count=17u;break;
        case 15:p.extents[0].logical_block=1u;break;
        case 16:p.extents[1].logical_block++;break;
        case 17:p.extents[0].blocks=0u;break;
        case 18:p.extents[0].blocks=UINT32_MAX;break;
        case 19:p.extents[1].blocks=127u;break;
        case 20:p.extents[1].blocks=129u;break;
        case 21:p.extents[0].lba=99u;break;
        case 22:p.extents[1].lba=89900u;break;
        case 23:p.extents[1].lba=1100u;break; /* Physical overlap. */
        case 24:p.extents[1].lba=1128u;break; /* Adjacent runs must merge. */
        default:p.extents[2].lba=1u;expected=KUI_VMU_SD_PROFILE_FORMAT;break;
        }
        invalid(&p,expected,fault==9u);
    }
}

static void maximum_fragmentation_and_address_boundaries(void) {
    struct kui_vmu_sd_profile p=good;
    memset(p.extents,0,sizeof(p.extents));p.extent_count=16u;
    for(unsigned i=0;i<16u;i++) p.extents[i]=(struct kui_vmu_sd_profile_extent){i*16u,1000u+i*32u,16u};
    assert(kui_vmu_sd_profile_validate(&p)==0);
    for(unsigned b=0;b<256u;b++) {uint32_t lba;assert(kui_vmu_sd_profile_map_lba(&p,(uint16_t)b,&lba)==0);assert(lba==1000u+(b/16u)*32u+b%16u);}
    for(unsigned i=0;i<16u;i++) p.extents[i].lba=1000u+(15u-i)*32u;
    assert(kui_vmu_sd_profile_validate(&p)==0); /* Physical order need not be ascending. */
    memset(p.extents,0,sizeof(p.extents));p.extent_count=1u;
    p.card_sectors=p.partition_end=UINT64_C(1)<<32;p.partition_first=0u;
    p.extents[0]=(struct kui_vmu_sd_profile_extent){0u,UINT32_MAX-255u,256u};
    uint32_t lba;assert(kui_vmu_sd_profile_validate(&p)==0);
    assert(kui_vmu_sd_profile_map_lba(&p,255u,&lba)==0 && lba==UINT32_MAX);
    p.extents[0].lba++;invalid(&p,KUI_VMU_SD_PROFILE_MAPPING,0);
    p=good;memset(p.extents,0,sizeof(p.extents));p.extent_count=1u;p.high_capacity=0u;
    p.card_sectors=p.partition_end=UINT64_C(1)<<23;p.partition_first=0u;
    p.extents[0]=(struct kui_vmu_sd_profile_extent){0u,(1u<<23)-256u,256u};
    assert(kui_vmu_sd_profile_validate(&p)==0 && kui_vmu_sd_profile_map_lba(&p,255u,&lba)==0 && lba==(1u<<23)-1u);
    p=good;memset(p.extents,0,sizeof(p.extents));p.extent_count=1u;
    p.card_sectors=p.partition_end=256u;p.partition_first=0u;
    p.extents[0]=(struct kui_vmu_sd_profile_extent){0u,0u,256u};
    assert(kui_vmu_sd_profile_map_lba(&p,0u,&lba)==0 && lba==0u);
    assert(kui_vmu_sd_profile_map_lba(&p,256u,&lba)==KUI_VMU_SD_PROFILE_MAPPING && !lba);
    lba=999u;assert(kui_vmu_sd_profile_map_lba(&p,UINT16_MAX,&lba)==KUI_VMU_SD_PROFILE_MAPPING && !lba);
}

static void hash_read_failures_and_stale_data(void) {
    for(unsigned block=0;block<256u;block++) {
        struct kui_vmu_sd_profile p=good,expected=good;
        memset(expected.sha256,0,sizeof(expected.sha256));memset(expected.block_crc,0,sizeof(expected.block_crc));
        struct fixture f=fixture();f.fail_at=(int)block;
        assert(kui_vmu_sd_profile_collect_hashes(&p,read_block,&f)==KUI_VMU_SD_PROFILE_IO && f.calls==block+1u);
        assert(!memcmp(&p,&expected,sizeof(p)));
        p=good;f=fixture();f.fail_at=(int)block;
        assert(kui_vmu_sd_profile_verify_image(&p,read_block,&f)==KUI_VMU_SD_PROFILE_IO && f.calls==block+1u);
        assert(!memcmp(&p,&good,sizeof(p)));
    }
    struct kui_vmu_sd_profile p=good;struct fixture f=fixture();
    p.block_crc[99]^=1u;
    assert(kui_vmu_sd_profile_verify_image(&p,read_block,&f)==KUI_VMU_SD_PROFILE_VERIFY && f.calls==100u);
    p=good;f=fixture();p.sha256[7]^=1u;
    assert(kui_vmu_sd_profile_verify_image(&p,read_block,&f)==KUI_VMU_SD_PROFILE_VERIFY && f.calls==256u);
    p=good;f=fixture();f.corrupt_at=199;
    assert(kui_vmu_sd_profile_verify_image(&p,read_block,&f)==KUI_VMU_SD_PROFILE_VERIFY && f.calls==200u);
    uint8_t changed[512];pattern(199u,changed);changed[123]^=1u;
    p.block_crc[199]=kui_retail_crc32(0,changed,sizeof(changed));f=fixture();f.corrupt_at=199;
    assert(kui_vmu_sd_profile_verify_image(&p,read_block,&f)==KUI_VMU_SD_PROFILE_VERIFY && f.calls==256u);
}

static void invalid_api_arguments(void) {
    uint8_t wire[1536];uint32_t lba=999u;struct fixture f=fixture();
    assert(kui_vmu_sd_profile_validate(NULL)==KUI_VMU_SD_PROFILE_ARGUMENT);
    assert(kui_vmu_sd_profile_encode(NULL,wire)==KUI_VMU_SD_PROFILE_ARGUMENT);assert_zero(wire,sizeof(wire));
    assert(kui_vmu_sd_profile_encode(&good,NULL)==KUI_VMU_SD_PROFILE_ARGUMENT);
    assert(kui_vmu_sd_profile_decode(NULL,canonical,sizeof(canonical))==KUI_VMU_SD_PROFILE_ARGUMENT);
    assert(kui_vmu_sd_profile_map_lba(NULL,0u,&lba)==KUI_VMU_SD_PROFILE_ARGUMENT && !lba);
    assert(kui_vmu_sd_profile_map_lba(&good,0u,NULL)==KUI_VMU_SD_PROFILE_ARGUMENT);
    assert(kui_vmu_sd_profile_collect_hashes(NULL,read_block,&f)==KUI_VMU_SD_PROFILE_ARGUMENT && !f.calls);
    struct kui_vmu_sd_profile p=good;
    assert(kui_vmu_sd_profile_collect_hashes(&p,NULL,NULL)==KUI_VMU_SD_PROFILE_ARGUMENT);
    assert_zero(p.sha256,sizeof(p.sha256));assert_zero(p.block_crc,sizeof(p.block_crc));
    assert(kui_vmu_sd_profile_verify_image(&good,NULL,NULL)==KUI_VMU_SD_PROFILE_ARGUMENT);
}

int main(void) {
    canonical_roundtrip_and_lba_map();wire_corruption_and_reserved_bytes();
    identity_and_map_refusals_before_io();maximum_fragmentation_and_address_boundaries();
    hash_read_failures_and_stale_data();invalid_api_arguments();
    puts("VMU SD profile: canonical wire, bounded extents, identity, hashes and fail-closed reads passed");
    return 0;
}
