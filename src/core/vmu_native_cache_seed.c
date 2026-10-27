/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_cache_seed.h"
#include "kui/vmu_native_resolution_cache.h"
#include <string.h>
static const uint8_t magic[8]={'K','U','I','N','V','S','0','1'};
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t value) {
    p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);p[2]=(uint8_t)(value>>16);p[3]=(uint8_t)(value>>24);
}
static bool span(const void *p,size_t n) {return p && n<=UINTPTR_MAX-(uintptr_t)p;}
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    if(!span(a,an) || !span(b,bn)) return true;
    uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b;
    if(aa<bb+bn && bb<aa+an) return true;
#if UINTPTR_MAX>UINT32_MAX
    if(aa>UINT32_MAX || bb>UINT32_MAX) return false;
#endif
    uint32_t ap=(uint32_t)aa&UINT32_C(0x1fffffff),bp=(uint32_t)bb&UINT32_C(0x1fffffff);
    uint32_t ar=(uint32_t)aa&UINT32_C(0xe0000000),br=(uint32_t)bb&UINT32_C(0xe0000000);
    return (ar==0u || ar==UINT32_C(0x80000000) || ar==UINT32_C(0xa0000000)) &&
        (br==0u || br==UINT32_C(0x80000000) || br==UINT32_C(0xa0000000)) &&
        ap>=UINT32_C(0x0c000000) && ap<UINT32_C(0x0d000000) &&
        bp>=UINT32_C(0x0c000000) && bp<UINT32_C(0x0d000000) &&
        an<=UINT32_C(0x0d000000)-ap && bn<=UINT32_C(0x0d000000)-bp && ap<bp+bn && bp<ap+an;
}
static bool zero(const uint8_t *p,size_t n) {for(size_t i=0;i<n;i++) if(p[i]) return false;return true;}
void kui_vmu_native_cache_seed_template(uint8_t out[KUI_VMU_NATIVE_CACHE_SEED_BYTES]) {
    if(!out) return;
    memset(out,0,KUI_VMU_NATIVE_CACHE_SEED_BYTES);memcpy(out,magic,8u);
    put32(out+8u,1u);put32(out+12u,KUI_VMU_NATIVE_CACHE_SEED_BYTES);
    put32(out+20u,KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES);
}
static bool valid(const void *slot,size_t bytes) {
    if(bytes!=KUI_VMU_NATIVE_CACHE_SEED_BYTES || !span(slot,bytes)) return false;
    const uint8_t *p=slot;
    if(memcmp(p,magic,8u) || get32(p+8u)!=1u || get32(p+12u)!=bytes ||
       get32(p+16u)>1u || get32(p+20u)!=KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES ||
       !zero(p+56u,8u) || !zero(p+1088u,32u)) return false;
    if(!get32(p+16u)) return zero(p+24u,1088u-24u);
    uint8_t digest[32];
    return kui_vmu_native_resolution_cache_record_digest(p+64u,1024u,digest) && !memcmp(p+24u,digest,32u);
}
size_t kui_vmu_native_cache_seed_find(const void *module,size_t bytes) {
    if(!span(module,bytes) || bytes<KUI_VMU_NATIVE_CACHE_SEED_BYTES) return SIZE_MAX;
    const uint8_t *p=module;size_t found=SIZE_MAX;
    for(size_t i=0;i<=bytes-KUI_VMU_NATIVE_CACHE_SEED_BYTES;i++) {
        if(p[i]!=magic[0] || memcmp(p+i,magic,8u) || !valid(p+i,KUI_VMU_NATIVE_CACHE_SEED_BYTES)) continue;
        if(found!=SIZE_MAX) return SIZE_MAX;
        found=i;
    }
    return found;
}
bool kui_vmu_native_cache_seed_fill(void *slot,size_t bytes,const void *wire,size_t wire_bytes) {
    if(!valid(slot,bytes)) return false;
    uint8_t digest[32];
    if(!kui_vmu_native_resolution_cache_record_digest(wire,wire_bytes,digest)) return false;
    uint8_t result[KUI_VMU_NATIVE_CACHE_SEED_BYTES];
    kui_vmu_native_cache_seed_template(result);put32(result+16u,1u);
    memcpy(result+24u,digest,32u);memcpy(result+64u,wire,1024u);
    memcpy(slot,result,sizeof(result));return true;
}
bool kui_vmu_native_cache_seed_read(const void *slot,size_t bytes,
    const uint8_t **wire,const uint8_t **digest) {
    if(!wire || !digest || overlap(wire,sizeof(*wire),digest,sizeof(*digest)) ||
       overlap(wire,sizeof(*wire),slot,bytes) || overlap(digest,sizeof(*digest),slot,bytes) ||
       !valid(slot,bytes)) return false;
    const uint8_t *p=slot;if(get32(p+16u)!=1u) return false;
    *wire=p+64u;*digest=p+24u;return true;
}
