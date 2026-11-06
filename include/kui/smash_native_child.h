/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_NATIVE_CHILD_H
#define KUI_SMASH_NATIVE_CHILD_H
#include "kui/game_metadata.h"
#include <stddef.h>

/* Source-bound cold child launches. The normal Smash Pack menu and its CE
 * child retain their existing route; no hot executable transition is implied. */
#define KUI_SMASH_NATIVE_MENU_BYTES 212324u
#define KUI_SMASH_NATIVE_MENU_CRC32 UINT32_C(0xc28e5f94)
#define KUI_SMASH_NATIVE_IP_CRC32 UINT32_C(0x18efc93a)
#define KUI_SMASH_NATIVE_ENGINE_BYTES 1848288u
#define KUI_SMASH_NATIVE_ENGINE_CRC32 UINT32_C(0xd2f2eafe)
#define KUI_SMASH_NATIVE_CONTEXT_OFFSET UINT32_C(0x18ee60)
#define KUI_SMASH_NATIVE_CONTEXT_BYTES 8192u
#define KUI_SMASH_NATIVE_CONTEXT_CRC32 UINT32_C(0x11634ac3)
#define KUI_SMASH_NATIVE_CHILD_PACKAGE "0:/KUI/apps/games/retail-smash-native.kui"
#define KUI_SMASH_NATIVE_CHILD_SEED_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_NATIVE_CHILD_SEED_MAGIC1 UINT32_C(0x31444843)
#define KUI_SMASH_NATIVE_CHILD_SEED_BYTES 32u
enum kui_smash_native_child {
    KUI_SMASH_NATIVE_CHILD_NONE,
    KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II,
    KUI_SMASH_NATIVE_CHILD_SHINING_FORCE,
    KUI_SMASH_NATIVE_CHILD_COUNT
};
struct kui_smash_native_child_seed {
    uint32_t magic0,magic1,version,bytes,child,reserved[2],crc32;
};
_Static_assert(sizeof(struct kui_smash_native_child_seed)==32u,"Smash native child seed ABI");
bool kui_smash_native_child_candidate(const struct kui_game_metadata *);
bool kui_smash_native_child_menu_owner(uint32_t ip_crc,uint32_t boot_bytes,const uint8_t boot_sha256[32]);
bool kui_smash_native_child_engine_owner(uint32_t bytes,const uint8_t sha256[32]);
bool kui_smash_native_child_seed_seal(struct kui_smash_native_child_seed *);
bool kui_smash_native_child_seed_valid(const struct kui_smash_native_child_seed *);
const char *kui_smash_native_child_name(unsigned child);
const char *kui_smash_native_child_folder(unsigned child);
const char *kui_smash_native_child_rom(unsigned child);
/* Apply only after the caller verified the complete original MDE image and
 * completed its immutable SDK discovery. The original8KiB context is checked
 * again here. On failure both disjoint outputs stay unchanged. IP tail is the
 * sixteen bytes beginning at8c0080f0; its final two bytes are preserved. */
/* Original BOOT2 owner frame only; validate all scalar state before applying
 * the five source-proven GIN differences. Failure leaves it unchanged. */
bool kui_smash_native_child_frame(uint32_t frame[21],uint32_t frame_address);
bool kui_smash_native_child_arguments(void *context,size_t context_bytes,
    uint8_t ip_tail[16],unsigned child);
#endif
