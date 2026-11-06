/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_CE_PROFILE_H
#define KUI_SMASH_CE_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kui/smash_menu_collection.h"

#define KUI_SMASH_CE_PROFILE_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_CE_PROFILE_MAGIC1 UINT32_C(0x31504543)
#define KUI_SMASH_CE_PROFILE_BYTES 80u
#define KUI_SMASH_CE_PROFILE_EMBEDDED_VERSION 2u
#define KUI_SMASH_CE_PROFILE_PERSISTENT 3u
#define KUI_SMASH_CE_PROFILE_RELOCATED 4u
#define KUI_SMASH_CE_VC2_PACKAGE "0:/KUI/apps/games/retail-smash-vc2.kui"
#define KUI_SMASH_CE_VC2_HELPER "0:/KUI/apps/games/smash-vc2-vmu-helper.bin"
#define KUI_SMASH_CE_VC2_SESSION "0:/KUI/apps/games/smash-vc2-vmu-session.bin"
#define KUI_SMASH_CE_SWIRL_PACKAGE "0:/KUI/apps/games/retail-smash-swirl.kui"
#define KUI_SMASH_CE_SWIRL_HELPER "0:/KUI/apps/games/smash-swirl-vmu-helper.bin"
#define KUI_SMASH_CE_SWIRL_SESSION "0:/KUI/apps/games/smash-swirl-vmu-session.bin"

/* An immutable module owner, separate from each launch's card and physical
 * capsule map. The helper CRC covers the complete padded48KiB image.
 * Version2 requires the typed embedded-helper descriptor. Version1 may use
 * its matching external helper when no embedded descriptor is present. */
struct kui_smash_ce_profile {
    uint32_t magic0,magic1,version,bytes,kind,virtual_slot,flags;
    uint32_t kernel_fad,kernel_sectors,kernel_bytes,body_bytes,body_crc32,prefix_crc32;
    uint32_t ram_end,helper_bytes,helper_crc32,reserve_begin,reserve_bytes,reserved,crc32;
};
_Static_assert(sizeof(struct kui_smash_ce_profile)==80u,"Smash CE profile ABI");
bool kui_smash_ce_profile_valid(const struct kui_smash_ce_profile *);
/* 1 unique valid owner;0 absent;-1 damaged, duplicate or wrong expected kind.
 * expected_kind must explicitly name VC2 or Swirl. Output clears on failure. */
int kui_smash_ce_profile_find(const void *,size_t,unsigned expected_kind,
    struct kui_smash_ce_profile *out);
bool kui_smash_ce_profile_paths(unsigned kind,const char **module,
    const char **helper,const char **session);
#endif
