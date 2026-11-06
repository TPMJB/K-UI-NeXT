/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_child.h"
#include "kui/retail_image.h"
#include <string.h>
const char *kui_smash_ce_child_folder(unsigned p) {
    return p==KUI_SMASH_MENU_CHILD_VC2?"VIRTCOP2":p==KUI_SMASH_MENU_CHILD_SWIRL?"SWIRL":NULL;
}
const char *kui_smash_ce_child_bootfile(unsigned p) {
    return p==KUI_SMASH_MENU_CHILD_VC2?"VIRTCOP2/SAMPLE.BIN":
        p==KUI_SMASH_MENU_CHILD_SWIRL?"SWIRL/SAMPLE.BIN":NULL;
}
static bool header(const struct kui_smash_ce_child_seed *s) {
    if(!s || s->magic0!=KUI_SMASH_CE_CHILD_MAGIC0 || s->magic1!=KUI_SMASH_CE_CHILD_MAGIC1 ||
       s->version!=1u || s->bytes!=sizeof(*s) || !kui_smash_ce_child_folder(s->profile) || s->reserved)return false;
    for(unsigned i=0;i<4u;i++)if(s->reserved_tail[i])return false;
    return true;
}
static bool ip_valid(unsigned p,const uint8_t ip[16]) {
    const char *folder=kui_smash_ce_child_folder(p);
    if(!folder || !ip || ip[12]!=1u || ip[13]!=4u)return false;
    size_t n=strlen(folder);
    if(memcmp(ip,folder,n))return false;
    for(size_t i=n;i<12u;i++)if(ip[i]!=' ')return false;
    return true;
}
static uint32_t crc(const struct kui_smash_ce_child_seed *s) {
    const uint32_t zero=0;
    return kui_retail_crc32(kui_retail_crc32(kui_retail_crc32(0,s,
        offsetof(struct kui_smash_ce_child_seed,crc32)),&zero,4u),s->ip_tail,
        sizeof(*s)-offsetof(struct kui_smash_ce_child_seed,ip_tail));
}
bool kui_smash_ce_child_blank(const struct kui_smash_ce_child_seed *s) {
    if(!header(s) || s->flags || s->crc32)return false;
    for(unsigned i=0;i<16u;i++)if(s->ip_tail[i])return false;
    return true;
}
size_t kui_smash_ce_child_find(const void *data,size_t bytes,unsigned p) {
    if(!data || !kui_smash_ce_child_folder(p) || bytes<16u || (uintptr_t)data>UINTPTR_MAX-bytes)return SIZE_MAX;
    const uint8_t *b=data;size_t found=SIZE_MAX;
    for(size_t at=0;at<=bytes-16u;at++) {
        /* A linked comparison-literal pair is not a typed64-byte record. */
        uint32_t id[4];memcpy(id,b+at,sizeof(id));
        if(id[0]!=KUI_SMASH_CE_CHILD_MAGIC0 || id[1]!=KUI_SMASH_CE_CHILD_MAGIC1 ||
           id[2]!=1u || id[3]!=sizeof(struct kui_smash_ce_child_seed))continue;
        struct kui_smash_ce_child_seed s;
        if(found!=SIZE_MAX || (at&3u) || sizeof(s)>bytes-at)return SIZE_MAX;
        memcpy(&s,b+at,sizeof(s));
        if(!kui_smash_ce_child_blank(&s) || s.profile!=p)return SIZE_MAX;
        found=at;
    }
    return found;
}
unsigned kui_smash_ce_child_identify(const uint8_t argv[256],const uint8_t ip[16]) {
    static const char *const commands[2]={"\\VIRTCOP2\\SAMPLE.BIN","\\SWIRL\\SAMPLE.BIN"};
    if(!argv || !ip)return KUI_SMASH_MENU_CHILD_COUNT;
    for(unsigned p=KUI_SMASH_MENU_CHILD_VC2;p<=KUI_SMASH_MENU_CHILD_SWIRL;p++) {
        size_t n=strlen(commands[p-1u]);
        if(!ip_valid(p,ip) || memcmp(argv,commands[p-1u],n))continue;
        size_t i=n;for(;i<256u;i++)if(argv[i])break;
        if(i==256u)return p;
    }
    return KUI_SMASH_MENU_CHILD_COUNT;
}
bool kui_smash_ce_child_capture(struct kui_smash_ce_child_seed *s,const uint8_t argv[256],const uint8_t ip[16]) {
    if(!kui_smash_ce_child_blank(s) || kui_smash_ce_child_identify(argv,ip)!=s->profile)return false;
    struct kui_smash_ce_child_seed out=*s;
    memcpy(out.ip_tail,ip,16u);out.flags=KUI_SMASH_CE_CHILD_CAPTURED;out.crc32=crc(&out);*s=out;return true;
}
bool kui_smash_ce_child_valid(const struct kui_smash_ce_child_seed *s) {
    return header(s) && s->flags==KUI_SMASH_CE_CHILD_CAPTURED && ip_valid(s->profile,s->ip_tail) && s->crc32==crc(s);
}
bool kui_smash_ce_child_owner(uint32_t bytes,const uint8_t sha[32]) {
    static const uint8_t expected[32]={0x82,0xbb,0x11,0x60,0x81,0x45,0x48,0xf4,
        0x65,0x6b,0x27,0x33,0x53,0xfd,0x61,0x68,0xa6,0x91,0xfb,0x5f,0x21,0x5b,0x77,0x37,
        0x78,0x60,0xff,0x9f,0xd2,0x15,0x6c,0x8a};
    return sha && bytes==KUI_SMASH_CE_SAMPLE_BYTES && !memcmp(sha,expected,sizeof(expected));
}
