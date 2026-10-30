/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_GAME_CANDIDATES_H
#define KUI_VMU_GAME_CANDIDATES_H
#include "kui/probe.h"
#include "kui/vmu_game_assign.h"

/* Launcher-only catalogue. The caller owns mounted FatFs 0: exclusively and
 * supplies a fresh physical SD identity/partition snapshot, checking it again
 * after the operation. This descriptor is stored in every catalogue generation;
 * a copied SD or changed partition cannot silently adopt its choices. */
#define KUI_VMU_GAME_CANDIDATES_PER_GAME 32u
#define KUI_VMU_GAME_CANDIDATES_PRODUCTS 256u
#define KUI_VMU_GAME_CANDIDATES_MAX 2048u
#define KUI_VMU_GAME_CANDIDATES_WIRE_BYTES (512u+40u*KUI_VMU_GAME_CANDIDATES_MAX)
#define KUI_VMU_GAME_CANDIDATES_NO_DEFAULT UINT32_MAX
#define KUI_VMU_GAME_CANDIDATES_PATH_A "0:/KUI/vmu-test/candidates-a.bin"
#define KUI_VMU_GAME_CANDIDATES_PATH_B "0:/KUI/vmu-test/candidates-b.bin"
/* Fixed little-endian wire: KUIVMUC2, version 2, total bytes, nonzero 64-bit
 * generation at 16, count at 24, entry/header/max sizes at 28/32/36. The storage
 * pin is CID at 40, capacity at 56, partition first/end at 64/72, HC at 80;
 * 84..507 are zero. CRC32 at 508 covers 0..507 and all record bytes from 512.
 * Each 40-byte record is key[12], pair, identity[16], flags (bit 0 default),
 * four zero reserved bytes. Records sort by canonical key then ascending pair;
 * each product has at most one default. Unused records are zero. Equal valid
 * generations must have identical contents. A present damaged catalogue with
 * no valid predecessor refuses; it never becomes implicit legacy fallback.
 * Bootstrap creates verified empty predecessors before the first product
 * update. Later publications overwrite a fixed-size inactive file without
 * truncation, allocation or namespace changes. Raw corruption of initial
 * directory creation still requires filesystem repair; it is not ignored. */
struct kui_vmu_game_candidates_storage {
    uint8_t cid[16];
    uint64_t card_sectors,partition_first,partition_end;
    uint32_t high_capacity;
};
struct kui_vmu_game_candidate_target {uint32_t pair;uint8_t identity[16];};
struct kui_vmu_game_candidates_view {
    char key[KUI_VMU_GAME_CARD_KEY_BYTES];
    struct kui_vmu_game_candidates_storage storage;
    uint64_t sequence;
    uint32_t count,default_index;
    bool present;
    struct kui_vmu_game_candidate_target targets[KUI_VMU_GAME_CANDIDATES_PER_GAME];
};
struct kui_vmu_game_candidates_plan {
    char key[KUI_VMU_GAME_CARD_KEY_BYTES];
    uint32_t additional;
};
enum kui_vmu_game_candidates_result {
    KUI_VMU_GAME_CANDIDATES_OK=0,
    KUI_VMU_GAME_CANDIDATES_ABSENT=1,
    KUI_VMU_GAME_CANDIDATES_ARGUMENT=-1,
    KUI_VMU_GAME_CANDIDATES_IO=-2,
    KUI_VMU_GAME_CANDIDATES_INVALID=-3,
    KUI_VMU_GAME_CANDIDATES_CANCELLED=-4,
    KUI_VMU_GAME_CANDIDATES_STALE=-5,
    KUI_VMU_GAME_CANDIDATES_FULL=-6,
    KUI_VMU_GAME_CANDIDATES_MISSING=-7
};
/* Explicit repair permission for selected products' legacy v1 targets ONLY.
 * A repair is offered only when each named pair's entire pNNNN directory is
 * positively absent (FR_NO_FILE/FR_NO_PATH). A present but incomplete card,
 * changed identity, corrupt record or I/O error never qualifies. No card file
 * or v1 assignment is removed: the new atomic candidate generation covers the
 * selected products without copying these dangling references into it. */
struct kui_vmu_game_candidates_repair {
    struct kui_vmu_game_candidates_storage storage;
    uint64_t catalogue_sequence,legacy_sequence;
    uint32_t count;
    struct kui_vmu_game_assignment entries[KUI_VMU_GAME_CANDIDATES_PRODUCTS];
};
/* Read only, including current identity/import marker and card/journal lengths
 * for every returned target. ABSENT alone permits legacy assign-v1/default
 * fallback. Present + multiple targets + NO_DEFAULT requires a launch choice;
 * a sole target can be selected without persisting a default. Outputs clear on
 * failure. None of these APIs rewrite card images, journals, identities, v1
 * assignment files, game bindings or profiles. */
int kui_vmu_game_candidates_list(const char *product,
    const struct kui_vmu_game_candidates_storage *storage,
    struct kui_vmu_game_candidates_view *out,kui_log_fn log,kui_cancel_fn cancel);
/* Read-only full-catalogue traversal, catalogue read once. Canonical groups,
 * CRC and every current target identity/ready marker/file length are checked
 * before the first callback; this is not raw allocation/ownership admission.
 * storage==NULL is display-only: the stored pin is structurally checked but
 * is NOT fresh hardware identity proof. Launch/default/write APIs never allow
 * NULL storage. The manager independently admits each displayed card.
 * Every callback key marks that product as covered:
 * suppress its v1/game.bin fallback even when its target is not on this page.
 * Pointers live only during the callback. Callback true continues; false aborts
 * with CANCELLED. On any error discard partial callback output. ABSENT means
 * neither final catalogue exists; an empty valid catalogue returns OK. */
typedef bool (*kui_vmu_game_candidates_visit_fn)(void *context,
    const char key[KUI_VMU_GAME_CARD_KEY_BYTES],
    const struct kui_vmu_game_candidate_target *target,bool is_default);
int kui_vmu_game_candidates_visit(const struct kui_vmu_game_candidates_storage *storage,
    kui_vmu_game_candidates_visit_fn visit,void *context,kui_log_fn log,kui_cancel_fn cancel);
/* Fresh-pair nonce guard only. Checks BOTH valid generations, not just the
 * newest, because either may become the recovery choice. OK means this exact
 * pair+identity is unreferenced; STALE means already pinned. Other errors also
 * refuse creation. This does not select a card or require a fresh storage pin. */
int kui_vmu_game_candidates_identity_unpinned(unsigned pair,const uint8_t identity[16],kui_cancel_fn cancel);
/* Read-only conservative capacity check before creating any imported cards.
 * Plans have unique canonical keys and count fresh targets to be added. Existing
 * v1 targets for previously absent products count towards capacity too. */
int kui_vmu_game_candidates_preflight(const struct kui_vmu_game_candidates_plan *plans,size_t count,
    const struct kui_vmu_game_candidates_storage *storage,kui_log_fn log,kui_cancel_fn cancel);
/* First attempt, read only: same checks as strict preflight. MISSING returns
 * the exact eligible missing legacy references in out; every other result
 * clears it. The caller must obtain explicit user confirmation of this repair.
 * Input/output buffers must not overlap. */
int kui_vmu_game_candidates_preflight_repair(const struct kui_vmu_game_candidates_plan *plans,size_t count,
    const struct kui_vmu_game_candidates_storage *storage,struct kui_vmu_game_candidates_repair *out,
    kui_log_fn log,kui_cancel_fn cancel);
/* After confirmation: recheck the exact storage, catalogue/v1 generations,
 * key/pair/identity records and still-absent folders before allowing this plan.
 * Every permitted key must belong to the selected plan. Any change refuses;
 * collect a fresh report and confirm it again. No files are written. */
int kui_vmu_game_candidates_preflight_confirmed(const struct kui_vmu_game_candidates_plan *plans,size_t count,
    const struct kui_vmu_game_candidates_storage *storage,const struct kui_vmu_game_candidates_repair *repair,
    kui_log_fn log,kui_cancel_fn cancel);
/* One atomic additive publication for the whole import. Repeating an identical
 * batch is a no-op. Retains all old candidates and explicit defaults. For an
 * affected v1-only product, its old identity-pinned target is migrated first
 * without an implicit default, so adding another version asks at launch.
 * All retained and new targets must be completed imported playable cards. */
int kui_vmu_game_candidates_apply(const struct kui_vmu_game_assignment *entries,size_t count,
    const struct kui_vmu_game_candidates_storage *storage,kui_log_fn log,kui_cancel_fn cancel);
/* Same additive publication with an explicitly confirmed repair. Revalidates
 * the permission afresh, then omits only those absent legacy targets in this
 * one candidate publication. Existing candidate records/defaults and unrelated
 * products are retained. This never cleans up or rewrites the old v1 files. */
int kui_vmu_game_candidates_apply_confirmed(const struct kui_vmu_game_assignment *entries,size_t count,
    const struct kui_vmu_game_candidates_storage *storage,const struct kui_vmu_game_candidates_repair *repair,
    kui_log_fn log,kui_cancel_fn cancel);
/* Explicit manual "Assign games" counterpart: one target per product. Adds
 * each target and selects it as that product's default in the SAME catalogue
 * generation, retaining every other candidate and unrelated default. A mixed
 * v1/v2 batch must choose this authority for the whole batch, never publish v1
 * first and v2 afterwards. Existing v1 targets are migrated as above. */
int kui_vmu_game_candidates_apply_selected(const struct kui_vmu_game_assignment *entries,size_t count,
    const struct kui_vmu_game_candidates_storage *storage,kui_log_fn log,kui_cancel_fn cancel);
/* target==NULL clears the default. Otherwise it must exactly match a current
 * candidate. expected_sequence comes from list; a stale snapshot refuses before
 * writing, including when another product changed. An unchanged choice is a
 * no-op. No default is stored for a one-time launch selection. */
int kui_vmu_game_candidates_set_default(const char *product,
    const struct kui_vmu_game_candidate_target *target,uint64_t expected_sequence,
    const struct kui_vmu_game_candidates_storage *storage,kui_log_fn log,kui_cancel_fn cancel);
#endif
