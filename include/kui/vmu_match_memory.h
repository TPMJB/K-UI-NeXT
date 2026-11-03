/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_MATCH_MEMORY_H
#define KUI_VMU_MATCH_MEMORY_H
#include "kui/vmu_import_match.h"

#define KUI_VMU_MATCH_MEMORY_KEY_BYTES 32u
#define KUI_VMU_MATCH_MEMORY_ROOT "0:/KUI/vmu/matches"
struct kui_vmu_match_memory_target {
    char product[12], title[129];
};
enum kui_vmu_match_memory_result {
    KUI_VMU_MATCH_MEMORY_OK = 0,
    KUI_VMU_MATCH_MEMORY_MISSING = 1,
    KUI_VMU_MATCH_MEMORY_INVALID = -1,
    KUI_VMU_MATCH_MEMORY_IO = -2,
    KUI_VMU_MATCH_MEMORY_CORRUPT = -3
};

/* Pure fingerprint of a nonempty match group from an admitted import plan.
 * Member order, directory slots, allocation, timestamps, hints and save
 * payload/progress are excluded. Each member's complete sanitized internal
 * filename, app ID, short/long descriptions, type, blocks and header offset
 * are included. Changing a description or group membership requires review
 * again. The group files/blocks must agree with its membership bits.
 * Invalid input leaves key unchanged. No raw save bytes are retained. */
int kui_vmu_match_memory_key(const struct kui_vmu_import_plan *plan,
    const struct kui_vmu_import_match_group *group,
    uint8_t key[KUI_VMU_MATCH_MEMORY_KEY_BYTES]);

/* Caller exclusively owns already mounted FatFs storage for these calls.
 * Lookup never creates files. OK returns the exact confirmed product/title;
 * every other result clears target. A newest forget tombstone is MISSING.
 * Any malformed published slot refuses the whole key, including when the
 * other slot is valid. Unpublished .part files are never applied.
 *
 * This is remembered user intent, not evidence of game compatibility. The
 * caller must require exactly one eligible catalogue row with BOTH strings
 * equal, and ask again for missing, ambiguous or contradictory evidence. */
int kui_vmu_match_memory_load(const struct kui_vmu_import_plan *plan,
    const struct kui_vmu_import_match_group *group,
    struct kui_vmu_match_memory_target *target);

/* Call only for an explicitly confirmed manual choice after successful user
 * review/commit, never for inferred or remembered choices. Idempotent for the
 * same target. A correction publishes a new generation while retaining the
 * previous one. Each key has two fixed-size CRC-checked slots: write a
 * create-new .part, sync/close, reread/verify, then rename over the older
 * slot. Interrupted staging cannot replace the latest published choice.
 * Corrupt published records are preserved and never silently repaired.
 *
 * If two reviewed groups have the same key but different confirmed targets,
 * skip learning either and forget their shared key instead. */
int kui_vmu_match_memory_remember(const struct kui_vmu_import_plan *plan,
    const struct kui_vmu_import_match_group *group,
    const struct kui_vmu_match_memory_target *target);
int kui_vmu_match_memory_forget(const struct kui_vmu_import_plan *plan,
    const struct kui_vmu_import_match_group *group);
#endif
