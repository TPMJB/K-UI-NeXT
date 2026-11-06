/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cold_image.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include <string.h>
bool kui_cold_image_capable(const void *prefix,size_t bytes,uint32_t image_bytes) {
    uint32_t words[6];
    if(!prefix || bytes<KUI_COLD_IMAGE_CAP_OFFSET+sizeof(words))return false;
    memcpy(words,(const uint8_t *)prefix+KUI_COLD_IMAGE_CAP_OFFSET,sizeof(words));
    return words[0]==UINT32_C(0x4349554b) && words[1]==UINT32_C(0x31305041) &&
        words[2]==1u && words[3]==sizeof(words) && words[4]==image_bytes && words[5]==~image_bytes;
}
static bool header(const struct kui_cold_image_seed *s) {
    return s && s->magic0==KUI_COLD_IMAGE_MAGIC0 && s->magic1==KUI_COLD_IMAGE_MAGIC1 &&
        s->version==1u && s->bytes==sizeof(*s);
}
static uint32_t crc(const struct kui_cold_image_seed *s) {
    const uint32_t zero=0u;
    return kui_retail_crc32(kui_retail_crc32(0u,s,offsetof(struct kui_cold_image_seed,crc32)),&zero,4u);
}
static bool source_span(uint32_t address,uint32_t bytes) {
    uint32_t area=address&UINT32_C(0xe0000000),p=address&UINT32_C(0x1fffffff);
    return (area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        !(address&31u) && bytes && !(bytes&3u) && p>=UINT32_C(0x0c400000) &&
        p<UINT32_C(0x0cc00000) && bytes<=UINT32_C(0x0cc00000)-p;
}
bool kui_cold_image_seed_blank(const struct kui_cold_image_seed *s) {
    return header(s) && !(s->source|s->image_bytes|s->image_crc32|s->crc32);
}
bool kui_cold_image_seed_valid(const struct kui_cold_image_seed *s) {
    return header(s) && source_span(s->source,s->image_bytes) && s->crc32==crc(s);
}
size_t kui_cold_image_seed_find(const void *payload,size_t bytes) {
    if(!payload || bytes>UINTPTR_MAX-(uintptr_t)payload || bytes<sizeof(struct kui_cold_image_seed))return SIZE_MAX;
    const uint8_t *p=payload;size_t found=SIZE_MAX;
    for(size_t i=0u;i<=bytes-sizeof(struct kui_cold_image_seed);i+=4u) {
        struct kui_cold_image_seed s;memcpy(&s,p+i,sizeof(s));
        if(!header(&s))continue;
        if(!kui_cold_image_seed_blank(&s) || found!=SIZE_MAX)return SIZE_MAX;
        found=i;
    }
    return found;
}
bool kui_cold_image_seed_configure(struct kui_cold_image_seed *s,uint32_t source,uint32_t bytes,uint32_t sum) {
    if(!kui_cold_image_seed_blank(s) || !source_span(source,bytes))return false;
    struct kui_cold_image_seed result=*s;
    result.source=source;result.image_bytes=bytes;result.image_crc32=sum;result.crc32=crc(&result);
    *s=result;return true;
}
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b;
    if(!a || !b || an>UINTPTR_MAX-aa || bn>UINTPTR_MAX-bb)return true;
    if(aa<bb+bn && bb<aa+an)return true;
#if UINTPTR_MAX>UINT32_MAX
    if(aa>UINT32_MAX || bb>UINT32_MAX)return false;
#endif
    uint32_t ap=(uint32_t)aa&UINT32_C(0x1fffffff),bp=(uint32_t)bb&UINT32_C(0x1fffffff);
    bool ar=ap>=UINT32_C(0x0c000000) && ap<UINT32_C(0x0d000000);
    bool br=bp>=UINT32_C(0x0c000000) && bp<UINT32_C(0x0d000000);
    return ar && br && ap<(uint64_t)bp+bn && bp<(uint64_t)ap+an;
}
bool kui_cold_image_restore(const struct kui_cold_image_seed *s,void *out,size_t capacity,
    const void *source,size_t source_capacity,uint32_t expected_crc32,
    bool (*owner)(uint32_t,const uint8_t [32])) {
    if(!kui_cold_image_seed_valid(s) || !owner || s->image_bytes>capacity ||
       s->image_bytes>source_capacity || s->image_crc32!=expected_crc32 ||
       overlap(out,s->image_bytes,source,s->image_bytes) || overlap(out,s->image_bytes,s,sizeof(*s)))return false;
#if UINTPTR_MAX<=UINT32_MAX
    if(((uintptr_t)source&UINT32_C(0x1fffffff))!=(s->source&UINT32_C(0x1fffffff)))return false;
#endif
    if(kui_retail_crc32(0u,source,s->image_bytes)!=expected_crc32)return false;
    struct kui_sha256 hash;uint8_t digest[32];kui_sha256_init(&hash);
    kui_sha256_update(&hash,source,s->image_bytes);kui_sha256_digest(&hash,digest);
    if(!owner(s->image_bytes,digest))return false;
    memcpy(out,source,s->image_bytes);return true;
}
