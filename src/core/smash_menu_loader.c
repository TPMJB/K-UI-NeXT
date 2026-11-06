/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_loader.h"
#include "kui/smash_native_menu.h"
#include "kui/retail_image.h"
#include <string.h>
static bool header(const struct kui_smash_menu_loader_seed *s) {
    return s && s->magic0==KUI_SMASH_MENU_LOADER_SEED_MAGIC0 &&
        s->magic1==KUI_SMASH_MENU_LOADER_SEED_MAGIC1 && s->version==1u && s->bytes==sizeof(*s);
}
static bool shape(const struct kui_smash_menu_loader_seed *s) {
    if(!s || s->flags || s->payload_bytes<304u || s->payload_bytes>UINT32_C(0x1f0000) ||
       s->padded_bytes<s->payload_bytes || s->padded_bytes-s->payload_bytes>=512u ||
       (s->padded_bytes&511u) || s->block_count!=s->padded_bytes/512u ||
       !s->block_count || !s->first_blocks || s->first_blocks>s->block_count ||
       !s->first_lba || (uint64_t)s->first_lba+s->first_blocks>UINT64_C(0x100000000) ||
       (!!s->second_lba!=(s->first_blocks<s->block_count)) ||
       (uint64_t)s->second_lba+s->block_count-s->first_blocks>UINT64_C(0x100000000) ||
       (s->menu_seed_offset&3u) || s->menu_seed_offset>s->payload_bytes-304u)return false;
    if(s->second_lba && (uint64_t)s->first_lba<(uint64_t)s->second_lba+s->block_count-s->first_blocks &&
       (uint64_t)s->second_lba<(uint64_t)s->first_lba+s->first_blocks)return false;
    return true;
}
static uint32_t crc(const struct kui_smash_menu_loader_seed *s) {
    const uint32_t zero=0;
    return kui_retail_crc32(kui_retail_crc32(0,s,offsetof(struct kui_smash_menu_loader_seed,seed_crc32)),&zero,4u);
}
bool kui_smash_menu_loader_seed_blank(const struct kui_smash_menu_loader_seed *s) {
    if(!header(s))return false;
    const uint32_t *words=(const uint32_t *)s;
    for(unsigned i=4u;i<16u;i++)if(words[i])return false;
    return true;
}
size_t kui_smash_menu_loader_seed_find(const void *payload,size_t bytes) {
    if(!payload || bytes<64u || (uintptr_t)payload>UINTPTR_MAX-bytes)return SIZE_MAX;
    const uint8_t *data=payload;size_t found=SIZE_MAX;
    for(size_t at=0;at<=bytes-64u;at++) {
        struct kui_smash_menu_loader_seed s;uint32_t id[4];memcpy(id,data+at,sizeof(id));
        if(id[0]!=KUI_SMASH_MENU_LOADER_SEED_MAGIC0 || id[1]!=KUI_SMASH_MENU_LOADER_SEED_MAGIC1 ||
           id[2]!=1u || id[3]!=64u)continue;
        memcpy(&s,data+at,sizeof(s));
        if(found!=SIZE_MAX || (at&3u) || !kui_smash_menu_loader_seed_blank(&s))return SIZE_MAX;
        found=at;
    }
    return found;
}
bool kui_smash_menu_loader_seed_seal(struct kui_smash_menu_loader_seed *s) {
    if(!shape(s))return false;
    s->magic0=KUI_SMASH_MENU_LOADER_SEED_MAGIC0;s->magic1=KUI_SMASH_MENU_LOADER_SEED_MAGIC1;
    s->version=1u;s->bytes=sizeof(*s);s->seed_crc32=crc(s);return true;
}
bool kui_smash_menu_loader_seed_valid(const struct kui_smash_menu_loader_seed *s) {
    return header(s) && shape(s) && s->seed_crc32==crc(s);
}
