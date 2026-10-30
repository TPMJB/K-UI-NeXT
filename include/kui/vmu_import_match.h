/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_IMPORT_MATCH_H
#define KUI_VMU_IMPORT_MATCH_H
#include "kui/vmu_import_plan.h"
#include <stdbool.h>
#include <limits.h>

#define KUI_VMU_IMPORT_MATCH_MAX_GAMES 256u
#define KUI_VMU_IMPORT_MATCH_MEMBERS_BYTES ((KUI_VMU_IMAGE_DIRECTORY_ENTRIES + 7u) / 8u)
#define KUI_VMU_IMPORT_MATCH_CANDIDATES_BYTES ((KUI_VMU_IMPORT_MATCH_MAX_GAMES + 7u) / 8u)
#define KUI_VMU_IMPORT_MATCH_NO_GAME UINT_MAX
struct kui_vmu_import_match_game {
    char title[129], product[12];
    bool native_eligible;
};
enum kui_vmu_import_match_state {
    KUI_VMU_IMPORT_MATCH_AUTO,
    KUI_VMU_IMPORT_MATCH_CHOOSE_GAME
};
enum kui_vmu_import_match_reason {
    KUI_VMU_IMPORT_MATCH_TITLE = 1u,
    KUI_VMU_IMPORT_MATCH_PRODUCT = 2u,
    KUI_VMU_IMPORT_MATCH_APPLICATION = 4u,
    KUI_VMU_IMPORT_MATCH_ALIAS = 8u,
    KUI_VMU_IMPORT_MATCH_FAMILY = 16u,
    KUI_VMU_IMPORT_MATCH_TRUNCATED = 32u,
    KUI_VMU_IMPORT_MATCH_AMBIGUOUS = 64u,
    KUI_VMU_IMPORT_MATCH_CONFLICT = 128u,
    KUI_VMU_IMPORT_MATCH_UNSUPPORTED = 256u,
    KUI_VMU_IMPORT_MATCH_UNKNOWN = 512u
};
struct kui_vmu_import_match_group {
    /* Bits refer to inspected entries[] ordinals, not directory slots. */
    uint8_t members[KUI_VMU_IMPORT_MATCH_MEMBERS_BYTES];
    /* Plausible global catalogue indices. AUTO has just its winner; unknown
     * groups have no candidates and can show the complete game chooser. */
    uint8_t candidates[KUI_VMU_IMPORT_MATCH_CANDIDATES_BYTES];
    unsigned files, blocks, game_index, candidate_count;
    enum kui_vmu_import_match_state state;
    uint32_t reasons;
    char label[49];
};
struct kui_vmu_import_matches {
    unsigned group_count;
    struct kui_vmu_import_match_group groups[KUI_VMU_IMAGE_DIRECTORY_ENTRIES];
};

/* Pure matching of ONE source's successfully inspected, admitted snapshot.
 * The caller supplies the complete stable catalogue, including ineligible
 * competing titles. Strong unique text/product or vetted game-family evidence
 * can assign a group automatically. Ambiguous, conflicting, unsupported and
 * unknown evidence leaves game_index=NO_GAME for one user game choice.
 * Generic publisher/platform app IDs never connect otherwise unrelated files.
 * No fuzzy distance, substring-only short title or arbitrary numeric score
 * breaks a tie. Related files stay together; every inspected ordinal occurs
 * in exactly one nonempty group. Separate calls never combine source versions.
 *
 * This does not identify a save cryptographically or establish hardware/game
 * compatibility. It writes no image, archive, destination, assignment or card.
 * Use the group member ordinals to choose explicit slots for import_plan_build.
 * Inputs must remain immutable, and out must be disjoint from both inputs.
 * Returns 0 on success, -1 for invalid/overlapping arguments without mutation.
 * Only plans returned successfully by import_plan_inspect are admitted inputs;
 * structural bounds are rechecked here, not the underlying image filesystem.
 */
int kui_vmu_import_match(const struct kui_vmu_import_plan *plan,
    const struct kui_vmu_import_match_game *games, size_t game_count,
    struct kui_vmu_import_matches *out);
#endif
