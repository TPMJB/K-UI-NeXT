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
    /* A virtual disc set keeps its real root and representative image path.
     * Empty for ordinary rows. Disc members retain their own exact paths. */
    char group[33];
    unsigned disc;
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
    char group[33],group_name[KUI_DEST_NAME_CAP];
};
enum kui_game_cdda_compatibility {
    KUI_GAME_CDDA_UNTESTED = 0,
    KUI_GAME_CDDA_TESTED,
    KUI_GAME_CDDA_TESTED_LIMITED
};
struct kui_games_detail {
    char path[KUI_GAMES_FILE_CAP];
    char title[129], product[17], region[9], boot_file[17];
    char message[128];
    uint64_t bytes;
    uint32_t boot_bytes, boot_lba;
    unsigned tracks, audio_tracks, data_tracks;
    struct kui_game_audio_info audio;
    /* Known tested disc release, never inferred from a title string. Final
     * executable and native-runtime guards still run before handoff. */
    enum kui_game_cdda_compatibility cdda_compatibility;
    bool valid, stopped;
    /* Inspection is useful even when this loader cannot boot the image.
     * CD music is optional and uses checked runtime adapters; unfamiliar
     * images remain experimental. With music Off, CD audio is silent.
     * Bare ISO/raw files do not reveal the original audio-track inventory. */
    bool native_gd, native_cd, windows_ce, scrambled, cd_image;
    /* Explicit inspection capability: native ZSO on a supported SCI path.
     * Valid metadata alone never grants compressed-image launch support. */
    bool zso_launch;
    unsigned zso_codec; /* Explicit decoder used by inspection, including errors. */
    /* The inspected original native GDI is on SCI. This chooses an unsaved
     * launch default only; executable/reader admission still runs at launch. */
    bool background_default;
    enum kui_game_image_format format;
    bool cover; /* Large box art was loaded for this image. */
};
/* One worker owns read-only SD access; offset counts entries, not pages.
 * Listing never creates /Games and never scans/hashes complete track files. */
bool kui_games_list(const char *root, unsigned offset, struct kui_games_page *out,
                    kui_log_fn log, kui_cancel_fn cancel);
/* Find a visible row by its FAT ASCII-casefolded name in the same sorted order
 * as list. Cached lookup performs no I/O; cold lookup does not resolve child
 * folders or read image payloads. Missing/error preserves offset. */
bool kui_games_find(const char *root,const char *name,unsigned *offset,
    kui_log_fn log,kui_cancel_fn cancel);
/* Storage worker only. Four roots share a bounded RAM catalogue across
 * pages/views and leave/return navigation. Explicit card/storage/content
 * refresh must clear both this cache and kui_games_covers_cache_clear().
 * No filesystem handles are retained. */
void kui_games_cache_clear(void);
/* RAM-only completeness fence for a persistent catalogue build. False while
 * discovery/filtering needed a retry, exceeded its cache bound, or a folder
 * still needs a successful resolution. Ordinary browsing stays fail-soft. */
bool kui_games_cache_complete(const char *root);
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
/* Validate track existence/length and bounded boot metadata. Candidates for
 * a known tested audio version also fingerprint IP and the complete boot
 * runtime. This is not disc verification or proof of game compatibility. */
bool kui_games_inspect(const char *path, struct kui_games_detail *out,
                       kui_log_fn log, kui_cancel_fn cancel);
bool kui_games_inspect_codec(const char *path,enum kui_zso_codec codec,struct kui_games_detail *out,
    kui_log_fn log,kui_cancel_fn cancel);
bool kui_games_inspect_with_codec(const char *path,enum kui_zso_codec codec,struct kui_games_detail *out,
    kui_games_mounted_fn mounted,void *ctx,kui_log_fn log,kui_cancel_fn cancel);
bool kui_games_inspect_with(const char *path, struct kui_games_detail *out,
                            kui_games_mounted_fn mounted, void *ctx, kui_log_fn log, kui_cancel_fn cancel);
#endif
