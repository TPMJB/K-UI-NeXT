/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_WIZARD_H
#define KUI_VMU_WIZARD_H
#include "kui/apps.h"
#include "kui/games.h"
/* Captured source images, not the number of per-game output cards. */
#define KUI_VMU_WIZARD_CARDS 8u
#define KUI_VMU_WIZARD_GAMES 256u
#define KUI_VMU_WIZARD_ROWS 8u
enum kui_vmu_wizard_stage { KUI_VMU_WIZARD_SOURCES, KUI_VMU_WIZARD_GAMES_STAGE,
    KUI_VMU_WIZARD_REVIEW, KUI_VMU_WIZARD_DONE, KUI_VMU_WIZARD_SAVES };
enum kui_vmu_wizard_op { KUI_VMU_WIZARD_OPEN, KUI_VMU_WIZARD_RESET,
    KUI_VMU_WIZARD_CAPTURE, KUI_VMU_WIZARD_LIST_GAMES, KUI_VMU_WIZARD_TOGGLE,
    KUI_VMU_WIZARD_SHOW_SOURCES, KUI_VMU_WIZARD_SHOW_REVIEW, KUI_VMU_WIZARD_COMMIT,
    KUI_VMU_WIZARD_USE_CARD, KUI_VMU_WIZARD_OPEN_SAVES,
    KUI_VMU_WIZARD_TOGGLE_SAVE, KUI_VMU_WIZARD_ACCEPT_SAVES,
    KUI_VMU_WIZARD_SHOW_GAMES, KUI_VMU_WIZARD_TOGGLE_BLANK,
    KUI_VMU_WIZARD_REVIEW_PAGE };
struct kui_vmu_wizard_request {
    enum kui_vmu_wizard_op op;
    /* OPEN_SAVES index: global game row. Within SAVES, OPEN_SAVES changes
     * source/card and page; TOGGLE_SAVE index: source directory ordinal.
     * TOGGLE_BLANK index: global game row. SHOW_GAMES uses game page. */
    unsigned generation,slot,card,page,index;
    bool view_only;
    char root[KUI_DEST_ROOT_CAP];
    char card_path[KUI_VMU_BACKUP_PATH_CAP];
};
struct kui_vmu_wizard_card {
    unsigned files,blocks,sources,games;
    /* Fresh import: completed immutable archive ID; USE_CARD: active pair. */
    unsigned pair;
};
/* supported means native-route eligibility only. The complete combined-reader
 * and memory-allocation admission still runs at game launch. */
struct kui_vmu_wizard_game {
    char title[129],product[12];
    bool selected,supported,blank;
    unsigned variants,saves,blocks;
};
struct kui_vmu_wizard_save {
    /* index is the inspected directory ordinal, slot its original VMU slot.
     * Neither is a displayed page-relative row number. */
    unsigned index,slot,blocks,type;
    char name[13],short_description[17],long_description[33];
    bool picked,suggested;
};
struct kui_vmu_wizard_view {
    struct kui_app_status status;
    enum kui_vmu_wizard_stage stage;
    bool publication_started;
    /* Existing imported cards are reused. Dedicated cards/snapshots are copied
     * to a new shared card only after the user confirms the final review. */
    bool existing_card,copy_on_commit,view_only;
    unsigned generation,slot,card,cards,sources,page,count,total,selected_games;
    struct kui_vmu_wizard_card summary[KUI_VMU_WIZARD_CARDS];
    struct kui_vmu_wizard_game games[KUI_VMU_WIZARD_ROWS];
    /* A new physical import has one independent plan per game/source. Saves
     * stay drafts until ACCEPT_SAVES; no suggestion selects a file. */
    unsigned game_index,saves_page,saves_count,saves_total;
    unsigned saves_selected_files,saves_selected_blocks,saves_variants;
    unsigned variants; /* Accepted output cards across all selected games. */
    char game_title[129],game_product[12];
    struct kui_vmu_wizard_save saves[KUI_VMU_WIZARD_ROWS];
};
/* Storage worker owns the RAM-only session. Every returned view is a copy;
 * physical sources remain read-only. Publication requires COMMIT from review. */
void kui_vmu_wizard_run(const struct kui_vmu_wizard_request *request,
    struct kui_vmu_wizard_view *out,kui_log_fn log,kui_cancel_fn cancel,
    kui_app_progress_fn progress);
#endif
