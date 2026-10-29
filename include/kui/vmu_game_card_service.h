/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_GAME_CARD_SERVICE_H
#define KUI_VMU_GAME_CARD_SERVICE_H
#include "kui/probe.h"
#include "kui/vmu_game_card.h"
#include "kui/vmu_game_assign.h"
#include "kui/vmu_fs_admission.h"
#include "kui/vmu_image.h"

/* Launcher-side selection of a game's own virtual card. The caller owns the
 * mounted FatFs volume "0:" on the SCI card and runs no other file worker.
 * The selected pair's P2 profile is returned for the module's profile slot;
 * the stage then admits the live filesystem and the pair exactly as for an
 * installed card. This service never writes a game's existing card.vmd or
 * card.journal, and never touches the physical VMU or another pair's files. */
#define KUI_VMU_GAME_CARD_ROOT "0:/KUI/vmu-test"
#define KUI_VMU_GAME_CARD_PAIRS_MAX 9999u

/* identity reads the SCI card's CID, capacity and HC flag afresh while the
 * mounted volume is idle. Raw access exists only between raw_begin and
 * raw_end: raw_begin unmounts FatFs and supplies absolute single-sector
 * reads of the same card; raw_end remounts "0:" and must fail unless the
 * volume has the same start and length. salt fills 16 bytes that differ
 * between calls (clock and counter). */
struct kui_vmu_game_card_raw {
    int (*read_sector)(void *context,uint32_t lba,uint8_t out[512]);
    void *context;
    uint8_t cid[16];
    uint64_t card_sectors;
    bool high_capacity;
};
struct kui_vmu_game_card_platform {
    void *context;
    bool (*identity)(void *context,struct kui_vmu_game_card_raw *out);
    bool (*raw_begin)(void *context,struct kui_vmu_game_card_raw *out);
    bool (*raw_end)(void *context);
    void (*salt)(void *context,uint8_t out[16]);
};
/* Caller-owned launcher/GUI scratch; never retained during game execution. */
struct kui_vmu_game_card_work {
    struct kui_vmu_persist_profile profile,cached;
    struct kui_vmu_fs_workspace filesystem;
    struct kui_vmu_image_workspace image;
    struct kui_vmu_game_card_raw raw;
    uint8_t wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES],check[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES];
    uint8_t sector[512];
    uint32_t link_map[2u+2u*32u];
    char path[96];
    uint16_t pairs[KUI_VMU_GAME_CARD_PAIRS_MAX];
    unsigned pair_count;
    uint8_t used[KUI_VMU_GAME_CARD_PAIRS_MAX/8u+1u];
    struct kui_vmu_game_assign_table assignments,assignment_candidate;
    uint8_t assignment_wire[KUI_VMU_GAME_ASSIGN_WIRE_BYTES];
    uint8_t assignment_valid_slots;
};
enum kui_vmu_game_card_result {
    KUI_VMU_GAME_CARD_READY=0,
    KUI_VMU_GAME_CARD_UNBOUND=1,
    KUI_VMU_GAME_CARD_FAILED=-1,
    KUI_VMU_GAME_CARD_CANCELLED=-2
};
struct kui_vmu_game_card_choice {
    unsigned pair;
    bool created,collected;
    char key[KUI_VMU_GAME_CARD_KEY_BYTES];
    char problem[96];
};
/* Selects, and when absent creates, the pair bound to product's key:
 * - READY: wire holds that pair's canonical profile for this very SD card
 *   (CID, capacity, partition, geometry and file clusters). A new pair is a blank
 *   formatted card, zero journal, fresh identity and game.bin written last,
 *   created only after every existing pair's clusters read as allocated.
 *   A pair whose saved profile no longer matches its files is re-mapped by
 *   a full ownership scan, which needs its journal marker clear.
 * - UNBOUND: the product binds no card; nothing was read or written.
 * - FAILED/CANCELLED: wire is zero; choice->problem names the refusal.
 * partition_first/end are the mounted volume's absolute bounds; forbidden
 * lists the game's mapped track extents, which a card may not overlap. */
int kui_vmu_game_card_select(const char *product,uint64_t partition_first,uint64_t partition_end,
    const struct kui_vmu_fs_range *forbidden,size_t forbidden_count,
    const struct kui_vmu_game_card_platform *platform,struct kui_vmu_game_card_work *work,
    uint8_t wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES],struct kui_vmu_game_card_choice *choice,
    kui_log_fn log,kui_cancel_fn cancel);
/* Read-only explicit-assignment lookup on mounted FatFs. READY means a
 * complete identity-pinned target exists (choice->pair); UNBOUND means no
 * explicit assignment. FAILED/CANCELLED must refuse launch. This does not
 * choose/create a legacy default or prepare a profile. */
int kui_vmu_game_card_assignment(const char *product,struct kui_vmu_game_card_work *work,
    struct kui_vmu_game_card_choice *choice,kui_cancel_fn cancel);
/* Publish a fresh imported playable card, with no game assignment yet. The
 * caller owns mounted FatFs and supplies a fresh 16-byte clock/counter salt.
 * image must be an admitted standard 128 KiB image. Old cards are never
 * changed. The new folder may remain incomplete on failure and is never
 * reused. identity receives the pinned identity only on READY. No profile is
 * required here: ordinary launch prepares it after the game is assigned. */
int kui_vmu_game_card_import_publish(const void *image,size_t bytes,const uint8_t salt[16],
    struct kui_vmu_game_card_work *work,struct kui_vmu_game_card_choice *choice,
    uint8_t identity[16],kui_log_fn log,kui_cancel_fn cancel);
/* Atomically add/replace this entire batch of explicit game assignments,
 * retaining unrelated assignments. Every target must be a completed imported
 * card with this exact identity. Two alternating sequence/CRC files preserve
 * the preceding complete assignment set across an interrupted publication.
 * A corrupted table with no valid predecessor refuses all automatic selection.
 * Several keys may name one card. Existing card data/journals are untouched.
 * Caller owns mounted FatFs; READY is the only successful publication result. */
int kui_vmu_game_card_assignments_apply(const struct kui_vmu_game_assignment *entries,size_t count,
    struct kui_vmu_game_card_work *work,struct kui_vmu_game_card_choice *choice,
    kui_log_fn log,kui_cancel_fn cancel);
#endif
