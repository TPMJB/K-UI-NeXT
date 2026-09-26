/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_H
#define KUI_GAMES_H
#include "kui/apps.h"
#include "kui/destination.h"
#include "kui/game_cover.h"
#define KUI_GAMES_ROWS 8u
#define KUI_GAMES_FILE_CAP KUI_DEST_PATH_CAP
struct kui_games_entry {
    char name[KUI_DEST_NAME_CAP];
    /* Card-root path, without drive prefix. A directory may target its single
     * GDI directly; ambiguous folders remain navigable directories. */
    char path[KUI_GAMES_FILE_CAP];
    /* What lists show: the disc title a box art scan recorded, else the
     * name. Empty from kui_games_list itself. cover: this row's pixels were
     * loaded for the page's view (games_covers.h). */
    char title[KUI_COVER_TITLE_CAP];
    bool directory, disabled, cover;
};
struct kui_games_page {
    struct kui_games_entry entries[KUI_GAMES_ROWS];
    unsigned count;
    /* Listable entries in the whole folder; zero if too many to count. */
    unsigned total;
    /* Set by the cover loader: the view its pixels suit, and whether a box
     * art folder exists at all. */
    unsigned view;
    bool has_more, artwork;
    char root[KUI_DEST_ROOT_CAP], message[128];
};
struct kui_games_detail {
    char path[KUI_GAMES_FILE_CAP];
    char title[129], product[17], region[9], boot_file[17];
    char message[128];
    uint64_t bytes;
    uint32_t boot_bytes, boot_lba;
    unsigned tracks, audio_tracks, data_tracks;
    bool valid, stopped;
    /* Inspection is useful even when this loader cannot boot the image.
     * CDDA is advisory: data reads may work but audio-play commands do not. */
    bool native_gd, windows_ce, high_density_audio;
    bool cover; /* Large box art was loaded for this image. */
};
/* One worker owns read-only SD access; offset counts entries, not pages.
 * Listing never creates /Games and never scans/hashes complete track files. */
bool kui_games_list(const char *root, unsigned offset, struct kui_games_page *out,
                    kui_log_fn log, kui_cancel_fn cancel);
/* Runs while the card is still mounted: after a successful listing, or after
 * an inspection whether or not the image was valid. It may read any file and
 * write under KUI/, and must leave no file or folder open. */
typedef void (*kui_games_mounted_fn)(void *ctx, kui_log_fn log, kui_cancel_fn cancel);
bool kui_games_list_with(const char *root, unsigned offset, struct kui_games_page *out,
                         kui_games_mounted_fn mounted, void *ctx, kui_log_fn log, kui_cancel_fn cancel);
/* The one GDI directly inside a folder, exactly as listings resolve a game
 * folder; false when there is none, several, or the folder is too large. */
bool kui_games_single_gdi(const char *root, char selected[KUI_GAMES_FILE_CAP], kui_cancel_fn cancel);
/* Validate every track's existence/length and bounded boot metadata. This is
 * image inspection, not content verification or proof of game compatibility. */
bool kui_games_inspect(const char *path, struct kui_games_detail *out,
                       kui_log_fn log, kui_cancel_fn cancel);
bool kui_games_inspect_with(const char *path, struct kui_games_detail *out,
                            kui_games_mounted_fn mounted, void *ctx, kui_log_fn log, kui_cancel_fn cancel);
#endif
