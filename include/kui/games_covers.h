/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_COVERS_H
#define KUI_GAMES_COVERS_H
#include "kui/games.h"
#include "kui/apps.h"

/* Games views: a list beside the selected game's large cover, a two-column
 * list with small covers, and a four-by-two gallery. All show eight rows of
 * the same listing, so a page is the same in every view. */
enum kui_games_view {
    KUI_GAMES_VIEW_LIST, KUI_GAMES_VIEW_COMPACT, KUI_GAMES_VIEW_GALLERY, KUI_GAMES_VIEW_COUNT
};
/* A page request that should use the view saved on the card. */
#define KUI_GAMES_VIEW_SAVED KUI_GAMES_VIEW_COUNT
#define KUI_GAMES_COVERS_FOLDER "0:/KUI/covers"
#define KUI_GAMES_VIEW_FILE "0:/KUI/apps/games/view.txt"
#define KUI_GAMES_SCAN_ROOT "/Games"
/* Game folders may sit inside two levels of category folders. */
#define KUI_GAMES_SCAN_DEPTH 3u
#define KUI_GAMES_SCAN_MAX 2000u
#define KUI_GAMES_ART_MAX_BYTES (2u * 1024u * 1024u)

enum kui_cover_size kui_games_view_size(unsigned view);
const char *kui_games_view_name(unsigned view);

/* kui_games_list, then while mounted: every row's display title and each GDI
 * row's cover in the view's size, into pixels[row] (rows without a record
 * are untouched). view SAVED reads the card's choice; any other view is
 * saved when it differs. out->view is SAVED if the listing failed. Cover and
 * view problems never fail the listing. */
bool kui_games_list_covers(const char *root, unsigned offset, unsigned view,
    struct kui_games_page *out, uint16_t (*pixels)[KUI_COVER_PIXELS],
    kui_log_fn log, kui_cancel_fn cancel);
/* kui_games_inspect, plus the large cover when a scan recorded one for
 * exactly this GDI path. */
bool kui_games_inspect_cover(const char *path, struct kui_games_detail *out,
    uint16_t pixels[KUI_COVER_PIXELS], kui_log_fn log, kui_cancel_fn cancel);

struct kui_games_scan_counts {
    unsigned games, disc, user, none, unchanged, failed, duplicates;
};
/* Box art for every game below /Games. A record whose GDI and optional owner
 * image are unchanged is kept; others are rebuilt from KUI/covers/<name>.png,
 * .jpg or .jpeg, else from the disc's 0GDTEX.PVR, else title only. Game files
 * are only read; only KUI/covers is written. progress receives status after
 * every game; B (cancel) stops between reads and keeps finished records. */
bool kui_games_scan(struct kui_app_status *status, struct kui_games_scan_counts *counts,
    kui_app_progress_fn progress, kui_log_fn log, kui_cancel_fn cancel);
#endif
