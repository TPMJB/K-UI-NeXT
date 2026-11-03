/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAME_PROFILE_H
#define KUI_GAME_PROFILE_H
#include "kui/game_launch_settings.h"
#include "kui/game_image.h"

#define KUI_GAME_PROFILE_PACK_ROWS 16u
#define KUI_GAME_PROFILE_PACK_HEADER 32u
#define KUI_GAME_PROFILE_PACK_ROW_BYTES 256u
#define KUI_GAME_PROFILE_PACK_MAX (KUI_GAME_PROFILE_PACK_HEADER + KUI_GAME_PROFILE_PACK_ROWS * KUI_GAME_PROFILE_PACK_ROW_BYTES + 4u)
#define KUI_GAME_PROFILE_PACK_PATH "0:/KUI/profiles/game-profiles.bin"
enum kui_game_profile_status {
    KUI_GAME_PROFILE_EXPERIMENTAL, KUI_GAME_PROFILE_TESTED, KUI_GAME_PROFILE_CUSTOM
};
struct kui_game_profile_identity {
    unsigned transport,format,audio_candidate;
    uint32_t boot_bytes,ip_crc32;
    char product[17],version[7],region[9];
    bool windows_ce,native_gd;
    uint8_t boot_sha256[32];
};
/* This is a recommendation, never a loader admission or a claim that this
 * build was console-tested. Tested rows name historical repository evidence.
 * External packs are always Experimental and require explicit Apply. */
struct kui_game_profile_match {
    bool matched,automatic;
    enum kui_game_profile_status status;
    char name[48],note[112],evidence[112],tested_build[13];
    struct kui_game_launch_settings recommended;
};
struct kui_game_profile_row {
    struct kui_game_profile_identity identity;
    char name[48],note[96];
    struct kui_game_launch_settings recommended;
};
struct kui_game_profile_pack {
    unsigned count;
    struct kui_game_profile_row rows[KUI_GAME_PROFILE_PACK_ROWS];
};

/* Candidate lookup examines only bounded metadata/size; it grants no label.
 * Only candidates pay for full IP and executable hashing during inspection. */
bool kui_game_profile_candidate(const struct kui_game_profile_identity *,const struct kui_game_profile_pack *);
/* Output is reset for no match. Every match requires complete executable SHA,
 * exact size, image format and transport; audio rows also require the existing
 * exact-disc geometry candidate and full IP CRC. Built-in rows take precedence. */
bool kui_game_profile_query(const struct kui_game_profile_identity *,const struct kui_game_profile_pack *,struct kui_game_profile_match *);
const char *kui_game_profile_status_name(enum kui_game_profile_status);
enum kui_game_profile_status kui_game_profile_settings_status(const struct kui_game_profile_match *,const struct kui_game_launch_settings *,bool saved);
/* Explicit saved options always win, including Standard/Off choices. With no
 * saved entry, only automatic historical built-ins may replace the caller's
 * inspected baseline. Imported Experimental recommendations stay opt-in.
 * Failure preserves output. This performs no filesystem writes. */
bool kui_game_profile_resolve(const struct kui_game_profile_match *,const struct kui_game_launch_settings *baseline,
    const struct kui_game_launch_settings *saved,bool found,struct kui_game_launch_settings *out);
/* Explicit Apply copies a valid matched recommendation; the caller persists
 * it through the existing launch-settings transaction and rechecks admission. */
bool kui_game_profile_apply(const struct kui_game_profile_match *,struct kui_game_launch_settings *out);

/* Fixed-width version1 pack with CRC32 footer; no executable, patch bytes,
 * addresses or Tested flags. Decode rejects duplicate keys, malformed strings,
 * invalid settings, unknown/reserved fields and non-SCI/non-GDI records.
 * GDI encoding is Automatic; CE uses Background, native CDDA uses Standard.
 * Encode/decode preserve all outputs on failure. CRC is integrity, not trust. */
bool kui_game_profile_pack_encode(const struct kui_game_profile_pack *,void *,size_t,size_t *);
bool kui_game_profile_pack_decode(struct kui_game_profile_pack *,const void *,size_t);
/* Already-mounted worker only; absent pack is an empty successful result.
 * An invalid/unreadable pack is ignored with a warning; callers may continue
 * using built-ins. Returns false and zeroes pack on that failure. */
bool kui_game_profile_pack_load(struct kui_game_profile_pack *,kui_log_fn);
#endif
