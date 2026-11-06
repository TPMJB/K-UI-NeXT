/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_return.h"
#include "kui/smash_menu_return_loader.h"
#include "kui/smash_native_child.h"
#include "kui/retail_image.h"
#include <string.h>
static bool disjoint(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=UINTPTR_MAX-n && y<=UINTPTR_MAX-m && (x+n<=y || y+m<=x);
}
static bool header(const struct kui_smash_menu_return_seed *s) {
    return s && s->magic0==KUI_SMASH_MENU_RETURN_SEED_MAGIC0 &&
        s->magic1==KUI_SMASH_MENU_RETURN_SEED_MAGIC1 && s->version==1u &&
        s->bytes==sizeof(*s) && !s->reserved[0] && !s->reserved[1];
}
static bool zeroes(const void *data,size_t bytes) {
    const uint8_t *p=data;
    while(bytes--)if(*p++)return false;
    return true;
}
static uint32_t seed_crc(const struct kui_smash_menu_return_seed *s) {
    const uint32_t zero=0u;
    uint32_t crc=kui_retail_crc32(0,s,offsetof(struct kui_smash_menu_return_seed,crc32));
    crc=kui_retail_crc32(crc,&zero,4u);
    return kui_retail_crc32(crc,&s->outer,sizeof(*s)-offsetof(struct kui_smash_menu_return_seed,outer));
}
static bool plan(const struct kui_smash_menu_loader_seed *s) {
    return kui_smash_menu_loader_seed_valid(s) && !s->handoff_crc32 && !s->raw_crc32 &&
        s->payload_bytes>=368u && s->menu_seed_offset<=s->payload_bytes-368u;
}
static bool returned(const uint8_t argv[256],const uint8_t ip[16]) {
    static const char *const names[]={"\\1ST_READ.BIN","\\manifest.bin"};
    bool match=zeroes(argv,256u);
    for(unsigned i=0;i<2u;i++) {
        size_t n=strlen(names[i])+1u;
        if(!memcmp(argv,names[i],n) && zeroes(argv+n,256u-n))match=true;
    }
    if(!match || ip[12]!=1u || ip[13]!=4u)return false;
    for(unsigned i=0;i<12u;i++)if(ip[i]<32u || ip[i]>126u)return false;
    return true;
}
bool kui_smash_menu_return_seed_blank(const struct kui_smash_menu_return_seed *s) {
    return header(s) && !s->flags && !s->crc32 && zeroes(&s->outer,sizeof(*s)-32u);
}
size_t kui_smash_menu_return_seed_find(const void *payload,size_t bytes) {
    if(!payload || bytes<368u || (uintptr_t)payload>UINTPTR_MAX-bytes)return SIZE_MAX;
    const uint8_t *data=payload;size_t found=SIZE_MAX;
    for(size_t at=0;at<=bytes-368u;at++) {
        uint32_t id[4];memcpy(id,data+at,sizeof(id));
        if(id[0]!=KUI_SMASH_MENU_RETURN_SEED_MAGIC0 || id[1]!=KUI_SMASH_MENU_RETURN_SEED_MAGIC1 ||
           id[2]!=1u || id[3]!=368u)continue;
        struct kui_smash_menu_return_seed s;memcpy(&s,data+at,sizeof(s));
        if(found!=SIZE_MAX || (at&3u) || !kui_smash_menu_return_seed_blank(&s))return SIZE_MAX;
        found=at;
    }
    return found;
}
bool kui_smash_menu_return_seed_configure(struct kui_smash_menu_return_seed *s,
    const struct kui_smash_menu_loader_seed *outer) {
    if(!s || !outer || !plan(outer))return false;
    struct kui_smash_menu_return_seed next={
        .magic0=KUI_SMASH_MENU_RETURN_SEED_MAGIC0,.magic1=KUI_SMASH_MENU_RETURN_SEED_MAGIC1,
        .version=1u,.bytes=sizeof(next),.flags=KUI_SMASH_MENU_RETURN_CONFIGURED,.outer=*outer};
    next.crc32=seed_crc(&next);*s=next;return true;
}
bool kui_smash_menu_return_seed_valid(const struct kui_smash_menu_return_seed *s) {
    return header(s) && plan(&s->outer) && s->crc32==seed_crc(s) &&
        ((s->flags==KUI_SMASH_MENU_RETURN_CONFIGURED && zeroes(s->argv,272u)) ||
         (s->flags==KUI_SMASH_MENU_RETURN_CONTEXT && returned(s->argv,s->ip_tail)));
}
bool kui_smash_menu_return_seed_capture(struct kui_smash_menu_return_seed *s,
    const uint8_t argv[256],const uint8_t ip[16]) {
    if(!s || !argv || !ip || !disjoint(s,sizeof(*s),argv,256u) ||
       !disjoint(s,sizeof(*s),ip,16u) || !kui_smash_menu_return_seed_valid(s) || !returned(argv,ip))return false;
    struct kui_smash_menu_return_seed next=*s;
    memcpy(next.argv,argv,256u);memcpy(next.ip_tail,ip,16u);next.flags=KUI_SMASH_MENU_RETURN_CONTEXT;
    next.crc32=seed_crc(&next);*s=next;return true;
}
bool kui_smash_menu_return_seed_apply(void *context,size_t bytes,uint8_t ip[16],
    const struct kui_smash_menu_return_seed *s) {
    if(!context || !ip || bytes!=8192u || !s || !disjoint(context,bytes,ip,16u) ||
       !disjoint(context,bytes,s,sizeof(*s)) || !disjoint(ip,16u,s,sizeof(*s)) ||
       !kui_smash_menu_return_seed_valid(s) || s->flags!=KUI_SMASH_MENU_RETURN_CONTEXT ||
       kui_retail_crc32(0,context,bytes)!=KUI_SMASH_NATIVE_CONTEXT_CRC32)return false;
    memcpy((uint8_t *)context+256u,s->argv,256u);memcpy(ip,s->ip_tail,16u);return true;
}
uint32_t kui_smash_menu_return_runtime_crc(uint32_t code,
    const struct kui_smash_menu_return_runtime *r) {
    const uint32_t zero=0u;
    uint32_t crc=kui_retail_crc32(code,r,offsetof(struct kui_smash_menu_return_runtime,code_crc32));
    crc=kui_retail_crc32(crc,&zero,4u);
    crc=kui_retail_crc32(crc,&r->raw_crc32,4u);
#if KUI_SMASH_CARD_SESSION
    /* The live authority is appended only in the collection-session ABI.
     * Authenticate it before the old worker can be retired or any copy of
     * it is written into a newly authenticated outer capsule. */
    crc=kui_retail_crc32(crc,&r->card_receipt,sizeof(r->card_receipt));
#endif
#if KUI_SMASH_PHASE_DIAGNOSTIC
    crc=kui_retail_crc32(crc,&r->phase_video,sizeof(r->phase_video));
#endif
    return crc;
}
