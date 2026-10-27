/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_COLLECTION_H
#define KUI_VMU_COLLECTION_H
#include "kui/probe.h"
#include "kui/vmu_image.h"

#define KUI_VMU_COLLECTION_ROOT "/KUI/vmu/cards"
#define KUI_VMU_COLLECTION_ROWS 8u
#define KUI_VMU_COLLECTION_PATH_CAP 192u
#define KUI_VMU_COLLECTION_READY_BYTES 512u
enum kui_vmu_collection_result {
    KUI_VMU_COLLECTION_OK=0, KUI_VMU_COLLECTION_ARGUMENT=-1,
    KUI_VMU_COLLECTION_IO=-2, KUI_VMU_COLLECTION_IMAGE=-3,
    KUI_VMU_COLLECTION_VERIFY=-4, KUI_VMU_COLLECTION_CANCELLED=-5,
    KUI_VMU_COLLECTION_FULL=-6
};
struct kui_vmu_collection_card {
    unsigned id;
    char label[16],path[KUI_VMU_COLLECTION_PATH_CAP];
    struct kui_vmu_image_summary summary;
    uint8_t sha256[32];
};
struct kui_vmu_collection_page {
    struct kui_vmu_collection_card entries[KUI_VMU_COLLECTION_ROWS];
    unsigned page,count,total;
};
/* Worker-only, mounted FatFs, exclusive volume ownership. Creates a NEW
 * v0001..v9999 directory. Existing cards and failed partial folders are never
 * overwritten/deleted/reused. Source must remain immutable and disjoint from
 * out for this call. Physical devices are never accessed. Complete general
 * VMU admission precedes allocation; all files are synced, closed, reopened
 * and verified before ready.bin is written last. Failed/cancelled work may
 * leave an unpublished folder; it must not be treated as an active card.
 * Stop is deferred once ready.bin publication starts, through final admission.
 * A late I/O error can leave a complete card: a new list revalidates it before
 * showing it. No automatic retry or rollback follows an uncertain write.
 *
 * This v1 catalog is an immutable management snapshot. It has a zero journal
 * and unique pair identity, but no live ownership/P2 or game binding. It must
 * undergo separate game activation before use by a resident game service.
 * label is optional display text, never a path. No CRC repair or save grouping
 * is performed. Counts describe the admitted image, not game payload validity.
 */
int kui_vmu_collection_publish(const void *image,size_t bytes,const char *label,
    struct kui_vmu_collection_card *out,kui_log_fn log,kui_cancel_fn cancel);
/* Lists complete, byte-verified v1 snapshots in numeric ID order. Missing
 * catalog is an empty successful page. Partial/corrupt/foreign entries are
 * omitted; storage failures/cancellation fail the entire view. Every image is
 * validated, including SHA/CRC and exact zero journal/identity/manifest match.
 * This API does not open active pNNNN pairs or recover/modify any game journal.
 */
int kui_vmu_collection_list(unsigned page,struct kui_vmu_collection_page *out,
    kui_log_fn log,kui_cancel_fn cancel);
/* Strict canonical 0:/KUI/vmu/cards/vNNNN/card.vmd parser. No case aliases,
 * traversal, arbitrary suffixes or other drives. Returns0 on invalid path. */
unsigned kui_vmu_collection_path_id(const char *path);
/* Admission of one exact catalog path, with the same checks as list. */
int kui_vmu_collection_verify(const char *path,struct kui_vmu_collection_card *out,
    kui_log_fn log,kui_cancel_fn cancel);
#endif
