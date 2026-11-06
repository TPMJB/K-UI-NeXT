/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_profile.h"
#include "kui/retail_image.h"
#include <string.h>

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
bool kui_smash_ce_profile_valid(const struct kui_smash_ce_profile *p) {
    if(!p || p->magic0!=KUI_SMASH_CE_PROFILE_MAGIC0 || p->magic1!=KUI_SMASH_CE_PROFILE_MAGIC1 ||
       (p->version!=1u && p->version!=KUI_SMASH_CE_PROFILE_EMBEDDED_VERSION) ||
       p->bytes!=sizeof(*p) || p->reserved || !p->helper_crc32 ||
       p->helper_bytes!=49152u || p->reserve_begin!=UINT32_C(0x8cfc4000) ||
       p->reserve_bytes!=49152u)return false;
    const bool swirl=p->kind==KUI_SMASH_MENU_CHILD_SWIRL;
    if(!swirl && p->kind!=KUI_SMASH_MENU_CHILD_VC2)return false;
    if(p->virtual_slot!=(swirl?1u:2u) ||
       p->flags!=(KUI_SMASH_CE_PROFILE_PERSISTENT|(swirl?KUI_SMASH_CE_PROFILE_RELOCATED:0u)) ||
       p->kernel_fad!=(swirl?493127u:529805u) || p->kernel_sectors!=(swirl?893u:899u) ||
       p->kernel_bytes!=(swirl?UINT32_C(0x1be800):UINT32_C(0x1c1800)) ||
       p->body_bytes!=(swirl?UINT32_C(0x1be000):UINT32_C(0x1c1000)) ||
       p->body_crc32!=(swirl?UINT32_C(0x14a27ac2):UINT32_C(0x6d9c21a3)) ||
       p->prefix_crc32!=(swirl?UINT32_C(0x7e29bc00):UINT32_C(0xa253bfef)) ||
       p->ram_end!=(swirl?UINT32_C(0x8cfb8000):UINT32_C(0x8cfc0000)))return false;
    struct kui_smash_ce_profile copy=*p;copy.crc32=0u;
    return p->crc32==kui_retail_crc32(0u,&copy,sizeof(copy));
}
int kui_smash_ce_profile_find(const void *data,size_t bytes,unsigned kind,
    struct kui_smash_ce_profile *out) {
    if(out)memset(out,0,sizeof(*out));
    if(!data || (kind!=KUI_SMASH_MENU_CHILD_VC2 && kind!=KUI_SMASH_MENU_CHILD_SWIRL) ||
       (uintptr_t)data>UINTPTR_MAX-bytes)return -1;
    if(bytes<4u)return 0;
    const uint8_t *p=data;bool found=false;struct kui_smash_ce_profile candidate={0};
    for(size_t at=0u;at<=bytes-4u;++at) {
        if(word(p+at)!=KUI_SMASH_CE_PROFILE_MAGIC0 &&
           word(p+at)!=KUI_SMASH_CE_PROFILE_MAGIC1)continue;
        /* A second magic alone is evidence of a damaged owner too. Other
         * KUI records share magic0, so inspect its CE-specific second word. */
        if(word(p+at)==KUI_SMASH_CE_PROFILE_MAGIC1) {
            if(at<4u || word(p+at-4u)!=KUI_SMASH_CE_PROFILE_MAGIC0)return -1;
            continue;
        }
        if(at+8u>bytes || word(p+at+4u)!=KUI_SMASH_CE_PROFILE_MAGIC1)continue;
        if(found || (at&3u) || sizeof(candidate)>bytes-at)return -1;
        memcpy(&candidate,p+at,sizeof(candidate));
        if(candidate.kind!=kind || !kui_smash_ce_profile_valid(&candidate))return -1;
        found=true;
    }
    if(found && out)*out=candidate;
    return found?1:0;
}
bool kui_smash_ce_profile_paths(unsigned kind,const char **module,
    const char **helper,const char **session) {
    if(module)*module=NULL;
    if(helper)*helper=NULL;
    if(session)*session=NULL;
    if(kind!=KUI_SMASH_MENU_CHILD_VC2 && kind!=KUI_SMASH_MENU_CHILD_SWIRL)return false;
    bool swirl=kind==KUI_SMASH_MENU_CHILD_SWIRL;
    if(module)*module=swirl?KUI_SMASH_CE_SWIRL_PACKAGE:KUI_SMASH_CE_VC2_PACKAGE;
    if(helper)*helper=swirl?KUI_SMASH_CE_SWIRL_HELPER:KUI_SMASH_CE_VC2_HELPER;
    if(session)*session=swirl?KUI_SMASH_CE_SWIRL_SESSION:KUI_SMASH_CE_VC2_SESSION;
    return true;
}
