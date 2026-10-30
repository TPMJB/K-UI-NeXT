/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SALVAGE_COMPLETION_H
#define KUI_SALVAGE_COMPLETION_H
#include "kui/salvage.h"

enum kui_salvage_check_result { KUI_SALVAGE_CHECK_FAILED,
    KUI_SALVAGE_CHECK_STOPPED, KUI_SALVAGE_CHECK_COMPLETE };
struct kui_salvage_check_status {
    enum kui_salvage_check_result result;
    bool complete, saved_verified, catalogue_checked, verifying_files, readback_requested, report_written;
    unsigned track, tracks;
    uint64_t done, total;
    struct kui_known_summary catalogue;
    char message[128];
    char report[KUI_DEST_PATH_CAP];
};
struct kui_salvage_check_ops {
    void *ctx;
    bool (*cancelled)(void *);
    void (*progress)(void *,const struct kui_salvage_check_status *);
    kui_log_fn log;
};
/* Run immediately after kui_salvage_run returned RESOLVED with the same plan
 * and its freshly validated, durably complete receipt. That engine has checked
 * the journal, immutable original-target evidence and interrupted repairs;
 * this function does not accept an arbitrary GDI/manifest as that receipt.
 *
 * Mounts/unmounts storage, optionally reads every saved track byte against the
 * receipt's reconstructed CRCs, then compares those track CRCs with Redump and
 * TOSEC. Publishes a unique completion-check report by synced/read-back .part
 * then .txt rename, retaining incomplete evidence on failure. It does not issue
 * optical reads or modify the original salvage files. A catalogue
 * match without verify_files describes recovery CRCs, not saved-byte readback.
 * Missing/mismatching catalogues remain distinct from storage verification.
 */
enum kui_salvage_check_result kui_salvage_check_complete(
    const struct kui_capture_plan *plan,const struct kui_salvage_status *receipt,
    bool verify_files,const struct kui_salvage_check_ops *ops,
    struct kui_salvage_check_status *out);
#endif
