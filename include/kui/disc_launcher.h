/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_LAUNCHER_H
#define KUI_DISC_LAUNCHER_H
#include "kui/apps.h"
#include "kui/runtime.h"
#include "kui/disc_boot.h"
#include "kui/vmu_game_info.h"
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
/* Shared optical preparation for the optional disc VMU frontend. The callback
 * runs with no optical read in flight and no filesystem mounted by this layer.
 * It receives the actual checked physical product and complete staged immutable
 * executable, and fills one canonical P2 profile. A refusal may explain the
 * unsupported component in the bounded, initially empty refusal buffer. The
 * module capability must explicitly admit that profile; the
 * physical-only path never calls a callback or examines a profile. */
#define KUI_DISC_LAUNCHER_REFUSAL_BYTES 128u
typedef bool (*kui_disc_launcher_card_fn)(const char *product,const void *executable,
    uint32_t executable_bytes,uint8_t profile[4096],
    char refusal[KUI_DISC_LAUNCHER_REFUSAL_BYTES],void *context,
    kui_log_fn log,kui_cancel_fn cancel);
struct kui_disc_launcher_region {bool region_free;char console;};
bool kui_disc_launcher_prepare_module(const char *package,kui_disc_launcher_card_fn card,
    void *card_context,const struct kui_disc_launcher_region *region,
    struct kui_runtime_image *image,struct kui_app_status *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
bool kui_disc_launcher_identity(struct kui_disc_boot_plan *plan,struct kui_app_status *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
/* Read-only release identification for card management; caller owns exclusive
 * optical access with SD storage released. Native and Windows CE GD metadata
 * are admitted independently of executable size or launch compatibility.
 * There is no executable read/hash, bootstrap load or storage write.
 *
 * Two bounded metadata passes must agree. signature is SHA-256 over canonical
 * TOC fields for both sessions followed by each exact metadata LBA/payload in
 * parser order (IP header, descriptors and root directory). It is a stable
 * release witness, not a unique serial number for physically identical discs.
 * Reidentify and compare before linking/creating a release's card. Both output
 * buffers are cleared on every failure; status records the refusal or Stop. */
bool kui_disc_launcher_metadata(struct kui_game_metadata *metadata,uint8_t signature[32],
    struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,
    kui_app_progress_fn progress);
void kui_disc_launcher_vmu_info(struct kui_vmu_game_info *info,struct kui_app_status *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
bool kui_disc_launcher_prepare_vmu(const struct kui_vmu_game_selection *selection,
    struct kui_runtime_image *image,struct kui_app_status *out,kui_log_fn log,
    kui_cancel_fn cancel,kui_app_progress_fn progress);
#endif
