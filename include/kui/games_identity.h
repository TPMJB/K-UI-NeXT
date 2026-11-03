/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_IDENTITY_H
#define KUI_GAMES_IDENTITY_H
#include "kui/apps.h"
#include "kui/zso.h"
#include "kui/games_group.h"
struct kui_games_identity {
    uint8_t digest[32];
    char product[11],version[7],region[9],title[129];
    unsigned disc;
    /* Data backing: 1 cooked2048, 2 raw, 3 mixed. Separate from the canonical
     * content digest so equivalent dialects keep independent launch choices. */
    unsigned representation;
    uint64_t bytes;
};
/* Explicit, potentially slow whole-image operation on the storage worker.
 * SHA-256 covers canonical track geometry and every represented data-sector
 * payload (2048 bytes) and audio main-channel sector (2352 bytes). Paths,
 * filenames, raw/cooked data strides, EDC/ECC and subchannels are excluded.
 * Thus equivalent Original/2048 layouts match; different geometry, audio,
 * game revisions or payloads do not. This is a library identity, not Redump
 * verification or proof that omitted tracks ever existed.
 *
 * bytes is the total canonical payload byte count. disc is the strict 1..9
 * IP.BIN GD-ROMn/m or CD-ROMn/m field, otherwise 0. Valid boot metadata is
 * required. Supports the opener's uncompressed GDI/ISO/CUE/CDI/raw formats;
 * compressed images are deliberately refused in this first slice. codec is
 * explicit and follows game_image_open_named_codec's non-ZSO rules.
 *
 * Uses bounded heap workspace and one streaming file handle. Owns a Games
 * storage session; no handle survives return. Cancellation/read/close/mount
 * failures preserve *out. Never called implicitly by directory scanning. */
bool kui_games_identity_verify(const char *path,enum kui_zso_codec codec,
    struct kui_games_identity *out,kui_log_fn log,kui_cancel_fn cancel);
enum kui_games_group_read_result {
    KUI_GAMES_GROUP_READ_OK, KUI_GAMES_GROUP_READ_UNSUPPORTED,
    KUI_GAMES_GROUP_READ_IO, KUI_GAMES_GROUP_READ_CANCELLED
};
/* Bounded worker-only grouping evidence: parse the image descriptor and read
 * one IP sector, never hash game/audio data. The caller owns a mounted Games
 * session; all image handles close before return. Output is unchanged unless
 * OK. Unsupported/malformed metadata is distinct from an I/O failure. */
enum kui_games_group_read_result kui_games_identity_group(const char *path,
    struct kui_games_group_evidence *out,kui_cancel_fn cancel);
#endif
