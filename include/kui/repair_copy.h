/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_REPAIR_COPY_H
#define KUI_REPAIR_COPY_H
#include "kui/recovery_scan.h"

#define KUI_FORENSIC_SECTOR_LIMIT 64u
enum kui_repair_result { KUI_REPAIR_FAILED, KUI_REPAIR_STOPPED,
    KUI_REPAIR_UNRESOLVED, KUI_REPAIR_COMPLETE };
enum kui_repair_phase { KUI_REPAIR_IDENTIFY, KUI_REPAIR_COPYING,
    KUI_REPAIR_READBACK, KUI_REPAIR_PUBLISHING };
struct kui_repair_status {
    enum kui_repair_result result;
    enum kui_repair_phase phase;
    bool complete, imported, storage_unstable, audio_unverified, resumed, partial_source;
    bool in_place, catalogue_checked, alternate_identity, resume_ready;
    struct kui_known_summary catalogue;
    unsigned track, tracks;
    uint32_t fad, suspects, repaired, unresolved, attempts, pass;
    uint64_t done, total, elapsed_ms;
    char job[KUI_DEST_JOB_CAP], message[128];
};
struct kui_repair_options {
    unsigned attempts; /* Exactly 1, 5, 10, 20 or 50 candidate pairs per sector. */
    void (*progress)(void *ctx,const struct kui_repair_status *status);
    bool continuous; /* Finish collection, then sweep unresolved targets. */
    bool in_place; /* Explicit raw-source repair/partial continuation; old bytes retained. */
    unsigned passes; /* continuous only: 0 until stopped, else 1/5/10/20/50. */
    bool allow_saved_identity; /* Explicit verified-prefix alternate-anchor admission. */
    bool new_copy; /* Explicit new separate-copy transaction; never resumes old jobs. */
};
/* Explicit repair worker. Default preserves separate-copy finite operation.
 * continuous collects missing sectors as tracked placeholders then retries
 * unresolved holes in alternating sweeps. in_place is a separate explicit
 * operation on supported raw-track sources: durable original/candidate evidence
 * precedes mutations; interruption retains a checked resume job. Partial or
 * unreadable results are never published complete. Caller prepares the exact
 * inserted disc and connects storage; worker owns mount/unmount. */
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
