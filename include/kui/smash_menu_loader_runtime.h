/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_MENU_LOADER_RUNTIME_H
#define KUI_SMASH_MENU_LOADER_RUNTIME_H
#include "kui/smash_menu_return.h"
#include "kui/smash_card_session.h"
#include "kui/smash_menu_collection.h"
/* The surviving low loader keeps the read-only child plan, the original
 * firmware and the outer restart plan. The restart arguments belong only
 * to the current menu; each child receives a freshly configured plan. */
struct kui_smash_menu_loader_runtime {
    struct kui_smash_menu_collection collection;
    uint32_t firmware[9];
    uint32_t stop_entry;
    struct kui_smash_menu_return_seed returning;
#if KUI_SMASH_CARD_SESSION
    struct kui_smash_card_receipt card_receipt;
#endif
};
_Static_assert(offsetof(struct kui_smash_menu_loader_runtime,returning)==264u,
    "Smash menu runtime return offset");
_Static_assert(sizeof(struct kui_smash_menu_loader_runtime)==632u+96u*KUI_SMASH_CARD_SESSION,
    "Smash menu loader runtime ABI");
#endif
