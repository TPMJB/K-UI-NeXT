/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_return.h"
#include "kui/storage_policy.h"
#include <string.h>

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t get64(const uint8_t *p) {return get32(p)|(uint64_t)get32(p+4u)<<32;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(i*8u));}
static void put64(uint8_t *p,uint64_t n) {put32(p,(uint32_t)n);put32(p+4u,(uint32_t)(n>>32));}
static uint32_t crc32(uint32_t previous,const void *data,size_t bytes) {
    uint32_t crc=~previous;const uint8_t *p=data;
    while(bytes--) {
        crc^=*p++;
        for(unsigned bit=0;bit<8u;bit++)crc=(crc>>1)^((0u-(crc&1u))&UINT32_C(0xedb88320));
    }
    return ~crc;
}
static uint32_t wire_crc(const uint8_t *wire) {
    static const uint8_t zeros[4]={0};
    uint32_t crc=crc32(0,wire,16u);crc=crc32(crc,zeros,4u);
    return crc32(crc,wire+20u,KUI_NATIVE_RETURN_WIRE_BYTES-20u);
}
static bool separate(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=UINTPTR_MAX-an && y<=UINTPTR_MAX-bn && (x+an<=y || y+bn<=x);
}
static bool header_valid(const uint8_t h[64],uint32_t file_bytes) {
    uint32_t payload=get32(h+16u),memory=get32(h+28u);
    if(memcmp(h,"KUIRUN1\0",8u) || get32(h+8u)!=1u || get32(h+12u)!=64u ||
       get32(h+20u)!=KUI_NATIVE_RETURN_ADDRESS || get32(h+24u)!=KUI_NATIVE_RETURN_ADDRESS ||
       !payload || payload<4u || payload>UINT32_C(0x400000) || (payload&3u) ||
       memory<payload || memory>UINT32_C(0x800000) || (memory&3u) ||
       payload+64u!=file_bytes || get32(h+36u) || get32(h+52u) || get32(h+56u) ||
       get32(h+60u)!=crc32(0,h,60u))return false;
    for(unsigned i=40u;i<52u;i++)if(!((h[i]>='0' && h[i]<='9') || (h[i]>='a' && h[i]<='f')))return false;
    return true;
}
bool kui_native_return_plan_valid(const struct kui_native_return_plan *p) {
    if(!p || p->version!=KUI_NATIVE_RETURN_VERSION || p->bytes!=sizeof(*p) ||
       !header_valid(p->header,p->file_bytes) || !p->extent_count ||
       p->extent_count>KUI_NATIVE_RETURN_EXTENTS || !p->card_sectors ||
       p->card_sectors>UINT64_C(0x100000000) || p->partition_first>=p->partition_end ||
       p->partition_end>p->card_sectors)return false;
    uint32_t blocks=(p->file_bytes+511u)/512u,at=0;
    for(unsigned i=0;i<KUI_NATIVE_RETURN_EXTENTS;i++) {
        const struct kui_native_return_extent *e=&p->extents[i];
        if(i>=p->extent_count) {if(e->file_block || e->card_lba || e->blocks)return false;continue;}
        if(e->file_block!=at || !e->blocks || e->blocks>blocks-at ||
           e->card_lba<p->partition_first || (uint64_t)e->card_lba+e->blocks>p->partition_end)return false;
        for(unsigned j=0;j<i;j++) {
            const struct kui_native_return_extent *old=&p->extents[j];
            if((uint64_t)e->card_lba< (uint64_t)old->card_lba+old->blocks &&
               (uint64_t)old->card_lba<(uint64_t)e->card_lba+e->blocks)return false;
        }
        at+=e->blocks;
    }
    return at==blocks;
}
bool kui_native_return_encode(uint8_t wire[KUI_NATIVE_RETURN_WIRE_BYTES],const struct kui_native_return_plan *p) {
    if(!wire || !kui_native_return_plan_valid(p) ||
       !separate(wire,KUI_NATIVE_RETURN_WIRE_BYTES,p,sizeof(*p)))return false;
    memset(wire,0,KUI_NATIVE_RETURN_WIRE_BYTES);memcpy(wire,"KUINRT01",8u);
    put32(wire+8u,KUI_NATIVE_RETURN_VERSION);put32(wire+12u,KUI_NATIVE_RETURN_WIRE_BYTES);
    put32(wire+24u,p->file_bytes);put32(wire+28u,p->extent_count);
    put64(wire+32u,p->card_sectors);put64(wire+40u,p->partition_first);put64(wire+48u,p->partition_end);
    memcpy(wire+64u,p->header,64u);
    for(unsigned i=0;i<p->extent_count;i++) {
        uint8_t *e=wire+128u+i*12u;put32(e,p->extents[i].file_block);
        put32(e+4u,p->extents[i].card_lba);put32(e+8u,p->extents[i].blocks);
    }
    put32(wire+16u,wire_crc(wire));return true;
}
bool kui_native_return_decode(struct kui_native_return_plan *out,const void *data,size_t bytes) {
    const uint8_t *wire=data;
    if(!out || !wire || bytes!=KUI_NATIVE_RETURN_WIRE_BYTES || memcmp(wire,"KUINRT01",8u) ||
       get32(wire+8u)!=KUI_NATIVE_RETURN_VERSION || get32(wire+12u)!=bytes ||
       get32(wire+16u)!=wire_crc(wire) || get32(wire+20u) || get32(wire+56u) || get32(wire+60u))return false;
    unsigned count=get32(wire+28u);if(!count || count>KUI_NATIVE_RETURN_EXTENTS)return false;
    for(size_t i=128u+count*12u;i<bytes;i++)if(wire[i])return false;
    struct kui_native_return_plan p={.version=KUI_NATIVE_RETURN_VERSION,.bytes=sizeof(p),
        .file_bytes=get32(wire+24u),.extent_count=count,.card_sectors=get64(wire+32u),
        .partition_first=get64(wire+40u),.partition_end=get64(wire+48u)};
    memcpy(p.header,wire+64u,64u);
    for(unsigned i=0;i<count;i++) {
        const uint8_t *e=wire+128u+i*12u;
        p.extents[i]=(struct kui_native_return_extent){get32(e),get32(e+4u),get32(e+8u)};
    }
    if(!kui_native_return_plan_valid(&p))return false;
    *out=p;return true;
}
bool kui_native_return_lease_safe(const struct kui_native_return_plan *p,uint32_t lease,uint32_t bytes) {
    if(!kui_native_return_plan_valid(p))return false;
    uint32_t area=lease&UINT32_C(0xe0000000),physical=lease&UINT32_C(0x1fffffff);
    uint32_t begin=KUI_NATIVE_RETURN_ADDRESS&UINT32_C(0x1fffffff),payload=get32(p->header+16u);
    if((area && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
       !bytes || (physical&31u) || physical<UINT32_C(0x0c010020) ||
       bytes>UINT32_C(0x0d000000)-physical || physical>=UINT32_C(0x0d000000))return false;
    return physical>=begin+payload || physical+bytes<=begin;
}
enum kui_native_return_result kui_native_return_load(const struct kui_native_return_plan *p,
    uint32_t lease,uint32_t lease_bytes,void *destination,size_t capacity,
    kui_native_return_read read,void *context) {
    if(!destination || !read)return KUI_NATIVE_RETURN_ARGUMENT;
    if(!kui_native_return_plan_valid(p))return KUI_NATIVE_RETURN_MAP;
    uint32_t payload=get32(p->header+16u);
    if(capacity<payload || !separate(destination,payload,p,sizeof(*p)))return KUI_NATIVE_RETURN_ARGUMENT;
    if(!kui_native_return_lease_safe(p,lease,lease_bytes))return KUI_NATIVE_RETURN_LEASE;
    uint8_t block[512];uint32_t crc=0u,copied=0u;
    for(unsigned i=0;i<p->extent_count;i++)for(uint32_t j=0;j<p->extents[i].blocks;j++) {
        const struct kui_native_return_extent *e=&p->extents[i];
        if(!read(context,e->card_lba+j,block))return KUI_NATIVE_RETURN_IO;
        uint32_t position=(e->file_block+j)*512u;
        if(!position && memcmp(block,p->header,64u))return KUI_NATIVE_RETURN_CHANGED;
        unsigned skip=position?0u:64u;
        uint32_t remaining=p->file_bytes-position;
        unsigned used=remaining<512u?remaining:512u;
        if(used<=skip)return KUI_NATIVE_RETURN_MAP;
        used-=skip;
        memcpy((uint8_t *)destination+copied,block+skip,used);
        crc=crc32(crc,block+skip,used);copied+=used;
    }
    if(copied!=payload || crc!=get32(p->header+32u))return KUI_NATIVE_RETURN_CRC;
    if(!kui_storage_patch_boot(destination,payload,KUI_STORAGE_SCI))return KUI_NATIVE_RETURN_HEADER;
    return KUI_NATIVE_RETURN_OK;
}
