/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_CARDS_H
#define KUI_DISC_CARDS_H
#include "kui/apps.h"
#include "kui/game_metadata.h"
#include "kui/vmu_game_candidates.h"

#define KUI_DISC_CARDS_ROWS 8u
enum kui_disc_cards_op {
    KUI_DISC_CARDS_IDENTIFY, KUI_DISC_CARDS_LINKED, KUI_DISC_CARDS_IMPORTS,
    KUI_DISC_CARDS_LINK, KUI_DISC_CARDS_CREATE,
    KUI_DISC_CARDS_DEFAULT, KUI_DISC_CARDS_ASK
};
enum kui_disc_cards_stage { KUI_DISC_CARDS_LINKED_STAGE, KUI_DISC_CARDS_IMPORTS_STAGE };
struct kui_disc_cards_row {
    char label[64],path[KUI_VMU_BACKUP_PATH_CAP],contents[97];
    uint8_t identity[16];
    unsigned pair,files,free_blocks;
    bool active,linked,is_default,readable,suggested,link_copies;
};
struct kui_disc_cards_request {
    enum kui_disc_cards_op op;
    unsigned generation,page;
    /* Copy these review pins from the last successful view for every op
     * except IDENTIFY. Copy path/pair/identity from the chosen row for LINK
     * or DEFAULT; CREATE/ASK need no row. No row number grants write authority. */
    uint64_t token,candidates_sequence;
    char product[KUI_VMU_GAME_CARD_KEY_BYTES];
    uint8_t signature[32];
    struct kui_vmu_game_candidates_storage storage;
    char path[KUI_VMU_BACKUP_PATH_CAP];
    unsigned pair;
    uint8_t identity[16];
};
struct kui_disc_cards_view {
    struct kui_app_status status;
    enum kui_disc_cards_stage stage;
    unsigned generation,page,count,total,linked_count,default_pair;
    uint64_t token,candidates_sequence;
    bool identified,created;
    struct kui_game_metadata metadata;
    char product[KUI_VMU_GAME_CARD_KEY_BYTES];
    uint8_t signature[32];
    struct kui_vmu_game_candidates_storage storage;
    struct kui_disc_cards_row rows[KUI_DISC_CARDS_ROWS];
};
/* Exclusive worker operation. Root grants optical handoff before calling.
 * Each operation identifies the inserted disc before opening SD storage.
 * Writes require the exact displayed release/signature, storage pin, catalogue
 * generation and selected source identity. Browsing creates no card. LINK
 * shares an imported active card, or makes a playable copy of an archive or
 * a legacy dedicated card (row.link_copies tells the confirmation which);
 * CREATE publishes a new empty 200-block card. Both add links and retain old
 * defaults. DEFAULT/ASK explicitly change the launch default. Global Virtual
 * VMU On/Off is never read or changed. No physical VMU is accessed. */
void kui_disc_cards_run(const struct kui_disc_cards_request *request,
    struct kui_disc_cards_view *out,kui_log_fn log,kui_cancel_fn cancel,
    kui_app_progress_fn progress);
#endif
