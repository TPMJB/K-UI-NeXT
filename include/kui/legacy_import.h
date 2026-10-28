/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_LEGACY_IMPORT_H
#define KUI_LEGACY_IMPORT_H
#include "kui/recovery_scan.h"
#include "kui/recovery_checks.h"

#define KUI_LEGACY_STATE_LIMIT 32768u
#define KUI_LEGACY_TARGET_BYTES ((KUI_RECOVERY_FAD_MAX+8u)/8u)
enum kui_legacy_result { KUI_LEGACY_ABSENT, KUI_LEGACY_INVALID,
    KUI_LEGACY_STOPPED, KUI_LEGACY_READY };
struct kui_legacy_job {
    struct kui_scan_gdi source;
    uint32_t declared[99], available[99], crc[99];
    uint8_t sha256[99][32];
    uint32_t checkpoint_sectors[99], checkpoint_crc[99];
    bool checkpoint_valid[99];
    bool partial, boot_identity;
    uint32_t identity_fad[2];
    uint8_t identity_sha256[2][32];
    uint32_t suspect_records, bad_records;
    uint8_t binding[32]; /* Read-only source/metadata receipt, not a catalogue hash. */
    uint8_t target_bits[KUI_LEGACY_TARGET_BYTES]; /* Absolute FAD bits; bounded <90 KiB. */
    char message[128];
};
/* Strict raw-2352 subset of DreamShell GD Ripper state v1. The legacy state
 * has no durable progress or optical identity of its own. This parser only
 * validates declared layout/names against the prepared disc. CD data's legacy
 * two-sector exclusion is admitted as a missing tail, never guessed bytes.
 * No filesystem or optical I/O; out is unchanged on failure. */
bool kui_legacy_state_parse(const void *data,size_t bytes,
    const struct kui_capture_plan *prepared,struct kui_legacy_job *out);
/* Called with the correct local volume already mounted by the sole worker.
 * Opens every original READ ONLY. Checks state, bounded sidecar syntax/ranges,
 * CRC1 prefix receipts against actual bytes, and independently repeated saved
 * reads. GD requires saved rip.disc (or raw track03 boot payload) to match two
 * validated optical boot observations. CD uses two distinct saved/optical
 * anchors, including paired audio observations. All optical reads are bounded;
 * FATAL and Stop exit immediately. No metadata is rewritten or migrated here.
 *
 * READY admits saved prefixes for explicit repair/continuation, not ordinary
 * independent-checkpoint Resume. Legacy recovery baseline records are checked;
 * their original target set becomes explicit forced rereads, including audio.
 * Old candidates/backups are never imported as independent patch authority.
 * Cooked legacy jobs are refused. Missing state returns ABSENT only.
 */
enum kui_legacy_result kui_legacy_import(const char *directory,
    const struct kui_capture_plan *prepared,const struct kui_capture_ops *ops,
    struct kui_legacy_job *out);
bool kui_legacy_target_is_marked(const struct kui_legacy_job *job,
    unsigned track,uint32_t sector);
/* Recheck precisely the immutable admitted optical anchors, without trusting
 * fresh source hashes or touching files modified by a later explicit repair.
 * This is also suitable after a successful command-safe drive reinit. */
enum kui_legacy_result kui_legacy_identity_check(const struct kui_legacy_job *job,
    const struct kui_capture_ops *ops);
#endif
