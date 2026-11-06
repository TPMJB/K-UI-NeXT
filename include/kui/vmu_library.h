/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_LIBRARY_H
#define KUI_VMU_LIBRARY_H
#include "kui/apps.h"
#include "kui/vmu_game_candidates.h"

/* Shell-only, mounted FatFs and exclusive storage-worker ownership. Canonical
 * active pNNNN cards and immutable vNNNN snapshots share one listing. Browse
 * and name operations never write card.vmd, card.journal or game assignments.
 * A committed active journal is validated against its P2 descriptor and
 * overlaid into the caller's RAM image; it is never replayed to storage. */
enum kui_vmu_library_result {
    KUI_VMU_LIBRARY_OK=0, KUI_VMU_LIBRARY_ARGUMENT=-1,
    KUI_VMU_LIBRARY_IO=-2, KUI_VMU_LIBRARY_INVALID=-3,
    KUI_VMU_LIBRARY_CANCELLED=-4,
    KUI_VMU_LIBRARY_UNCERTAIN=-5
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
/* Worker-owned confirmation token. Preview is read only and pins the entire
 * effective image, exact card identity/path and, for a save, its raw directory
 * entry. NULL save_name selects the entire card. Commit rechecks all pins.
 * users counts all games sharing the selected active card. */
struct kui_vmu_delete_preview {
    char path[KUI_VMU_BACKUP_PATH_CAP],save_name[16];
    uint8_t identity[16],image_sha256[32],entry[32],references_sha256[32];
    unsigned directory_index,users,files,blocks;
    bool whole_card,snapshot,ready;
};
int kui_vmu_library_delete_preview(const char *path,const uint8_t expected_identity[16],
    const char *save_name,struct kui_vmu_delete_preview *out,kui_log_fn log,kui_cancel_fn cancel);
/* Exclusive mounted-volume ownership throughout. Active-card deletion needs
 * a fresh physical storage pin; snapshot deletion does not. Save deletion
 * stages a verified replacement, clears the journal after overlaying committed
 * data and lets normal launch rebuild its profile. Snapshot images are
 * immutable: only their entire explicitly selected card may be deleted.
 * Pre-commit cancellation removes only this call's verified, identity-pinned
 * stage; preexisting or failed-I/O stages are preserved for inspection.
 * Stop is deferred once reference or namespace commit begins. UNCERTAIN means
 * a storage failure may have changed references or left quarantined files;
 * never automatically retry or roll back. */
int kui_vmu_library_delete_commit(const struct kui_vmu_delete_preview *preview,
    const struct kui_vmu_game_candidates_storage *storage,kui_log_fn log,kui_cancel_fn cancel);
int kui_vmu_library_describe(const char *path,const uint8_t expected_identity[16],
    struct kui_vmu_backup_entry *out,kui_log_fn log,kui_cancel_fn cancel);
#endif
