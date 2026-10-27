/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_relocate.h"
#include "kui/hash.h"
#include <stdbool.h>
#include <string.h>

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
static bool direct(uintptr_t address,uint32_t *physical) {
#if UINTPTR_MAX > UINT32_MAX
    if(address>UINT32_MAX) return false;
#endif
    uint32_t a=(uint32_t)address,area=a&0xe0000000u;
    *physical=a&0x1fffffffu;
    return (area==0u || area==0x80000000u || area==0xa0000000u) &&
        *physical>=0x0c000000u && *physical<0x0d000000u;
}
static bool valid_span(const void *p,size_t n) {
    return p && n && n<=UINTPTR_MAX-(uintptr_t)p;
}
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t ap=(uintptr_t)a,bp=(uintptr_t)b;uint32_t ac,bc;
    if(ap<bp+bn && bp<ap+an) return true;
    if(!direct(ap,&ac) || !direct(bp,&bc)) return false;
    /* Clip invalid overlong direct spans too, before any output write. */
    if(an>0x0d000000u-ac) an=0x0d000000u-ac;
    if(bn>0x0d000000u-bc) bn=0x0d000000u-bc;
    return ac<bc+bn && bc<ac+an;
}
static bool internal(uint32_t pointer,uint32_t base,uint32_t span) {
    uint32_t area=pointer&0xe0000000u,p=pointer&0x1fffffffu,b=base&0x1fffffffu;
    return (area==0u || area==0x80000000u || area==0xa0000000u) && p>=b && p-b<=span;
}
enum kui_vmu_native_relocate_result kui_vmu_native_relocate(
    const void *image,size_t n,const void *table,size_t bytes,
    void *destination,size_t allocation,uint32_t load) {
    if(!valid_span(image,n) || !valid_span(table,bytes) ||
       !valid_span(destination,allocation) || bytes<KUI_VMU_NATIVE_RELOCATE_HEADER_BYTES)
        return KUI_VMU_NATIVE_RELOCATE_INVALID;
    /* Test even malformed tables against output aliasing before reading them. */
    if(overlap(image,n,destination,allocation) || overlap(table,bytes,destination,allocation))
        return KUI_VMU_NATIVE_RELOCATE_OVERLAP;
    const uint8_t *p=image,*wire=table;
    uint32_t span=get32(wire+20u),base=get32(wire+24u),count=get32(wire+28u);
    if(memcmp(wire,"KUIVRL01",8u) || get32(wire+8u)!=1u ||
       get32(wire+12u)!=KUI_VMU_NATIVE_RELOCATE_HEADER_BYTES || get32(wire+16u)!=n ||
       !n || n>span || span>KUI_VMU_NATIVE_RELOCATE_MAX_SPAN ||
       (span&31u) || !span || count>n/4u || get32(wire+32u)!=bytes ||
       bytes!=KUI_VMU_NATIVE_RELOCATE_HEADER_BYTES+(size_t)count*KUI_VMU_NATIVE_RELOCATE_RECORD_BYTES ||
       base<0x8c000000u || base>=0x8d000000u || (base&31u) || span>0x8d000000u-base)
        return KUI_VMU_NATIVE_RELOCATE_TABLE;
    static const uint8_t zero[4]={0};
    uint32_t crc=kui_crc32(0,wire,36u);
    crc=kui_crc32(crc,zero,sizeof(zero));crc=kui_crc32(crc,wire+40u,bytes-40u);
    if(crc!=get32(wire+36u)) return KUI_VMU_NATIVE_RELOCATE_TABLE;
    if(allocation<span || load<0x8c000000u || load>=0x8d000000u ||
       (load&31u) || span>0x8d000000u-load)
        return KUI_VMU_NATIVE_RELOCATE_INVALID;
    uint32_t dest_physical;
    if(direct((uintptr_t)destination,&dest_physical) && dest_physical!=(load&0x1fffffffu))
        return KUI_VMU_NATIVE_RELOCATE_INVALID;
    struct kui_sha256 hash;uint8_t digest[32];
    kui_sha256_init(&hash);kui_sha256_update(&hash,image,n);kui_sha256_digest(&hash,digest);
    if(memcmp(digest,wire+40u,sizeof(digest))) return KUI_VMU_NATIVE_RELOCATE_IMAGE;
    uint32_t previous=0;
    for(uint32_t i=0;i<count;i++) {
        const uint8_t *record=wire+KUI_VMU_NATIVE_RELOCATE_HEADER_BYTES+(size_t)i*8u;
        uint32_t at=get32(record),expected=get32(record+4u);
        if((at&3u) || at>n || n-at<4u || (i && at<=previous) ||
           !internal(expected,base,span) || get32(p+at)!=expected)
            return KUI_VMU_NATIVE_RELOCATE_TABLE;
        previous=at;
    }
    /* An omitted internal literal would leave a stale pointer after moving.
     * Conservatively require every aligned address-looking word to have an
     * explicit linker-backed record. The build extractor uses the same rule. */
    uint32_t next=0;
    for(size_t at=0;at<=n && n-at>=4u;at+=4u) {
        if(!internal(get32(p+at),base,span)) continue;
        if(next>=count || get32(wire+72u+(size_t)next*8u)!=at)
            return KUI_VMU_NATIVE_RELOCATE_TABLE;
        ++next;
    }
    if(next!=count) return KUI_VMU_NATIVE_RELOCATE_TABLE;
    memcpy(destination,image,n);
    for(uint32_t i=0;i<count;i++) {
        const uint8_t *record=wire+KUI_VMU_NATIVE_RELOCATE_HEADER_BYTES+(size_t)i*8u;
        uint32_t at=get32(record),v=get32(record+4u);
        put32((uint8_t *)destination+at,(v&0xe0000000u)+
            (load&0x1fffffffu)+(v&0x1fffffffu)-(base&0x1fffffffu));
    }
    return KUI_VMU_NATIVE_RELOCATED;
}
