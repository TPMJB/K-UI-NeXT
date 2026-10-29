/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_IMAGE_MERGE_H
#define KUI_VMU_IMAGE_MERGE_H
#include "kui/vmu_image.h"

enum kui_vmu_image_merge_result {
    KUI_VMU_MERGE_OK=0,
    KUI_VMU_MERGE_ARGUMENT=-1,
    KUI_VMU_MERGE_INVALID=-2,
    KUI_VMU_MERGE_NAME_CONFLICT=-3,
    KUI_VMU_MERGE_FULL=-4,
    KUI_VMU_MERGE_MINIGAME_CONFLICT=-5,
    KUI_VMU_MERGE_VERIFY=-6
};
struct kui_vmu_image_merge_ref {
    const uint8_t *image,*entry;
    uint8_t name[12];
};
struct kui_vmu_image_merge_workspace {
    struct kui_vmu_image_workspace validation;
    struct kui_vmu_image_merge_ref files[KUI_VMU_IMAGE_USER_BLOCKS];
};
struct kui_vmu_image_merge_report {
    struct kui_vmu_image_summary summary;
    unsigned added,duplicates;
    char conflict_name[13];
};
/* Pure, read-only inputs: canonical 128KiB cards admitted by image_validate.
 * Builds a new canonical image from all files in base, then incoming. The
 * three images, workspace and report must be disjoint. No allocation or I/O.
 * A collision key trims trailing NUL/space padding from the exact 12-byte
 * filename. Identical duplicates require every directory byte except the
 * relocated first-block pointer, and every logical payload byte, to match.
 * Different saves, capacity overflow or a second distinct minigame refuse
 * the whole merge. No name, checksum, save metadata or payload is repaired.
 * Empty names or duplicate names within an input are invalid.
 *
 * destination remains byte-exact on every failure except VERIFY (which
 * indicates an internal construction failure). report is reset on a valid
 * call and may supply the conflicting filename on failure. workspace is
 * scratch. Invalid arguments/aliasing change none of the output buffers.
 */
int kui_vmu_image_merge(const void *base,const void *incoming,void *destination,
    size_t bytes,struct kui_vmu_image_merge_workspace *,
    struct kui_vmu_image_merge_report *);
#endif
