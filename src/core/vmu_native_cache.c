/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_cache.h"
#include "kui/hash.h"
#include <string.h>
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);
}
/* Enumerate fields explicitly: native struct packing is not a file ABI. */
static void fields(uint32_t v[18],const struct kui_vmu_native_discovery *r) {
    const uint32_t values[18]={r->version,r->bytes,r->family,r->image_bytes,
        r->load_address,r->requirements,r->submit,r->resume,r->initializer,
        r->manager_cell,r->manager_initial,r->lock_cell,r->completion_slot,
        r->completion_call,r->consumer,r->gate,r->ready,r->gate_register};
    memcpy(v,values,sizeof(values));
}
static bool schema(const struct kui_vmu_native_discovery *r) {
    return r->version==KUI_VMU_NATIVE_DISCOVERY_VERSION && r->bytes==sizeof(*r) &&
        r->family==KUI_VMU_NATIVE_FAMILY_UNIFORM_V1 &&
        r->requirements==KUI_VMU_NATIVE_REQUIREMENTS &&
        r->image_bytes>=2u && r->image_bytes<=0xc00000u &&
        (r->gate_register==12u || r->gate_register==13u);
}
bool kui_vmu_native_cache_encode(const struct kui_vmu_native_discovery *r,
    void *wire,size_t n) {
    if(!r || !wire || n!=KUI_VMU_NATIVE_CACHE_BYTES || !schema(r)) return false;
    uint8_t result[KUI_VMU_NATIVE_CACHE_BYTES]={0};uint32_t v[18];fields(v,r);
    memcpy(result,"KUINVM01",8u);put32(result+8u,1u);
    put32(result+12u,KUI_VMU_NATIVE_CACHE_BYTES);put32(result+16u,104u);
    for(unsigned i=0;i<18u;i++) put32(result+20u+4u*i,v[i]);
    memcpy(result+92u,r->image_sha256,32u);
    put32(result+124u,kui_crc32(0u,result,124u));
    memcpy(wire,result,sizeof(result));return true;
}
bool kui_vmu_native_cache_decode(struct kui_vmu_native_discovery *r,
    const void *wire,size_t n) {
    if(!r || !wire || n!=KUI_VMU_NATIVE_CACHE_BYTES) return false;
    const uint8_t *p=wire;
    if(memcmp(p,"KUINVM01",8u) || get32(p+8u)!=1u ||
       get32(p+12u)!=n || get32(p+16u)!=104u ||
       get32(p+124u)!=kui_crc32(0u,p,124u)) return false;
    struct kui_vmu_native_discovery decoded={
        .version=get32(p+20u),.bytes=get32(p+24u),.family=get32(p+28u),
        .image_bytes=get32(p+32u),.load_address=get32(p+36u),
        .requirements=get32(p+40u),.submit=get32(p+44u),.resume=get32(p+48u),
        .initializer=get32(p+52u),.manager_cell=get32(p+56u),
        .manager_initial=get32(p+60u),.lock_cell=get32(p+64u),
        .completion_slot=get32(p+68u),.completion_call=get32(p+72u),
        .consumer=get32(p+76u),.gate=get32(p+80u),.ready=get32(p+84u),
        .gate_register=get32(p+88u)};
    memcpy(decoded.image_sha256,p+92u,32u);
    if(!schema(&decoded)) return false;
    *r=decoded;return true;
}
bool kui_vmu_native_cache_load(struct kui_vmu_native_discovery *r,
    const void *wire,size_t n,const void *image,size_t bytes,uint32_t load) {
    /* Check before any scan or output write, including native RAM aliases. */
    if(!r) return false;
    uintptr_t a=(uintptr_t)image,b=(uintptr_t)r;
    if(sizeof(*r)>UINTPTR_MAX-b || bytes>UINTPTR_MAX-a ||
       (b<a+bytes && a<b+sizeof(*r))) return false;
    /* direct RAM aliases can name the same executable on SH4 */
#if UINTPTR_MAX > UINT32_MAX
    if(a<=UINT32_MAX && b<=UINT32_MAX)
#endif
    {
        uint32_t ap=(uint32_t)a&0x1fffffffu,bp=(uint32_t)b&0x1fffffffu;
        uint32_t aa=(uint32_t)a&0xe0000000u,ba=(uint32_t)b&0xe0000000u;
        if((aa==0u || aa==0x80000000u || aa==0xa0000000u) &&
           (ba==0u || ba==0x80000000u || ba==0xa0000000u) &&
           ap>=0x0c000000u && ap<0x0d000000u && bp>=0x0c000000u && bp<0x0d000000u &&
           bytes<=0x0d000000u-ap && sizeof(*r)<=0x0d000000u-bp &&
           ap<bp+sizeof(*r) && bp<ap+bytes) return false;
    }
    struct kui_vmu_native_discovery decoded;
    if(!kui_vmu_native_cache_decode(&decoded,wire,n) ||
       !kui_vmu_native_discovery_cache_valid(&decoded,image,bytes,load)) return false;
    *r=decoded;return true;
}
