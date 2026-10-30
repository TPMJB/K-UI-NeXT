/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_IMPORT_PLAN_H
#define KUI_VMU_IMPORT_PLAN_H
#include "kui/vmu_image_subset.h"

#define KUI_VMU_IMPORT_TITLE_BYTES 128u
#define KUI_VMU_IMPORT_PRODUCT_BYTES 32u

/* Text coincidences for review, never proof of ownership or a selection.
 * Only a complete ASCII field equal to the complete title/product (ignoring
 * ASCII case and surrounding spaces, with at least four characters) sets a
 * hint. No prefix, fuzzy, filename-family or related-file inference is made. */
enum kui_vmu_import_hint {
    KUI_VMU_IMPORT_HINT_TITLE_TEXT = 1u,
    KUI_VMU_IMPORT_HINT_PRODUCT_TEXT = 2u
};
struct kui_vmu_import_entry {
    uint16_t slot, blocks, header_offset;
    uint8_t type, hints;
    char name[13];
    char short_description[17];
    char long_description[33];
    char application[17];
};
struct kui_vmu_import_plan {
    struct kui_vmu_image_summary summary;
    unsigned entry_count;
    /* Occupied entries in ascending directory-slot order; array index is not
     * necessarily the physical directory slot. No entry is auto-selected. */
    struct kui_vmu_import_entry entries[KUI_VMU_IMAGE_DIRECTORY_ENTRIES];
};

/* Pure GUI-side inspection of one immutable, exact 128 KiB snapshot. The
 * complete filesystem is admitted before any package metadata is used. The
 * VMS header is found by following FAT links to the entry's header offset;
 * its first 64 bytes supply short16/long32/application16. These are untrusted
 * display text, not a package/CRC or game-identity check. Name12 and all text
 * are NUL-terminated, stop at an on-disk NUL, trim trailing ASCII spaces and
 * replace non-printable/non-ASCII bytes with '?'. Save payload is unchanged.
 *
 * title/product are optional NUL-terminated strings. At most TITLE_BYTES /
 * PRODUCT_BYTES bytes are inspected; an unterminated or non-ASCII key gives
 * no hints. work and plan must be mutually disjoint and disjoint from source
 * and supplied keys. Invalid arguments/overlap change no buffers. Admission
 * failure clears plan. Source and keys stay immutable throughout the call.
 */
int kui_vmu_import_plan_inspect(const void *source, size_t source_bytes,
    const char *title, const char *product,
    struct kui_vmu_image_workspace *work, struct kui_vmu_import_plan *plan);

/* Build one separate per-game snapshot from explicitly reviewed physical
 * directory slots. Nonempty selection delegates to kui_vmu_image_subset,
 * preserving every selected file block, directory entry and source root
 * byte exactly. Nothing infers or drops related files from metadata/hints.
 * Zero selected slots instead creates a canonical blank 200-block card after
 * fully admitting source, then independently validates the result. slots may
 * be NULL only in that case. No live pair, journal or assignment is changed.
 *
 * The overlap, failure and immutable-source rules of image_subset apply;
 * invalid arguments change no buffers, other pre-construction failures leave
 * destination unchanged and clear summary. Discard destination on VERIFY.
 */
int kui_vmu_import_plan_build(const void *source, size_t source_bytes,
    void *destination, size_t destination_bytes,
    const uint16_t *slots, size_t slot_count,
    struct kui_vmu_image_workspace *work,
    struct kui_vmu_image_summary *summary);
#endif
