/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_REPAIR_COPY_H
#define KUI_REPAIR_COPY_H
#include "kui/recovery_scan.h"

#define KUI_FORENSIC_SECTOR_LIMIT 64u
#define KUI_REPAIR_SECTOR_LIMIT 8192u
enum kui_repair_result { KUI_REPAIR_FAILED, KUI_REPAIR_STOPPED,
    KUI_REPAIR_UNRESOLVED, KUI_REPAIR_COMPLETE };
enum kui_repair_phase { KUI_REPAIR_IDENTIFY, KUI_REPAIR_COPYING,
    KUI_REPAIR_READBACK, KUI_REPAIR_PUBLISHING };
struct kui_repair_status {
    enum kui_repair_result result;
    enum kui_repair_phase phase;
    bool complete, imported, storage_unstable, audio_unverified;
    unsigned track, tracks;
    uint32_t fad, suspects, repaired, unresolved, attempts;
    uint64_t done, total, elapsed_ms;
    char job[KUI_DEST_JOB_CAP], message[128];
};
struct kui_repair_options {
    unsigned attempts; /* Exactly 1, 5, 10, 20 or 50 candidate pairs per sector. */
    void (*progress)(void *ctx,const struct kui_repair_status *status);
};
/* Explicit repair-to-copy, never in-place. Caller prepares the inserted disc
 * and connects storage; this worker mounts/unmounts FatFs. Supports completed
 * ordinary raw GDI jobs and imported raw-2352 GDI folders with safe names and
 * zero offsets. The saved track ranges must exactly match prepared_plan.
 *
 * Normal jobs require their complete optical capture identity. Imports require
 * two distinct healthy saved high-density data sectors to match optical bytes. Only bad
 * supported Mode 1 sectors can be localized; whole-track hash mismatches and
 * audio cannot be automatically localized. Candidates need two identical
 * optical reads with correct FAD/EDC/PQ. Each suspect is reopened twice before
 * optical repair; disagreement blocks repair and retains binary evidence.
 *
 * Original tracks/descriptor/manifest/checkpoints are opened READ ONLY. A new
 * /KUI/repair/job-NNNN retains synced original/candidate evidence and a .part
 * log if stopped/failed. No playable GDI is published before every copied track
 * has full saved-byte hash readback and all available source hashes agree.
 * There is no automatic resume: a later run creates a separate copy; old partial
 * jobs remain evidence. A completed imported copy can still have unverified
 * audio; COMPLETE never means catalogue/archival verification.
 */
enum kui_repair_result kui_repair_copy(const char *directory,
    const struct kui_capture_plan *prepared_plan,const struct kui_capture_ops *ops,
    const struct kui_repair_options *options,struct kui_repair_status *out);

enum kui_forensic_result { KUI_FORENSIC_FAILED, KUI_FORENSIC_STOPPED,
    KUI_FORENSIC_STABLE, KUI_FORENSIC_UNSTABLE };
struct kui_forensic_status {
    enum kui_forensic_result result;
    bool complete, limit_reached;
    unsigned track, tracks;
    uint32_t suspects, sampled, unstable, unsupported;
    uint64_t done, total, elapsed_ms;
    char report[KUI_DEST_PATH_CAP], evidence[KUI_DEST_PATH_CAP], message[128];
};
struct kui_forensic_ops {
    void *ctx;
    bool (*cancelled)(void *);
    uint64_t (*now_ms)(void *);
    void (*progress)(void *,const struct kui_forensic_status *);
    kui_log_fn log;
};
/* Storage-only diagnostic: read tracks once, reopen/read each of at most 64
 * structurally suspect Mode 1 sectors twice, and save all three raw samples,
 * byte difference counts/first offsets and CRCs. Stable means those sampled
 * reads agreed, not that media/hashes/audio are correct. Original files remain
 * read-only. Report and binary evidence are unique; .part is incomplete.
 */
enum kui_forensic_result kui_recovery_forensic(const char *directory,
    const struct kui_forensic_ops *ops,struct kui_forensic_status *out);
#endif
