/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_GAME_CARD_SERVICE_H
#define KUI_VMU_GAME_CARD_SERVICE_H
#include "kui/probe.h"
#include "kui/vmu_game_card.h"
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

/* Raw access exists only between raw_begin and raw_end. raw_begin unmounts
 * FatFs, then supplies absolute single-sector reads of the same SCI card and
 * its freshly read CID, capacity and HC flag. raw_end remounts "0:" and must
 * fail unless the volume has the same start and length. salt fills 16 bytes
 * that differ between calls (clock and counter). */
struct kui_vmu_game_card_raw {
    int (*read_sector)(void *context,uint32_t lba,uint8_t out[512]);
    void *context;
    uint8_t cid[16];
    uint64_t card_sectors;
    bool high_capacity;
};
struct kui_vmu_game_card_platform {
    void *context;
    bool (*raw_begin)(void *context,struct kui_vmu_game_card_raw *out);
    bool (*raw_end)(void *context);
    void (*salt)(void *context,uint8_t out[16]);
};
/* Caller-owned scratch, about 90 KiB; never retained after return. */
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
 * - READY: wire holds that pair's canonical profile. A new pair is a blank
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
#endif
