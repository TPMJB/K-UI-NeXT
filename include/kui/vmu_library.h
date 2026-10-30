/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_LIBRARY_H
#define KUI_VMU_LIBRARY_H
#include "kui/apps.h"

/* Shell-only, mounted FatFs and exclusive storage-worker ownership. Canonical
 * active pNNNN cards and immutable vNNNN snapshots share one listing. These
 * functions never write card.vmd, card.journal, identities or assignments.
 * A committed active journal is validated against its P2 descriptor and
 * overlaid into the caller's RAM image; it is never replayed to storage. */
enum kui_vmu_library_result {
    KUI_VMU_LIBRARY_OK=0, KUI_VMU_LIBRARY_ARGUMENT=-1,
    KUI_VMU_LIBRARY_IO=-2, KUI_VMU_LIBRARY_INVALID=-3,
    KUI_VMU_LIBRARY_CANCELLED=-4
};
bool kui_vmu_library_path(const char *path);
/* Admits only this page's cards. Optional game titles come from one bounded
 * scan of KUI/covers header metadata with exact product keys and source
 * stamps. Save descriptions come from the admitted in-RAM image. Neither
 * is a launch/compatibility check, and listing never changes stored names. */
int kui_vmu_library_list(unsigned page,struct kui_vmu_backup_view *out,
    kui_log_fn log,kui_cancel_fn cancel);
int kui_vmu_library_read(const char *path,uint8_t image[KUI_VMU_IMAGE_BYTES],
    kui_log_fn log,kui_cancel_fn cancel);
/* Display metadata only: checks the canonical path and current identity but
 * does not admit a game/save image. Empty output means no custom name. */
int kui_vmu_library_label(const char *path,const uint8_t expected_identity[16],
    char out[49],kui_cancel_fn cancel);
/* Before a GUI tool allocates backup/metadata files, check the allocation
 * chains and disjointness of VMU files, their directories and ancestors,
 * including the exFAT allocation bitmap. No storage writes. */
int kui_vmu_library_check_allocation(kui_cancel_fn cancel);
/* Name is printable ASCII, 1..48 characters. Only display metadata in two
 * alternating sequence/identity/CRC records is changed. Interrupted updates
 * retain the preceding valid name. Path is independently admitted again. */
int kui_vmu_library_rename(const char *path,const char *name,const uint8_t expected_identity[16],
    kui_log_fn log,kui_cancel_fn cancel);
#endif
