/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_MENU_RETURN_H
#define KUI_SMASH_MENU_RETURN_H
#include "kui/smash_menu_loader.h"
#define KUI_SMASH_MENU_RETURN_SEED_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_MENU_RETURN_SEED_MAGIC1 UINT32_C(0x3154524d)
#define KUI_SMASH_MENU_RETURN_SEED_BYTES 368u
#define KUI_SMASH_MENU_RETURN_CONFIGURED 1u
#define KUI_SMASH_MENU_RETURN_CONTEXT 3u
#define KUI_SMASH_MENU_RETURN_CAPSULE_PATH "0:/KUI/apps/games/smash-menu-outer.cap"
struct kui_smash_menu_return_seed {
    uint32_t magic0,magic1,version,bytes,flags,reserved[2],crc32;
    struct kui_smash_menu_loader_seed outer;
    uint8_t argv[256],ip_tail[16];
};
_Static_assert(sizeof(struct kui_smash_menu_return_seed)==368u,"Smash return seed ABI");
_Static_assert(offsetof(struct kui_smash_menu_return_seed,outer)==32u,"Smash return capsule offset");
_Static_assert(offsetof(struct kui_smash_menu_return_seed,argv)==96u,"Smash return argv offset");
_Static_assert(offsetof(struct kui_smash_menu_return_seed,ip_tail)==352u,"Smash return IP offset");
bool kui_smash_menu_return_seed_blank(const struct kui_smash_menu_return_seed *);
size_t kui_smash_menu_return_seed_find(const void *,size_t);
/* The outer capsule contains its prepared native-capsule plan and a blank
 * return seed. Configure only the launch copy after the outer CRC is known.
 * A fresh child receives this plan with both old return fields cleared. */
bool kui_smash_menu_return_seed_configure(struct kui_smash_menu_return_seed *,
    const struct kui_smash_menu_loader_seed *outer);
bool kui_smash_menu_return_seed_valid(const struct kui_smash_menu_return_seed *);
/* Capture the current original GINS parser output, never the previous
 * native selection's five-token argument seed. Failure changes no output. */
bool kui_smash_menu_return_seed_capture(struct kui_smash_menu_return_seed *,
    const uint8_t argv[256],const uint8_t ip_tail[16]);
/* Late publication follows complete original-menu verification. */
bool kui_smash_menu_return_seed_apply(void *context,size_t context_bytes,uint8_t ip_tail[16],
    const struct kui_smash_menu_return_seed *);
#endif
