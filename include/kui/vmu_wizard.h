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
    KUI_VMU_WIZARD_REVIEW, KUI_VMU_WIZARD_DONE, KUI_VMU_WIZARD_SAVES,
    KUI_VMU_WIZARD_GROUPS, KUI_VMU_WIZARD_GROUP_GAME };
enum kui_vmu_wizard_op { KUI_VMU_WIZARD_OPEN, KUI_VMU_WIZARD_RESET,
    KUI_VMU_WIZARD_CAPTURE, KUI_VMU_WIZARD_LIST_GAMES, KUI_VMU_WIZARD_TOGGLE,
    KUI_VMU_WIZARD_SHOW_SOURCES, KUI_VMU_WIZARD_SHOW_REVIEW, KUI_VMU_WIZARD_COMMIT,
    KUI_VMU_WIZARD_USE_CARD, KUI_VMU_WIZARD_OPEN_SAVES,
    KUI_VMU_WIZARD_TOGGLE_SAVE, KUI_VMU_WIZARD_ACCEPT_SAVES,
    KUI_VMU_WIZARD_SHOW_GAMES, KUI_VMU_WIZARD_TOGGLE_BLANK,
    KUI_VMU_WIZARD_REVIEW_PAGE, KUI_VMU_WIZARD_SHOW_GROUPS,
    KUI_VMU_WIZARD_OPEN_GROUP, KUI_VMU_WIZARD_RESOLVE_GROUP,
    KUI_VMU_WIZARD_SKIP_GROUP, KUI_VMU_WIZARD_GROUP_GAME_PAGE,
    KUI_VMU_WIZARD_CONFIRM_MISSING, KUI_VMU_WIZARD_CANCEL_MISSING };
struct kui_vmu_wizard_request {
    enum kui_vmu_wizard_op op;
    /* OPEN_SAVES index: global game row. Within SAVES, OPEN_SAVES changes
     * source/card and page; TOGGLE_SAVE index: source directory ordinal.
     * TOGGLE_BLANK index: global game row. SHOW_GAMES uses game page.
     * OPEN_GROUP/SKIP_GROUP index: stable visible group ID, page: group page.
     * RESOLVE_GROUP index: global game row in GROUP_GAME's displayed page.
     * SHOW_GROUPS/GROUP_GAME_PAGE page the unresolved queue / game picker. */
    unsigned generation,slot,card,page,index;
    unsigned group_index; /* Expected group ID for a GROUP_GAME answer. */
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
    unsigned index; /* Stable global catalogue index, including filtered review. */
    bool selected,supported,blank;
    unsigned variants,saves,blocks;
    unsigned free_min,free_max; /* Free blocks per separate 200-block output. */
};
struct kui_vmu_wizard_save {
    /* index is the inspected directory ordinal, slot its original VMU slot.
     * Neither is a displayed page-relative row number. */
    unsigned index,slot,blocks,type;
    char name[13],short_description[17],long_description[33];
    bool picked,suggested;
};
struct kui_vmu_wizard_group {
    unsigned index,source,source_slot,files,blocks;
    char label[49],reason[96];
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
    /* Automatic matching never creates blank cards. Only unresolved groups
     * appear here; explicit resolution assigns every member as one set. */
    bool automatic,missing_card_confirmation;
    unsigned missing_cards,missing_pair;
    char missing_product[12];
    unsigned groups_page,groups_count,groups_total,unresolved_groups,skipped_groups;
    struct kui_vmu_wizard_group groups[KUI_VMU_WIZARD_ROWS];
    unsigned group_index,group_source,group_source_slot,group_files,group_blocks;
    char group_label[49];
};
/* Storage worker owns the RAM-only session. Every returned view is a copy;
 * physical sources remain read-only. Publication requires COMMIT from review. */
void kui_vmu_wizard_run(const struct kui_vmu_wizard_request *request,
    struct kui_vmu_wizard_view *out,kui_log_fn log,kui_cancel_fn cancel,
    kui_app_progress_fn progress);
#endif
