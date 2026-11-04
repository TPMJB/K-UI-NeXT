/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/update_handoff.h"
#include "kui/runtime.h"
#include <string.h>

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t get64(const uint8_t *p) {return get32(p)|(uint64_t)get32(p+4u)<<32;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(8u*i));}
static void put64(uint8_t *p,uint64_t n) {put32(p,(uint32_t)n);put32(p+4u,(uint32_t)(n>>32));}
static bool zero(const uint8_t *p,size_t bytes) {
    for(size_t i=0;i<bytes;i++)if(p[i])return false;
    return true;
}
static bool header(const uint8_t *p) {
    return !memcmp(p,"KUIHOF1",8u) && get32(p+8u)==1u && get32(p+12u)==KUI_UPDATE_HANDOFF_BYTES;
}
static void set_header(uint8_t *p) {
    memcpy(p,"KUIHOF1",8u);put32(p+8u,1u);put32(p+12u,KUI_UPDATE_HANDOFF_BYTES);
}
static bool fields(const struct kui_update_handoff *h) {
    if(!h || h->build[12] || h->transport>=3u || !h->sequence ||
       h->file_bytes<64u+KUI_UPDATE_HANDOFF_BYTES || h->file_bytes>64u+KUI_RUNTIME_MAX_BYTES ||
       (h->file_bytes-64u)%4u || h->memory_bytes<h->file_bytes-64u ||
       h->memory_bytes>KUI_RUNTIME_MAX_MEMORY || h->memory_bytes%4u ||
       h->payload_offset>h->file_bytes-64u-KUI_UPDATE_HANDOFF_BYTES || zero(h->nonce,16u))return false;
    for(unsigned i=0;i<12u;i++)if(!((h->build[i]>='0' && h->build[i]<='9') ||
        (h->build[i]>='a' && h->build[i]<='f')))return false;
    return true;
}
bool kui_update_handoff_encode(uint8_t wire[KUI_UPDATE_HANDOFF_BYTES],const struct kui_update_handoff *h) {
    if(!wire || !fields(h))return false;
    memset(wire,0,KUI_UPDATE_HANDOFF_BYTES);set_header(wire);
    put32(wire+16u,1u);put32(wire+20u,h->transport);put32(wire+24u,~h->transport);
    memcpy(wire+32u,h->build,12u);put64(wire+48u,h->sequence);put64(wire+56u,h->file_bytes);
    memcpy(wire+64u,h->manifest_sha256,32u);memcpy(wire+96u,h->runtime_sha256,32u);
    put32(wire+128u,h->payload_offset);memcpy(wire+132u,h->nonce,16u);
    for(unsigned i=0;i<16u;i++)wire[148u+i]=(uint8_t)~h->nonce[i];
    put32(wire+164u,h->payload_crc32);put32(wire+168u,h->memory_bytes);
    put32(wire+188u,kui_crc32(0,wire,188u));return true;
}
bool kui_update_handoff_decode(const uint8_t wire[KUI_UPDATE_HANDOFF_BYTES],struct kui_update_handoff *h) {
    if(!h)return false;
    memset(h,0,sizeof(*h));
    if(!wire || !header(wire) || get32(wire+16u)!=1u ||
       get32(wire+24u)!=~get32(wire+20u) || !zero(wire+28u,4u) || !zero(wire+44u,4u) ||
       !zero(wire+172u,16u) || get32(wire+188u)!=kui_crc32(0,wire,188u))return false;
    for(unsigned i=0;i<16u;i++)if((unsigned)wire[148u+i]+wire[132u+i]!=255u)return false;
    struct kui_update_handoff decoded={0};
    memcpy(decoded.build,wire+32u,12u);decoded.transport=get32(wire+20u);
    decoded.sequence=get64(wire+48u);decoded.file_bytes=get64(wire+56u);
    memcpy(decoded.manifest_sha256,wire+64u,32u);memcpy(decoded.runtime_sha256,wire+96u,32u);
    decoded.payload_offset=get32(wire+128u);memcpy(decoded.nonce,wire+132u,16u);
    decoded.payload_crc32=get32(wire+164u);decoded.memory_bytes=get32(wire+168u);
    if(!fields(&decoded))return false;
    *h=decoded;return true;
}
bool kui_update_handoff_empty(const void *payload,size_t bytes,uint32_t *offset) {
    if(!payload || bytes>UINT32_MAX)return false;
    const uint8_t *p=payload;unsigned count=0u;uint32_t found=0u;bool empty=false;
    for(size_t i=0;i+16u<=bytes;i++)if(p[i]=='K' && header(p+i)) {
        if(++count>1u)return false;
        found=(uint32_t)i;empty=bytes-i>=KUI_UPDATE_HANDOFF_BYTES && zero(p+i+16u,KUI_UPDATE_HANDOFF_BYTES-16u);
    }
    if(count!=1u || !empty)return false;
    if(offset)*offset=found;
    return true;
}
static void scan_complete(struct kui_update_handoff_scan *s,const uint8_t *p,size_t bytes) {
    for(size_t i=0;i+KUI_UPDATE_HANDOFF_BYTES<=bytes;i++)if(p[i]=='K' && header(p+i)) {
        if(s->count<2u)++s->count;
        if(zero(p+i+16u,KUI_UPDATE_HANDOFF_BYTES-16u) && s->empty<2u)++s->empty;
    }
}
void kui_update_handoff_scan_update(struct kui_update_handoff_scan *s,const void *data,size_t bytes) {
    if(!s || !bytes || !data)return;
    const uint8_t *p=data;uint8_t edge[2u*(KUI_UPDATE_HANDOFF_BYTES-1u)];
    size_t first=bytes<sizeof(s->tail)?bytes:sizeof(s->tail);
    memcpy(edge,s->tail,s->bytes);memcpy(edge+s->bytes,p,first);
    scan_complete(s,edge,s->bytes+first);scan_complete(s,p,bytes);
    if(bytes>=sizeof(s->tail)) {memcpy(s->tail,p+bytes-sizeof(s->tail),sizeof(s->tail));s->bytes=sizeof(s->tail);}
    else {
        size_t keep=s->bytes+bytes>sizeof(s->tail)?sizeof(s->tail)-bytes:s->bytes;
        memmove(s->tail,s->tail+s->bytes-keep,keep);memcpy(s->tail+keep,p,bytes);s->bytes=keep+bytes;
    }
}
bool kui_update_handoff_scan_valid(const struct kui_update_handoff_scan *s) {
    if(!s || s->count!=1u || s->empty!=1u)return false;
    /* A full header stranded at EOF is a truncated second slot. */
    for(size_t i=0;i+16u<=s->bytes;i++)if(s->tail[i]=='K' && header(s->tail+i))return false;
    return true;
}
