/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_WIZARD_H
#define KUI_VMU_WIZARD_H
#include "kui/apps.h"
#include "kui/games.h"
#define KUI_VMU_WIZARD_CARDS 8u
#define KUI_VMU_WIZARD_GAMES 256u
#define KUI_VMU_WIZARD_ROWS 8u
enum kui_vmu_wizard_stage { KUI_VMU_WIZARD_SOURCES, KUI_VMU_WIZARD_GAMES_STAGE,
    KUI_VMU_WIZARD_REVIEW, KUI_VMU_WIZARD_DONE };
enum kui_vmu_wizard_op { KUI_VMU_WIZARD_OPEN, KUI_VMU_WIZARD_RESET,
    KUI_VMU_WIZARD_CAPTURE, KUI_VMU_WIZARD_LIST_GAMES, KUI_VMU_WIZARD_TOGGLE,
    KUI_VMU_WIZARD_SHOW_SOURCES, KUI_VMU_WIZARD_SHOW_REVIEW, KUI_VMU_WIZARD_COMMIT,
    KUI_VMU_WIZARD_USE_CARD };
struct kui_vmu_wizard_request {
    enum kui_vmu_wizard_op op;
    unsigned generation,slot,card,page,index;
    bool view_only;
    char root[KUI_DEST_ROOT_CAP];
    char card_path[KUI_VMU_BACKUP_PATH_CAP];
};
struct kui_vmu_wizard_card { unsigned files,blocks,sources,games,pair; };
/* supported means native-route eligibility only. The complete combined-reader
 * and memory-allocation admission still runs at game launch. */
struct kui_vmu_wizard_game { char title[129],product[12]; bool selected,supported; };
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
};
/* Storage worker owns the RAM-only session. Every returned view is a copy;
 * physical sources remain read-only. Publication requires COMMIT from review. */
void kui_vmu_wizard_run(const struct kui_vmu_wizard_request *request,
    struct kui_vmu_wizard_view *out,kui_log_fn log,kui_cancel_fn cancel,
    kui_app_progress_fn progress);
#endif
