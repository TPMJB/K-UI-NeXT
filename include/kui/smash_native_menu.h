/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_NATIVE_MENU_H
#define KUI_SMASH_NATIVE_MENU_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The original menu's completed GIN handoff supplies these two pointer-free
 * fields. A fresh cold module reloads and verifies the original engine before
 * applying them; no executable, context pointer or live heap crosses over. */
#define KUI_SMASH_NATIVE_MENU_SEED_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_NATIVE_MENU_SEED_MAGIC1 UINT32_C(0x31554e4d)
#define KUI_SMASH_NATIVE_MENU_SEED_BYTES 304u
#define KUI_SMASH_NATIVE_MENU_ARGV_BYTES 256u
#define KUI_SMASH_NATIVE_MENU_IP_BYTES 16u
#define KUI_SMASH_NATIVE_MENU_SELECTIONS 10u
#define KUI_SMASH_NATIVE_MENU_PACKAGE "0:/KUI/apps/games/retail-smash-native-menu.kui"
#define KUI_SMASH_NATIVE_MENU_SEED_BLANK_WORDS \
    KUI_SMASH_NATIVE_MENU_SEED_MAGIC0,KUI_SMASH_NATIVE_MENU_SEED_MAGIC1,1u, \
    KUI_SMASH_NATIVE_MENU_SEED_BYTES,0u,0u,0u,0u

struct kui_smash_native_menu_seed {
    uint32_t magic0,magic1,version,bytes,selection,reserved[2],crc32;
    uint8_t argv[KUI_SMASH_NATIVE_MENU_ARGV_BYTES];
    uint8_t ip_tail[KUI_SMASH_NATIVE_MENU_IP_BYTES];
};
_Static_assert(sizeof(struct kui_smash_native_menu_seed)==304u,"Smash menu seed ABI");
_Static_assert(offsetof(struct kui_smash_native_menu_seed,argv)==32u,"Smash menu argv ABI");
_Static_assert(offsetof(struct kui_smash_native_menu_seed,ip_tail)==288u,"Smash menu IP ABI");

bool kui_smash_native_menu_seed_blank(const struct kui_smash_native_menu_seed *);
size_t kui_smash_native_menu_seed_find(const void *payload,size_t bytes);
/* Failure leaves the blank seed unchanged. Both source fields must describe
 * one exact original menu selection, including its source option tokens. */
bool kui_smash_native_menu_seed_capture(struct kui_smash_native_menu_seed *,
    const uint8_t argv[KUI_SMASH_NATIVE_MENU_ARGV_BYTES],
    const uint8_t ip_tail[KUI_SMASH_NATIVE_MENU_IP_BYTES]);
bool kui_smash_native_menu_seed_valid(const struct kui_smash_native_menu_seed *);
const char *kui_smash_native_menu_name(unsigned selection);
/* These share the original ten-entry selection catalog, not a second list. */
const char *kui_smash_native_menu_folder(unsigned selection);
const char *kui_smash_native_menu_rom(unsigned selection);
/* Only after complete original-image verification and native SDK discovery.
 * Failure changes neither disjoint output. The carried bytes stay exact. */
bool kui_smash_native_menu_arguments(void *context,size_t context_bytes,
    uint8_t ip_tail[KUI_SMASH_NATIVE_MENU_IP_BYTES],
    const struct kui_smash_native_menu_seed *);
#endif
