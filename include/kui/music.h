/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MUSIC_H
#define KUI_MUSIC_H
#include "kui/probe.h"
#define KUI_MUSIC_TRACKS 5u
#define KUI_MUSIC_CUSTOM_INDEX KUI_MUSIC_TRACKS
#define KUI_MUSIC_FILE_MAX (2u*1024u*1024u)
#define KUI_MUSIC_CUSTOM_MAX (6u*1024u*1024u)
#define KUI_MUSIC_CACHE_MAX (8u*1024u*1024u)
struct kui_music_status {
    bool enabled,loaded,playing,paused;
    unsigned volume,current_index,cached_mask;
    uint32_t sample_rate,pcm_bytes,cache_bytes;
    char title[40],message[128];
};
/* One audio thread polls RAM-only callbacks. Public controls/status copies
 * serialize with it; only the existing I/O worker may call preload functions.
 * The audio mutex is never held across SD reads or calls to the app logger. */
void kui_music_init(kui_log_fn log);
const char *kui_music_track_name(unsigned index);
const char *kui_music_track_file(unsigned index);
void kui_music_status_copy(struct kui_music_status *out);
/* Compatibility snapshot for single-threaded host clients; console/UI callers
 * should use status_copy, not retain a pointer across controls. */
const struct kui_music_status *kui_music_status(void);
void kui_music_set_config(bool enabled,unsigned volume_percent);
/* Preload one bundled track without changing the selected song. Failed or
 * cancelled replacements retain current playback; inactive cache may be evicted.
 * The cache budget includes the temporary new allocation, at most 8 MiB total. */
bool kui_music_cache_menu(unsigned index,kui_cancel_fn cancel);
bool kui_music_load(unsigned index,kui_cancel_fn cancel);
/* Normalize/validate the caller's card path before passing it here. PCM16 WAV,
 * up to 6 MiB including headers. All I/O finishes before selecting the cache. */
bool kui_music_load_path(const char *path,const char *title,kui_cancel_fn cancel);
/* No card I/O, safe while capture owns SD. Returns false if track isn't cached.
 * step_cached traverses the five bundled songs; the custom song is selected by
 * its browser entry. Returns the wanted index when missing, via wanted. */
bool kui_music_select_cached(unsigned index);
bool kui_music_step_cached(int direction,unsigned *wanted);
void kui_music_pause(void);
void kui_music_resume(void);
/* Normally called by the audio service thread; exposed for deterministic host
 * tests. Never reads SD. A playback failure latches until explicit controls. */
void kui_music_service(void);
void kui_music_shutdown(void);
/* Drain and release global KOS streamer, retaining cache/config. Used only for
 * the startup cue and exiting to the BIOS, not ordinary foreground work. */
void kui_music_release_audio(void);
void kui_music_play_boot_chime(kui_cancel_fn cancel);
#endif
