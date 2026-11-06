/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_collection.h"
#include "kui/retail_image.h"
#include <string.h>
static bool header(const struct kui_smash_menu_collection *s) {
    return s && s->magic0==KUI_SMASH_MENU_COLLECTION_MAGIC0 &&
        s->magic1==KUI_SMASH_MENU_COLLECTION_MAGIC1 && s->version==1u &&
        s->bytes==sizeof(*s) && s->count==KUI_SMASH_MENU_CHILD_COUNT &&
        s->flags==KUI_SMASH_MENU_COLLECTION_FLAGS && !s->reserved;
}
static uint32_t crc(const struct kui_smash_menu_collection *s) {
    const uint32_t zero=0;
    return kui_retail_crc32(kui_retail_crc32(0,s,offsetof(struct kui_smash_menu_collection,crc32)),&zero,4u);
}
static bool disjoint(const struct kui_smash_menu_loader_seed *a,
    const struct kui_smash_menu_loader_seed *b) {
    const uint32_t al[2]={a->first_lba,a->second_lba},bl[2]={b->first_lba,b->second_lba};
    const uint32_t an[2]={a->first_blocks,a->block_count-a->first_blocks};
    const uint32_t bn[2]={b->first_blocks,b->block_count-b->first_blocks};
    for(unsigned i=0;i<2u;i++)for(unsigned j=0;j<2u;j++)
        if(an[i] && bn[j] && (uint64_t)al[i]<(uint64_t)bl[j]+bn[j] &&
           (uint64_t)bl[j]<(uint64_t)al[i]+an[i])return false;
    return true;
}
static bool shape(const struct kui_smash_menu_collection *s) {
    if(!header(s))return false;
    for(unsigned i=0;i<KUI_SMASH_MENU_CHILD_COUNT;i++) {
        if(!kui_smash_menu_loader_seed_valid(&s->plans[i]) ||
           s->plans[i].handoff_crc32!=s->plans[0].handoff_crc32 ||
           s->plans[i].raw_crc32!=s->plans[0].raw_crc32)return false;
        for(unsigned j=0;j<i;j++)if(!disjoint(&s->plans[i],&s->plans[j]))return false;
    }
    return true;
}
bool kui_smash_menu_collection_blank(const struct kui_smash_menu_collection *s) {
    if(!header(s) || s->crc32)return false;
    const uint8_t *p=(const void *)s->plans;
    for(size_t i=0;i<sizeof(s->plans);i++)if(p[i])return false;
    return true;
}
size_t kui_smash_menu_collection_find(const void *data,size_t bytes) {
    if(!data || bytes<16u || (uintptr_t)data>UINTPTR_MAX-bytes)return SIZE_MAX;
    const uint8_t *p=data;size_t found=SIZE_MAX;
    for(size_t at=0;at<=bytes-16u;at++) {
        /* The two magic words also occur in the linked validator's literal
         * pool. Only a complete typed wire header identifies a seed record,
         * matching the native-child and return-seed discovery contracts. */
        uint32_t id[4];memcpy(id,p+at,sizeof(id));
        if(id[0]!=KUI_SMASH_MENU_COLLECTION_MAGIC0 || id[1]!=KUI_SMASH_MENU_COLLECTION_MAGIC1 ||
           id[2]!=1u || id[3]!=sizeof(struct kui_smash_menu_collection))continue;
        struct kui_smash_menu_collection s;
        if(found!=SIZE_MAX || (at&3u) || sizeof(s)>bytes-at)return SIZE_MAX;
        memcpy(&s,p+at,sizeof(s));
        if(!kui_smash_menu_collection_blank(&s))return SIZE_MAX;
        found=at;
    }
    return found;
}
bool kui_smash_menu_collection_seal(struct kui_smash_menu_collection *s) {
    if(!shape(s))return false;
    s->crc32=crc(s);return true;
}
bool kui_smash_menu_collection_valid(const struct kui_smash_menu_collection *s) {
    return shape(s) && s->crc32==crc(s);
}
bool kui_smash_menu_collection_seal_valid(const struct kui_smash_menu_collection *s) {
    return header(s) && s->crc32==crc(s);
}
