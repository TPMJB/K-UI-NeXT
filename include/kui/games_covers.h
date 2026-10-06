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

/* Rows and cached display titles only, without artwork reads. SAVED loads the
 * view preference once; explicit changes save once. Warm rows use RAM only.
 * Worker only; refresh/card changes invalidate both Games and cover caches. */
bool kui_games_list_rows(const char *root, unsigned offset, unsigned view,
    struct kui_games_page *out, kui_log_fn log, kui_cancel_fn cancel);
struct kui_games_cover_result {
    char path[KUI_GAMES_FILE_CAP], name[KUI_DEST_NAME_CAP], title[KUI_COVER_TITLE_CAP];
    enum kui_cover_size size;
    bool cover, stopped;
};
/* A selected game's artwork only. Missing records/artwork are successful
 * negative results. I/O/cancel failures are not cached. The result echoes the
 * entry identity/size for publication checks; handles close before return.
 * A bounded RAM cache keeps positive and negative results per identity/size. */
bool kui_games_selected_cover(const struct kui_games_entry *entry, enum kui_cover_size size,
    uint16_t pixels[KUI_COVER_PIXELS], struct kui_games_cover_result *out,
    kui_log_fn log, kui_cancel_fn cancel);
void kui_games_covers_cache_clear(void);
/* Compatibility wrapper: the page contains rows only; pixels is untouched.
 * All views load artwork separately for their selected entry. */
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
