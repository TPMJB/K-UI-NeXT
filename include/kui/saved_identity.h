/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SAVED_IDENTITY_H
#define KUI_SAVED_IDENTITY_H
#include "kui/capture.h"
#define KUI_SAVED_IDENTITY_BYTES 124u
struct kui_saved_identity_source {
    const char *directory;
    const struct kui_capture_plan *plan;
    const char (*files)[KUI_DEST_NAME_CAP];
    const uint32_t *sectors,*crc32;
    const uint8_t (*sha256)[32];
    const uint8_t *capture_identity;
    bool crc_only;
};
struct kui_saved_identity {
    enum kui_capture_profile profile;
    uint32_t fad[2];
    uint8_t sha256[2][32],source_binding[32];
};
enum kui_saved_identity_result {
    KUI_SAVED_IDENTITY_MATCH, KUI_SAVED_IDENTITY_UNREADABLE,
    KUI_SAVED_IDENTITY_DIFFERENT, KUI_SAVED_IDENTITY_SOURCE_INVALID,
    KUI_SAVED_IDENTITY_FAILED, KUI_SAVED_IDENTITY_STOPPED
};
/* The caller must explicitly opt into a new repair binding ONLY after exact
 * original identity reads are exhausted by unreadability. A valid original
 * digest disagreement must never take this path. It is not ordinary Resume.
 * The mounted, sole-worker helper verifies every admitted saved prefix twice
 * against the original recorded CRC/SHA, then compares any readable original
 * fixed sample inside those prefixes. A valid disagreement rejects the disc.
 * It then discovers two distinct valid saved/paired-optical anchors, GD HD
 * data only, or CD data/audio. No source writes occur. Existing receipts must
 * use recheck instead; never rediscover replacement anchors on Resume/reset.
 * Output receipt is unchanged on failure. Message is optional, cap128. */
enum kui_saved_identity_result kui_saved_identity_discover(
    const struct kui_saved_identity_source *source,const struct kui_capture_ops *ops,
    unsigned attempts,struct kui_saved_identity *out,char message[128]);
/* Metadata equality is a new binding of the exact current admitted plan and
 * trusted prefix receipts. A checkpoint's old opaque identity alone does not
 * prove that plan was its original TOC. This makes no such claim. */
bool kui_saved_identity_matches_source(const struct kui_saved_identity_source *source,
    const struct kui_saved_identity *identity);
enum kui_saved_identity_result kui_saved_identity_recheck(
    const struct kui_saved_identity *identity,const struct kui_capture_plan *plan,
    const struct kui_capture_ops *ops,unsigned attempts,char message[128]);
void kui_saved_identity_encode(const struct kui_saved_identity *identity,
    uint8_t out[KUI_SAVED_IDENTITY_BYTES]);
bool kui_saved_identity_decode(const uint8_t data[KUI_SAVED_IDENTITY_BYTES],
    const struct kui_capture_plan *plan,struct kui_saved_identity *out);
/* Provisional stored-metadata decode: validates the record CRC, syntax and
 * ranges against the proposed plan, using the stored opaque identity. This is
 * NOT an optical identity match or proof of the original TOC. Admitted original
 * prefixes must still pass discovery/hash verification before any writes. */
bool kui_saved_checkpoint_decode(const uint8_t data[KUI_CHECKPOINT_BYTES],
    const struct kui_capture_plan *plan,struct kui_checkpoint *out);
#endif
