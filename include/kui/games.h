/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_H
#define KUI_GAMES_H
#include "kui/apps.h"
#include "kui/destination.h"
#include "kui/game_cover.h"
#include "kui/game_image.h"
#define KUI_GAMES_ROWS 8u
#define KUI_GAMES_FILE_CAP KUI_DEST_PATH_CAP
struct kui_games_entry {
    char name[KUI_DEST_NAME_CAP];
    /* Card-root path, without drive prefix. A directory may target its single
     * image directly; ambiguous folders remain navigable directories. */
    char path[KUI_GAMES_FILE_CAP];
    /* Optional sibling cooked-data GDI. path always selects the original;
     * pairing checks descriptors/layout only, never full track contents. */
    char variant_2048_path[KUI_GAMES_FILE_CAP];
    /* What lists show: a cached disc title, else the folder/file name.
     * Empty from kui_games_list itself. cover is selected artwork only. */
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
    struct kui_game_audio_info audio;
    bool valid, stopped;
    /* Inspection is useful even when this loader cannot boot the image.
     * CDDA is advisory: audio-play commands are acknowledged silently.
     * Bare ISO/raw files do not reveal the original audio-track inventory. */
    bool native_gd, native_cd, windows_ce, scrambled, cd_image;
    enum kui_game_image_format format;
    bool cover; /* Large box art was loaded for this image. */
};
/* One worker owns read-only SD access; offset counts entries, not pages.
 * Listing never creates /Games and never scans/hashes complete track files. */
bool kui_games_list(const char *root, unsigned offset, struct kui_games_page *out,
                    kui_log_fn log, kui_cancel_fn cancel);
/* Storage worker only. Four roots share a bounded RAM catalogue across
 * pages/views and leave/return navigation. Explicit card/storage/content
 * refresh must clear both this cache and kui_games_covers_cache_clear().
 * No filesystem handles are retained. */
void kui_games_cache_clear(void);
/* Runs while the card is still mounted: after a successful listing, or after
 * an inspection whether or not the image was valid. It may read any file and
 * write under KUI/, and must leave no file or folder open. */
typedef void (*kui_games_mounted_fn)(void *ctx, kui_log_fn log, kui_cancel_fn cancel);
bool kui_games_list_with(const char *root, unsigned offset, struct kui_games_page *out,
                         kui_games_mounted_fn mounted, void *ctx, kui_log_fn log, kui_cancel_fn cancel);
/* The one GDI directly inside a folder, exactly as listings resolve a game
 * folder; false when there is none, several, or the folder is too large. */
bool kui_games_single_gdi(const char *root, char selected[KUI_GAMES_FILE_CAP], kui_cancel_fn cancel);
/* The one visible image selector directly inside a folder. Referenced GDI/CUE
 * payload files are excluded; ambiguous folders remain navigable. */
bool kui_games_single_image(const char *root, char selected[KUI_GAMES_FILE_CAP], kui_cancel_fn cancel);
/* Read-only storage-worker visitor while the card is already mounted. Visits
 * immediate visible directories and image selectors using the listing's
 * descriptor-payload filter. Returning false stops and closes the directory. */
typedef bool (*kui_games_image_visit_fn)(void *ctx,const char *name,bool directory);
bool kui_games_visit_images(const char *root,kui_games_image_visit_fn visit,void *ctx,kui_cancel_fn cancel);
/* Validate every track's existence/length and bounded boot metadata. This is
 * image inspection, not content verification or proof of game compatibility. */
bool kui_games_inspect(const char *path, struct kui_games_detail *out,
                       kui_log_fn log, kui_cancel_fn cancel);
bool kui_games_inspect_with(const char *path, struct kui_games_detail *out,
                            kui_games_mounted_fn mounted, void *ctx, kui_log_fn log, kui_cancel_fn cancel);
#endif
