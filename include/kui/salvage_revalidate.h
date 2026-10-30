/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SALVAGE_REVALIDATE_H
#define KUI_SALVAGE_REVALIDATE_H
#include "kui/capture.h"

enum kui_salvage_revalidate_phase {
    KUI_SALVAGE_REVALIDATE_NONE,
    KUI_SALVAGE_REVALIDATE_HEADER,
    KUI_SALVAGE_REVALIDATE_JOURNAL,
    KUI_SALVAGE_REVALIDATE_CANDIDATES,
    KUI_SALVAGE_REVALIDATE_TRACKS,
    KUI_SALVAGE_REVALIDATE_FINAL
};
struct kui_salvage_revalidate_status {
    enum kui_salvage_revalidate_phase phase;
    bool complete, failed, stopped, guard_cleared;
    unsigned track, tracks;
    uint32_t targets, recovered, candidates;
    uint64_t bytes_done, bytes_total;
    char job[KUI_DEST_JOB_CAP], message[128];
};
/* Strict saved-data check of a collected packed v2/v3 salvage job. The caller
 * has already selected the matching disc/job through normal admission. No
 * optical callback is made. Header, journal, candidates and every track byte
 * are read with fresh handles and byte-compared; original baseline and target
 * semantics must also agree. Incomplete/torn/unpublished states fail closed.
 * Source files and the existing target cache are opened read-only. A unique,
 * disposable index may be created under /KUI/probes and removed by this call.
 * Proven repeat-read disagreement publishes a quarantine; uncertain marker
 * publication blocks further admission for this boot. Only complete
 * uncancelled validation permits clearing the matching guard; success does
 * not itself repair sectors or start optical recovery. */
bool kui_salvage_revalidate(const struct kui_capture_plan *plan,
    const struct kui_capture_ops *ops,const char *job,
    void (*progress)(void *ctx,const struct kui_salvage_revalidate_status *status),
    struct kui_salvage_revalidate_status *status);
#endif
