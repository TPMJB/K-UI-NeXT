/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MUSIC_PLAYER_H
#define KUI_MUSIC_PLAYER_H
#include "kui/apps.h"
#include "kui/destination.h"
#define KUI_MUSIC_PLAYER_ROWS 8u
struct kui_music_player_entry {char name[KUI_DEST_NAME_CAP];bool directory,disabled;};
struct kui_music_player_page {
    struct kui_music_player_entry entries[KUI_MUSIC_PLAYER_ROWS];
    unsigned count;bool has_more;
    char root[KUI_DEST_ROOT_CAP],message[128];
};
/* Single-worker, read-only SD operations. Listing includes subdirectories and
 * .wav files only; offset counts entries, not pages. Paths use '/Music/song.wav'. */
bool kui_music_player_list(const char *root,unsigned offset,
    struct kui_music_player_page *out,kui_log_fn log,kui_cancel_fn cancel);
/* Preload a PCM16 WAV (mono/stereo 8–44.1 kHz, <=6 MiB) into the shared
 * background player, then return. SD is released before playback starts; failed
 * or cancelled replacement keeps the previously selected song. */
void kui_music_player_run(const char *path,unsigned volume,struct kui_app_status *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
#endif
