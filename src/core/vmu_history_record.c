/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_history_record.h"
#include "kui/hash.h"
#include <string.h>

static uint32_t get32(const uint8_t *p) {
    return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t get64(const uint8_t *p) {return get32(p)|(uint64_t)get32(p+4u)<<32;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(i*8u));}
static void put64(uint8_t *p,uint64_t n) {put32(p,(uint32_t)n);put32(p+4u,(uint32_t)(n>>32));}
static bool zero(const void *data,size_t bytes) {
    const uint8_t *p=data;while(bytes--)if(*p++)return false;return true;
}
static bool valid(const struct kui_vmu_history_record *r) {
    char key[KUI_VMU_GAME_CARD_KEY_BYTES];
    return r && r->sequence && r->source_pair && r->source_pair<=9999u &&
        r->reason>=KUI_VMU_HISTORY_MANUAL && r->reason<=KUI_VMU_HISTORY_AFTER_RETURN &&
        !zero(r->source_identity,16u) && !zero(r->sha256,32u) &&
        memchr(r->product,0,sizeof(r->product)) && kui_vmu_game_card_key(r->product,key) &&
        !memcmp(key,r->product,sizeof(key));
}
bool kui_vmu_history_record_encode(const struct kui_vmu_history_record *r,uint8_t out[512]) {
    if(!r || !out)return false;
    uintptr_t a=(uintptr_t)r,b=(uintptr_t)out;
    if(a>UINTPTR_MAX-sizeof(*r) || b>UINTPTR_MAX-512u ||
       !(a+sizeof(*r)<=b || b+512u<=a))return false;
    memset(out,0,512u);
    if(!valid(r))return false;
    memcpy(out,"KUIVMUH1",8u);put32(out+8u,1u);put32(out+12u,512u);
    put64(out+16u,r->sequence);put64(out+24u,r->timestamp_seconds);
    put32(out+32u,r->source_pair);put32(out+36u,r->reason);
    memcpy(out+40u,r->source_identity,16u);memcpy(out+56u,r->sha256,32u);
    put32(out+88u,r->image_crc32);memcpy(out+92u,r->product,12u);
    put32(out+508u,kui_crc32(0,out,508u));return true;
}
bool kui_vmu_history_record_decode(const void *data,size_t bytes,struct kui_vmu_history_record *out) {
    if(!out)return false;
    struct kui_vmu_history_record r={0};const uint8_t *p=data;
    if(!p || bytes!=512u || memcmp(p,"KUIVMUH1",8u) || get32(p+8u)!=1u ||
       get32(p+12u)!=512u || !zero(p+104u,404u) || get32(p+508u)!=kui_crc32(0,p,508u))return false;
    r.sequence=get64(p+16u);r.timestamp_seconds=get64(p+24u);
    r.source_pair=get32(p+32u);r.reason=get32(p+36u);r.image_crc32=get32(p+88u);
    memcpy(r.source_identity,p+40u,16u);memcpy(r.sha256,p+56u,32u);memcpy(r.product,p+92u,12u);
    if(!valid(&r))return false;
    *out=r;return true;
}
