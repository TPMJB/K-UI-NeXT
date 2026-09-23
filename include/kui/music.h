/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MUSIC_H
#define KUI_MUSIC_H
#include "kui/probe.h"
#define KUI_MUSIC_TRACKS 5u
#define KUI_MUSIC_FILE_MAX (2u*1024u*1024u)
struct kui_music_status {
    bool enabled,loaded,playing,paused;
    unsigned volume,current_index;
    uint32_t sample_rate,pcm_bytes;
    char title[40],message[128];
};
/* All calls belong to the single I/O worker. UI code copies status under its
 * normal publication lock. init does not touch storage or initialize audio. */
void kui_music_init(kui_log_fn log);
const char *kui_music_track_name(unsigned index);
const char *kui_music_track_file(unsigned index);
const struct kui_music_status *kui_music_status(void);
void kui_music_set_config(bool enabled,unsigned volume_percent);
/* Stop the old song, read one bounded WAV from /KUI/apps/music, close/unmount
 * and disconnect SD before returning. Missing files are not retried by service.
 * On success call resume when idle. Cancel is checked between bounded reads. */
bool kui_music_load(unsigned index,kui_cancel_fn cancel);
/* pause destroys the stream and drains its DMA before other hardware work.
 * resume uses cached PCM only; service must be called ~every16ms while idle.
 * Neither performs filesystem I/O. Off and pause retain the one cached track.
 * Playback failures latch until an explicit set_config or load call, so an
 * idle loop may safely call resume repeatedly without retry/log flooding. */
void kui_music_pause(void);
void kui_music_resume(void);
void kui_music_service(void);
void kui_music_shutdown(void);
/* Drain and release the global KOS streamer while retaining menu PCM/config.
 * A foreground Music app may then own a differently sized stream. */
void kui_music_release_audio(void);
/* Worker-only original startup cue, about 4.5 seconds, no SD access. The
 * callback allows B to skip and retains any cached menu PCM/config. */
void kui_music_play_boot_chime(kui_cancel_fn cancel);
#endif
