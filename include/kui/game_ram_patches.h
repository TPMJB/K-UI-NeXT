/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAME_RAM_PATCHES_H
#define KUI_GAME_RAM_PATCHES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_GAME_RAM_PATCHES_COUNT 2u
#define KUI_GAME_RAM_PATCH_EXEC_ADDRESS UINT32_C(0x8c010000)
#define KUI_GAME_RAM_PATCH_EXEC_MAX_BYTES UINT32_C(0x00c00000)
struct kui_game_ram_patch {uint32_t address,value;bool enabled;};
struct kui_game_ram_patches {struct kui_game_ram_patch entries[KUI_GAME_RAM_PATCHES_COUNT];};

/* Only physical/P1/P2 aliases of the owner's loaded native executable are
 * eligible. Four-byte alignment is required. Disabled tuples are ignored so
 * the UI can retain a choice while it is off. Duplicate physical addresses
 * fail, even when entered using different aliases. No MMIO or runtime hook. */
bool kui_game_ram_patches_enabled(const struct kui_game_ram_patches *patches);
bool kui_game_ram_patches_valid(const struct kui_game_ram_patches *patches);
/* Validate both patches before publishing any offsets. Disabled offsets are
 * UINT32_MAX. Actual file bytes, rather than sector padding, bound writes. */
bool kui_game_ram_patches_plan(const struct kui_game_ram_patches *patches,
    uint32_t exec_bytes,uint32_t offsets[KUI_GAME_RAM_PATCHES_COUNT]);
/* No writes on failure. Enabled writes are exactly two or fewer volatile
 * uint32 stores into an aligned caller-owned executable buffer. The caller
 * owns cache publication and must first check original executable integrity.
 * All-off is a true no-op, including with a NULL executable. */
bool kui_game_ram_patches_apply(void *executable,uint32_t exec_bytes,
    const struct kui_game_ram_patches *patches);

/* This private high-stage capability leaves the retail manifest and low
 * resident unchanged. Native ordinary stages contain exactly one blank seed.
 * Old or incompatible packages without a blank seed cannot accept patches.
 * Wire LE words: magic[2],version,bytes,enable-mask,exact-executable-bytes,
 * address0,value0,address1,value1,CRC32,reserved[5]. CRC covers all64bytes with
 * its own word zeroed. Disabled pairs/reserved words must be zero. A blank
 * seed has only its first four words set; all-off encoding preserves it.
 * Encoded addresses are canonical P1 aliases. No game data is shipped. */
#define KUI_GAME_RAM_PATCH_SEED_WORDS 16u
#define KUI_GAME_RAM_PATCH_SEED_BYTES 64u
#define KUI_GAME_RAM_PATCH_SEED_MAGIC0 UINT32_C(0x5249554b) /* KUIR */
#define KUI_GAME_RAM_PATCH_SEED_MAGIC1 UINT32_C(0x31504d41) /* AMP1 */
#define KUI_GAME_RAM_PATCH_SEED_VERSION 1u
#define KUI_GAME_RAM_PATCH_SEED_BLANK_WORDS \
    {KUI_GAME_RAM_PATCH_SEED_MAGIC0,KUI_GAME_RAM_PATCH_SEED_MAGIC1, \
     KUI_GAME_RAM_PATCH_SEED_VERSION,KUI_GAME_RAM_PATCH_SEED_BYTES,0,0,0,0,0,0,0,0,0,0,0,0}
struct kui_game_ram_patch_seed {uint32_t words[KUI_GAME_RAM_PATCH_SEED_WORDS];};
_Static_assert(sizeof(struct kui_game_ram_patch_seed)==KUI_GAME_RAM_PATCH_SEED_BYTES,"RAM patch seed ABI");
bool kui_game_ram_patch_seed_blank(const void *seed,size_t bytes);
/* Only an exact blank seed can be filled; failure leaves it unchanged. */
bool kui_game_ram_patch_seed_encode(void *seed,size_t bytes,
    const struct kui_game_ram_patches *patches,uint32_t exec_bytes);
/* Blank is accepted as all-off. Failure preserves out. */
bool kui_game_ram_patch_seed_decode(struct kui_game_ram_patches *out,
    const void *seed,size_t bytes,uint32_t exec_bytes);
#endif
