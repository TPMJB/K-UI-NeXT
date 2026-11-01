/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_LAUNCHER_H
#define KUI_DISC_LAUNCHER_H
#include "kui/apps.h"
#include "kui/runtime.h"
/* Exclusive optical worker operations. Check reads native GD boot metadata and
 * exact hashes only. Prepare additionally reads our small trusted bootstrap
 * from SD, releases FatFs, then stages verified physical-disc bytes for main's
 * existing arch_exec handoff. No VMU lookup, settings change or card write.
 * The initial direct-boot hardware gate deliberately has no resident VMU. */
void kui_disc_launcher_check(struct kui_app_status *out,kui_log_fn log,
    kui_cancel_fn cancel,kui_app_progress_fn progress);
bool kui_disc_launcher_prepare(struct kui_runtime_image *image,
    struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,
    kui_app_progress_fn progress);
#endif
