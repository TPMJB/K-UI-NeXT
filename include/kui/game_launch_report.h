/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAME_LAUNCH_REPORT_H
#define KUI_GAME_LAUNCH_REPORT_H
#include "kui/probe.h"

#define KUI_GAME_LAUNCH_REPORT_PATH_A "0:/KUI/apps/games/last-launch-a.txt"
#define KUI_GAME_LAUNCH_REPORT_PATH_B "0:/KUI/apps/games/last-launch-b.txt"
#define KUI_GAME_LAUNCH_REPORT_MAX 8192u
struct kui_game_ram_patches;
/* These are actual prepared values, not unnormalized saved menu choices.
 * Strings must terminate within their field limit (excluding NUL): version,
 * build_id, reader, cdda_profile and product 64 bytes; storage/encoding 32;
 * title/bootfile 128; image/package paths 511 after canonical normalization.
 * Empty title/product are allowed. Other text fields must be nonempty.
 * Text control bytes and backslashes are escaped; paths are strictly checked.
 * NULL patches means both entries are off. Executable CRC/bytes describe the
 * source file before descrambling (including the WinCE prefix if present).
 * No game bytes are recorded. */
struct kui_game_launch_report {
    const char *version,*build_id,*image_path,*reader,*storage,*encoding;
    const char *cdda_profile,*package_path,*product,*title,*bootfile;
    bool cdda;
    uint32_t ip_crc32,ip_bytes,executable_crc32,executable_bytes;
    const struct kui_game_ram_patches *patches;
};

/* Human-readable v1 text: fixed-width decimal sequence, fixed field order,
 * and CRC32 footer covering every preceding byte. Sequence zero, malformed
 * fields, trailing bytes and corrupt checksums are rejected. Failure leaves
 * caller output/size/sequence unchanged. Text explicitly records preparation
 * before handoff; it is never evidence that the game started successfully. */
bool kui_game_launch_report_encode(void *record,size_t capacity,size_t *size,
        const struct kui_game_launch_report *report,uint64_t sequence);
bool kui_game_launch_report_inspect(const void *record,size_t size,uint64_t *sequence);

/* Caller owns the mounted volume and excludes concurrent file workers.
 * Read both bounded slots; highest valid sequence is authoritative. Identical
 * equal-sequence records choose A; conflicting equal sequences fail. Write
 * only the other/invalid slot, sync, close and compare a complete reread.
 * Real I/O errors fail; missing/corrupt slots may be ignored. Cancellation
 * is checked before any write. Temporary buffers live on a bounded heap.
 * The caller decides how to handle failure. Previous authoritative bytes are never opened
 * for writing; physical filesystem/power-loss guarantees remain device-specific.
 * To inspect manually, keep both last-launch-a.txt and last-launch-b.txt and
 * use the largest CRC-valid sequence; there is deliberately no mutable index. */
bool kui_game_launch_report_save(const struct kui_game_launch_report *report,
        kui_log_fn log,kui_cancel_fn cancelled);
#endif
