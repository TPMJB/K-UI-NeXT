/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SETTINGS_H
#define KUI_SETTINGS_H
#include "kui/probe.h"

struct kui_settings {
    bool crc_only;
    bool end_readback;
    bool show_memory;
};
#define KUI_SETTINGS_RECORD_SIZE 32u
#define KUI_SETTINGS_PATH_A "0:/KUI/settings-a.bin"
#define KUI_SETTINGS_PATH_B "0:/KUI/settings-b.bin"

void kui_settings_default(struct kui_settings *out);
/* Fixed little-endian version 1 record, independent of compiler padding.
 * CRC32 covers every byte except its own final four bytes. Sequence 0 is
 * invalid; the writer refuses to wrap UINT64_MAX. Decode failure preserves
 * the caller's values. */
bool kui_settings_encode(uint8_t out[KUI_SETTINGS_RECORD_SIZE],
                         const struct kui_settings *settings,uint64_t sequence);
bool kui_settings_decode(struct kui_settings *settings,uint64_t *sequence,
                         const void *record,size_t size);
/* Caller owns the mounted FatFs volume and excludes all other file workers.
 * Load starts at defaults. Missing/invalid records use defaults (true);
 * unreadable storage returns false. Neither function mounts or formats.
 * Save replaces the older/invalid slot, syncs/closes it, then checks the saved
 * record. The current valid slot is never opened for writing. Filesystem/card
 * power-loss guarantees are still limited by FatFs and the storage device. */
bool kui_settings_load(struct kui_settings *out,kui_log_fn log);
bool kui_settings_save(const struct kui_settings *settings,kui_log_fn log);
#endif
