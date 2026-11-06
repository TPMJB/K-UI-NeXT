/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_MENU_LOADER_H
#define KUI_SMASH_MENU_LOADER_H
#define KUI_SMASH_MENU_LOADER_P1 0x8c008300
#define KUI_SMASH_MENU_LOADER_P2 0xac008300
#define KUI_SMASH_MENU_LOADER_LIMIT 0xac00b800
#define KUI_SMASH_MENU_LOADER_STACK 0xac00c000
#define KUI_SMASH_MENU_LOADER_RAW_P1 0x8c008100
#define KUI_SMASH_MENU_LOADER_RAW_P2 0xac008100
#define KUI_SMASH_MENU_LOADER_RAW_BYTES 512
#ifndef KUI_SMASH_MENU_LOADER
#define KUI_SMASH_MENU_LOADER 0
#endif
#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define KUI_SMASH_MENU_LOADER_SEED_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_MENU_LOADER_SEED_MAGIC1 UINT32_C(0x31444c4d)
#define KUI_SMASH_MENU_LOADER_SEED_BYTES 64u
#define KUI_SMASH_MENU_LOADER_PACKAGE "0:/KUI/apps/games/retail-smash-menu.kui"
#define KUI_SMASH_MENU_LOADER_SEED_BLANK_WORDS \
    KUI_SMASH_MENU_LOADER_SEED_MAGIC0,KUI_SMASH_MENU_LOADER_SEED_MAGIC1,1u,64u, \
    0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u,0u
struct kui_smash_menu_loader_seed {
    uint32_t magic0,magic1,version,bytes;
    uint32_t payload_bytes,padded_bytes,payload_crc32,menu_seed_offset;
    uint32_t first_lba,first_blocks,second_lba,block_count;
    uint32_t handoff_crc32,raw_crc32,flags,seed_crc32;
};
_Static_assert(sizeof(struct kui_smash_menu_loader_seed)==64u,"Smash menu loader seed ABI");
bool kui_smash_menu_loader_seed_blank(const struct kui_smash_menu_loader_seed *);
size_t kui_smash_menu_loader_seed_find(const void *,size_t);
/* Code CRCs may be zero during app preparation. The authenticated cold stage
 * fills both and seals again before installing this surviving runtime. */
bool kui_smash_menu_loader_seed_seal(struct kui_smash_menu_loader_seed *);
bool kui_smash_menu_loader_seed_valid(const struct kui_smash_menu_loader_seed *);
#endif
#endif
