/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SALVAGE_H
#define KUI_SALVAGE_H
#include "kui/capture.h"

/* Explicit damaged-disc workflow. Normal capture/checkpoint formats are never
 * read or written. The single I/O worker prepares the disc and connects SD,
 * then this function owns FatFs mount/unmount and the bounded read callback. */
#define KUI_SALVAGE_TARGET_LIMIT 8192u
enum kui_salvage_action { KUI_SALVAGE_NEW, KUI_SALVAGE_RESUME, KUI_SALVAGE_RECOVER };
enum kui_salvage_result { KUI_SALVAGE_FAILED, KUI_SALVAGE_STOPPED,
    KUI_SALVAGE_UNRESOLVED, KUI_SALVAGE_RESOLVED };
struct kui_salvage_status {
    enum kui_salvage_result result;
    bool first_pass_complete, complete;
    unsigned track, tracks, pass, pass_limit;
    uint32_t fad, targets, recovered, remaining, attempts;
    uint64_t done, total, elapsed_ms;
    char job[KUI_DEST_JOB_CAP], message[128];
};
struct kui_salvage_options {
    bool zero_fill; /* New jobs only. False stops before saving an unreadable chunk. */
    unsigned passes; /* RECOVER: exactly 1, 5, 10, 20 or 50. */
    const char *job; /* RESUME/RECOVER: exact /KUI/salvage/job-NNNN folder. */
    void (*progress)(void *ctx,const struct kui_salvage_status *status);
};
/* NEW collects readable sectors and, only with zero_fill consent, records
 * unresolved placeholders durably. RECOVER is a separate explicit action.
 * RESUME continues a first pass; it does not start optical repair sweeps.
 * Fatal/reset read results always stop, never become zeros. Data candidates
 * require Mode 1 FAD/EDC/PQ; audio repair requires two byte-identical reads.
 * No catalogue match or independent saved-file verification is implied.
 * Accepted normal/DMA capture code is not modified or used as a salvage loop. */
enum kui_salvage_result kui_salvage_run(const struct kui_capture_plan *plan,
    const struct kui_capture_ops *ops,const struct kui_salvage_options *options,
    enum kui_salvage_action action,struct kui_salvage_status *status);
/* Read-only discovery, mounts/unmounts. Chooses the greatest numbered valid
 * header matching the prepared TOC and both optical identity anchors. Returns
 * false on no match, cancellation or I/O/drive error. Does not repair/truncate
 * journals or imply that a selected job is complete. */
bool kui_salvage_latest(const struct kui_capture_plan *plan,
    const struct kui_capture_ops *ops,char job[KUI_DEST_JOB_CAP]);
#endif
