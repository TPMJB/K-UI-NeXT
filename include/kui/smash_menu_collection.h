/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_MENU_COLLECTION_H
#define KUI_SMASH_MENU_COLLECTION_H
#include "kui/smash_menu_loader.h"

/* These indices are a wire contract shared by preparation and the surviving
 * original-menu chooser. Each backend remains an independently built capsule. */
enum kui_smash_menu_child_kind {
    KUI_SMASH_MENU_CHILD_NATIVE = 0,
    KUI_SMASH_MENU_CHILD_VC2 = 1,
    KUI_SMASH_MENU_CHILD_SWIRL = 2,
    KUI_SMASH_MENU_CHILD_COUNT = 3
};
#define KUI_SMASH_MENU_COLLECTION_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_MENU_COLLECTION_MAGIC1 UINT32_C(0x31434d53)
#define KUI_SMASH_MENU_COLLECTION_BYTES 224u
#define KUI_SMASH_MENU_COLLECTION_FLAGS 7u
struct kui_smash_menu_collection {
    uint32_t magic0,magic1,version,bytes,count,flags;
    struct kui_smash_menu_loader_seed plans[KUI_SMASH_MENU_CHILD_COUNT];
    uint32_t reserved,crc32;
};
_Static_assert(sizeof(struct kui_smash_menu_collection)==224u,"Smash collection ABI");
bool kui_smash_menu_collection_blank(const struct kui_smash_menu_collection *);
size_t kui_smash_menu_collection_find(const void *,size_t);
bool kui_smash_menu_collection_seal(struct kui_smash_menu_collection *);
bool kui_smash_menu_collection_valid(const struct kui_smash_menu_collection *);
/* Low reader callback: authenticate the already stage-admitted table without
 * repeating nested extent validation on its guarded352-byte stack. The cold
 * dispatcher repeats full valid() before following any capsule plan. */
bool kui_smash_menu_collection_seal_valid(const struct kui_smash_menu_collection *);
#endif
