/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_MENU_LOADER_CAPABILITY_H
#define KUI_SMASH_MENU_LOADER_CAPABILITY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define KUI_SMASH_MENU_LOADER_CAP_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_MENU_LOADER_CAP_MAGIC1 UINT32_C(0x314c4e4d)
#define KUI_SMASH_MENU_LOADER_CAP_BYTES 32u
#define KUI_SMASH_MENU_LOADER_CAP_WORDS \
    KUI_SMASH_MENU_LOADER_CAP_MAGIC0,KUI_SMASH_MENU_LOADER_CAP_MAGIC1,2u,32u,7u,224u,304u,64u
struct kui_smash_menu_loader_capability {
    uint32_t magic0,magic1,version,bytes,flags,loader_seed_bytes,native_seed_bytes,reserved;
};
_Static_assert(sizeof(struct kui_smash_menu_loader_capability)==32u,"Smash menu loader capability ABI");
static inline uint32_t kui_smash_menu_loader_cap_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline bool kui_smash_menu_loader_capability_valid(const void *data,size_t bytes) {
    if(!data || bytes<32u)return false;
    const uint8_t *p=data;
    return kui_smash_menu_loader_cap_u32(p)==KUI_SMASH_MENU_LOADER_CAP_MAGIC0 &&
        kui_smash_menu_loader_cap_u32(p+4u)==KUI_SMASH_MENU_LOADER_CAP_MAGIC1 &&
        kui_smash_menu_loader_cap_u32(p+12u)==32u && kui_smash_menu_loader_cap_u32(p+24u)==304u &&
        ((kui_smash_menu_loader_cap_u32(p+8u)==1u && kui_smash_menu_loader_cap_u32(p+16u)==1u &&
          kui_smash_menu_loader_cap_u32(p+20u)==64u && !kui_smash_menu_loader_cap_u32(p+28u)) ||
         (kui_smash_menu_loader_cap_u32(p+8u)==2u && kui_smash_menu_loader_cap_u32(p+16u)==7u &&
          kui_smash_menu_loader_cap_u32(p+20u)==224u && kui_smash_menu_loader_cap_u32(p+28u)==64u));
}
/* Exact unique aligned capability, with changed/ambiguous prefixes refused.
 * No host alignment or byte order is inherited from the input pointer. */
static inline int kui_smash_menu_loader_capability_find(const void *data,size_t bytes,size_t *offset) {
    if(!data || bytes<8u || (uintptr_t)data>UINTPTR_MAX-bytes)return 0;
    const uint8_t *p=data;size_t found=0;bool have=false;
    for(size_t at=0;at<=bytes-8u;at++) {
        if(kui_smash_menu_loader_cap_u32(p+at)!=KUI_SMASH_MENU_LOADER_CAP_MAGIC0 ||
           kui_smash_menu_loader_cap_u32(p+at+4u)!=KUI_SMASH_MENU_LOADER_CAP_MAGIC1)continue;
        if(have || (at&3u) || !kui_smash_menu_loader_capability_valid(p+at,bytes-at))return -1;
        have=true;found=at;
    }
    if(have && offset)*offset=found;
    return have?1:0;
}
#endif
