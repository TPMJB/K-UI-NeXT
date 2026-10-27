/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_launch_package.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(n>>(8u*i));
}
static void capability(uint8_t p[64]) {
    /* Independent canonical scalar fixture, not the production encoder. */
    static const uint32_t values[]={1u,64u,2u,4u,1u,0x5e1b611au,0x770d2fedu,
        0x7d83cf24u,2956128u,45000u,547556u,0u,0u,0u};
    memset(p,0,64u);memcpy(p,"KUIVMU04",8u);
    for(unsigned i=0;i<14u;i++) put32(p+8u+4u*i,values[i]);
}
static void toy_capability(uint8_t p[64]) {
    static const uint32_t values[]={1u,64u,1u,4u,1u,0xe25531d1u,0x38ba2868u,
        0xcdc493b3u,748444u,45000u,548634u,0u,0u,0u};
    memset(p,0,64u);memcpy(p,"KUIVMU04",8u);
    for(unsigned i=0;i<14u;i++) put32(p+8u+4u*i,values[i]);
}
static struct kui_retail_manifest silver(void) {
    static const uint32_t starts[]={0u,660u,45000u},ends[]={510u,6610u,549150u};
    struct kui_retail_manifest m={0};
    m.card_sectors=4000000u;m.partition_start=2048u;m.partition_end=4000000u;
    m.track_count=3u;m.extent_count=3u;m.session_lba=45000u;
    m.boot_lba=547556u;m.boot_bytes=2956128u;m.storage_transport=KUI_STORAGE_SCI;
    m.reader=KUI_RETAIL_READER_STANDARD;m.flags=KUI_RETAIL_IMAGE_BOOT_CRC;
    m.boot_crc32=0x7d83cf24u;m.ip_crc32=0x770d2fedu;m.gdi_crc32=0x5e1b611au;
    strcpy(m.title,"Silver");strcpy(m.product,"T15108N");strcpy(m.bootfile,"1ST_READ.BIN");
    uint32_t lba=4096u;
    for(unsigned i=0;i<3u;i++) {
        m.slots[i].track=(struct kui_retail_track){starts[i],ends[i],(uint16_t)(3u+i),1u,i==1u?0u:4u};
        uint32_t n=((ends[i]-starts[i])*2352u+511u)/512u;
        m.slots[3u+i].extent=(struct kui_retail_extent){0u,lba,n};lba+=n;
    }
    assert(kui_retail_manifest_validate(&m)==KUI_GAME_OK);return m;
}
static struct kui_retail_manifest toy(void) {
    static const uint32_t starts[]={0u,7257u,45000u,201900u,219712u,234091u,
        255599u,272017u,282799u,303092u,323352u,341314u,356690u,374201u,377422u};
    static const uint32_t ends[]={7107u,7753u,201750u,219712u,234091u,255599u,
        272017u,282799u,303092u,323352u,341314u,356690u,374201u,377272u,549150u};
    struct kui_retail_manifest m={0};
    m.card_sectors=4000000u;m.partition_start=2048u;m.partition_end=4000000u;
    m.track_count=15u;m.extent_count=15u;m.session_lba=45000u;
    m.boot_lba=548634u;m.boot_bytes=748444u;m.storage_transport=KUI_STORAGE_SCI;
    m.reader=KUI_RETAIL_READER_STANDARD;m.flags=KUI_RETAIL_IMAGE_BOOT_CRC;
    m.boot_crc32=0xcdc493b3u;m.ip_crc32=0x38ba2868u;m.gdi_crc32=0xe25531d1u;
    strcpy(m.title,"Toy");strcpy(m.bootfile,"1ST_READ.BIN");
    uint32_t lba=4096u;
    for(unsigned i=0;i<15u;i++) {
        m.slots[i].track=(struct kui_retail_track){starts[i],ends[i],
            (uint16_t)(15u+i),1u,(i==0u || i==2u || i==14u)?4u:0u};
        uint32_t n=((ends[i]-starts[i])*2352u+511u)/512u;
        m.slots[15u+i].extent=(struct kui_retail_extent){0u,lba,n};lba+=n;
    }
    assert(kui_retail_manifest_validate(&m)==KUI_GAME_OK);return m;
}
static enum kui_vmu_launch_route route(const struct kui_retail_manifest *m,const uint8_t *p) {
    return kui_vmu_launch_package_route(m,0u,true,false,true,p,64u);
}
static void toy_routes(void) {
    uint8_t p[64],silver_p[64],copy[64];toy_capability(p);capability(silver_p);
    struct kui_retail_manifest m=toy(),silver_m=silver(),other=m;
    assert(kui_vmu_launch_toy_candidate(&m));
    assert(kui_vmu_launch_profile_candidate(&m)==1u);
    assert(kui_vmu_launch_profile_candidate(&silver_m)==2u);
    assert(!kui_vmu_launch_toy_candidate(&silver_m));
    assert(!kui_vmu_launch_toy_candidate(NULL));
    assert(kui_vmu_launch_profile_candidate(NULL)==0u);
    assert(kui_vmu_launch_capability_valid(p,64u));assert(route(&m,p)==KUI_VMU_LAUNCH_TOY);
    assert(!kui_vmu_launch_capability_valid(p,63u));assert(!kui_vmu_launch_capability_valid(p,65u));
    assert(route(&m,silver_p)==KUI_VMU_LAUNCH_CAPABILITY);
    assert(route(&silver_m,p)==KUI_VMU_LAUNCH_CAPABILITY);
    assert(kui_vmu_launch_package_route(&m,0u,true,false,false,NULL,0u)==KUI_VMU_LAUNCH_ORDINARY);
    for(unsigned i=0;i<64u;i++) {
        memcpy(copy,p,64u);copy[i]^=1u;
        assert(!kui_vmu_launch_capability_valid(copy,64u));
        assert(route(&m,copy)==KUI_VMU_LAUNCH_CAPABILITY);
    }
    for(unsigned bit=0;bit<32u;bit++)
        assert(kui_vmu_launch_package_route(&m,UINT32_C(1)<<bit,true,false,true,p,64u)==KUI_VMU_LAUNCH_SETTINGS);
    assert(kui_vmu_launch_package_route(&m,0u,false,false,true,p,64u)==KUI_VMU_LAUNCH_SETTINGS);
    assert(kui_vmu_launch_package_route(&m,0u,true,true,true,p,64u)==KUI_VMU_LAUNCH_SETTINGS);
    other.gdi_crc32^=1u;
    assert(kui_vmu_launch_profile_candidate(&other)==0u);
    assert(route(&other,p)==KUI_VMU_LAUNCH_ORDINARY);
    for(unsigned fault=0;fault<12u;fault++) {
        other=m;
        switch(fault) {
        case 0:other.ip_crc32^=1u;break;
        case 1:other.boot_crc32^=1u;break;
        case 2:other.boot_bytes--;break;
        case 3:other.boot_lba++;break;
        case 4:other.session_lba++;break;
        case 5:other.storage_transport=KUI_STORAGE_SCIF;break;
        case 6:other.reader=KUI_RETAIL_READER_ASYNC;break;
        case 7:other.slots[2].track.control|=KUI_RETAIL_TRACK_COOKED;break;
        case 8:other.flags=0u;break;
        case 9:other.flags|=KUI_RETAIL_IMAGE_SCRAMBLED;break;
        case 10:other.slots[15].extent.card_lba=0u;break;
        case 11:other.slots[1].track.end_lba++;break;
        }
        assert(route(&other,p)==KUI_VMU_LAUNCH_MANIFEST);
    }
}
int main(void) {
    uint8_t p[64],copy[64];capability(p);struct kui_retail_manifest m=silver(),other=m;
    assert(kui_vmu_launch_capability_valid(p,64u));assert(route(&m,p)==KUI_VMU_LAUNCH_SILVER);
    assert(!kui_vmu_launch_capability_valid(NULL,64u));
    assert(!kui_vmu_launch_capability_valid(p,63u));assert(!kui_vmu_launch_capability_valid(p,65u));
    for(unsigned i=0;i<64u;i++) {
        memcpy(copy,p,64u);copy[i]^=1u;
        assert(!kui_vmu_launch_capability_valid(copy,64u));
        assert(route(&m,copy)==KUI_VMU_LAUNCH_CAPABILITY);
    }
    assert(kui_vmu_launch_package_route(&m,0u,true,false,false,NULL,0u)==KUI_VMU_LAUNCH_ORDINARY);
    strcpy(other.product,"T00000N");
    assert(kui_vmu_launch_package_route(&other,UINT32_MAX,false,true,true,NULL,0u)==KUI_VMU_LAUNCH_ORDINARY);
    for(unsigned bit=0;bit<32u;bit++)
        assert(kui_vmu_launch_package_route(&m,UINT32_C(1)<<bit,true,false,true,p,64u)==KUI_VMU_LAUNCH_SETTINGS);
    assert(kui_vmu_launch_package_route(&m,0u,false,false,true,p,64u)==KUI_VMU_LAUNCH_SETTINGS);
    assert(kui_vmu_launch_package_route(&m,0u,true,true,true,p,64u)==KUI_VMU_LAUNCH_SETTINGS);
    for(unsigned fault=0;fault<13u;fault++) {
        other=m;
        switch(fault) {
        case 0:other.gdi_crc32^=1u;break;
        case 1:other.ip_crc32^=1u;break;
        case 2:other.boot_crc32^=1u;break;
        case 3:other.boot_bytes--;break;
        case 4:other.boot_lba++;break;
        case 5:other.session_lba++;break;
        case 6:other.storage_transport=KUI_STORAGE_SCIF;break;
        case 7:other.reader=KUI_RETAIL_READER_ASYNC;break;
        case 8:other.slots[2].track.control|=KUI_RETAIL_TRACK_COOKED;break;
        case 9:other.flags=0u;break;
        case 10:other.flags|=KUI_RETAIL_IMAGE_SCRAMBLED;break;
        case 11:other.slots[3].extent.card_lba=0u;break;
        case 12:other.slots[1].track.end_lba++;break;
        }
        assert(route(&other,p)==KUI_VMU_LAUNCH_MANIFEST);
    }
    assert(!memcmp(p,"KUIVMU04",8u));
    toy_routes();
    puts("VMU launch: exact Toy/Silver maps and capabilities, every capability byte, swapped profiles, settings and absent module passed");
    return 0;
}
