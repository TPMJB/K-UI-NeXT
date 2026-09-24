/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_H
#define KUI_GAMES_H
#include "kui/apps.h"
#include "kui/destination.h"
#define KUI_GAMES_ROWS 8u
#define KUI_GAMES_FILE_CAP KUI_DEST_PATH_CAP
struct kui_games_entry {
    char name[KUI_DEST_NAME_CAP];
    /* Card-root path, without drive prefix. A directory may target its single
     * GDI directly; ambiguous folders remain navigable directories. */
    char path[KUI_GAMES_FILE_CAP];
    bool directory, disabled;
};
struct kui_games_page {
    struct kui_games_entry entries[KUI_GAMES_ROWS];
    unsigned count;
    bool has_more;
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
};
/* One worker owns read-only SD access; offset counts entries, not pages.
 * Listing never creates /Games and never scans/hashes complete track files. */
bool kui_games_list(const char *root, unsigned offset, struct kui_games_page *out,
                    kui_log_fn log, kui_cancel_fn cancel);
/* Validate every track's existence/length and bounded boot metadata. This is
 * image inspection, not content verification or proof of game compatibility. */
bool kui_games_inspect(const char *path, struct kui_games_detail *out,
                       kui_log_fn log, kui_cancel_fn cancel);
#endif
