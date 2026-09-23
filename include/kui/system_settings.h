/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SYSTEM_SETTINGS_H
#define KUI_SYSTEM_SETTINGS_H
#include "kui/probe.h"

enum kui_video_mode {
    KUI_VIDEO_AUTO,
    KUI_VIDEO_NTSC60,
    KUI_VIDEO_PAL50,
    KUI_VIDEO_MODE_COUNT
};
struct kui_system_settings {
    unsigned video_mode;
    bool show_memory;
    bool music_enabled;
    unsigned music_volume;
};
#define KUI_SYSTEM_SETTINGS_RECORD_SIZE 40u
#define KUI_SYSTEM_SETTINGS_PATH_A "0:/KUI/apps/system/settings-a.bin"
#define KUI_SYSTEM_SETTINGS_PATH_B "0:/KUI/apps/system/settings-b.bin"

void kui_system_settings_default(struct kui_system_settings *out);
bool kui_system_settings_valid(const struct kui_system_settings *settings);
const char *kui_system_video_name(unsigned mode);
/* Fixed little-endian version 1. Reserved bits/bytes must be zero. The CRC
 * covers all preceding bytes; sequence zero and wrapping are forbidden. */
bool kui_system_settings_encode(uint8_t out[KUI_SYSTEM_SETTINGS_RECORD_SIZE],
                                const struct kui_system_settings *settings,uint64_t sequence);
bool kui_system_settings_decode(struct kui_system_settings *settings,uint64_t *sequence,
                                const void *record,size_t size);
/* The caller owns the mounted volume and excludes other file workers.
 * Load never writes. Only when both files are absent does it carry the legacy
 * memory-display preference forward; invalid records fall back to defaults.
 * A genuine read error returns false. Save alternates slots, syncs/closes and
 * rereads exact bytes. It never opens the current valid slot for writing.
 * Video mode changes/preview confirmation belong to the platform/UI, not here. */
bool kui_system_settings_load(struct kui_system_settings *out,bool legacy_show_memory,kui_log_fn log);
bool kui_system_settings_save(const struct kui_system_settings *settings,kui_log_fn log);
#endif
