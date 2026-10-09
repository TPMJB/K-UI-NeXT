/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_PREFLIGHT_STORAGE_H
#define KUI_CDDA_PREFLIGHT_STORAGE_H
#include "kui/game_image.h"

#define CDDA_PREFLIGHT_PATH_CAP 512u
#define CDDA_PREFLIGHT_EXTENT_MAX 160u

struct cdda_preflight_storage_scan {
    uint32_t directories, entries, descriptors, incomplete, candidates;
    bool configured;
};
struct cdda_preflight_storage_extents {
    uint32_t file_bytes, cluster_bytes, clusters, extents, map_words;
    uint32_t first_volume_lba;
    uint64_t rounded_bytes;
    /* SHA256 over explicit LE geometry and (clusters,start,LBA) tuples.
     * LBAs are relative to the unique volume selected by cdda_storage_init.
     * Every cluster required by the logical file is covered; unused chain
     * allocation beyond EOF is not inspected or claimed. */
    uint8_t sha256[32];
};
struct cdda_preflight_storage_extent_run {
    uint32_t first_cluster, clusters, first_volume_lba;
    uint32_t file_sector, sectors;
};

/* One private, serialized, read-only adapter. No remount, allocator, writer,
 * audio or callbacks into retired runtime state. Root scan depth <=8,
 * directories <=128 and entries <=4096, excluding /KUI/tests/cdda entirely.
 * Exact original Toy GDI metadata and all fifteen sibling backings are
 * required. Two complete candidates, a scan limit, or an I/O fault refuse.
 * Optional /KUI/tests/cdda/preflight.cfg supplies one absolute ASCII path;
 * it still receives the same descriptor/backing checks. */
int cdda_preflight_storage_discover(void);
int cdda_preflight_storage_descriptor(void *out,size_t capacity,size_t *bytes);
const char *cdda_preflight_storage_path(void);
const struct kui_game_file_ops *cdda_preflight_storage_files(void);
const struct cdda_preflight_storage_scan *cdda_preflight_storage_scan(void);
const char *cdda_preflight_storage_failure(void);
void cdda_preflight_storage_cancel(bool (*cancelled)(void *),void *context);
/* Bounded CLMT construction: ceil(file_bytes/cluster_bytes) sample seeks plus
 * two rewind seeks,
 * <=160 disjoint runs. The same cached FIL then uses the checked CLMT.
 * Overfragmentation/overlap/range errors are refused, never truncated. */
int cdda_preflight_storage_extents(const char *name,
    struct cdda_preflight_storage_extents *out);
/* Copies one checked run of the most recently mapped cached file. Any later
 * open of a different backing invalidates this view; copy needed runs before
 * moving to the next file. No I/O or allocation. */
int cdda_preflight_storage_extent_run(unsigned index,
    struct cdda_preflight_storage_extent_run *out);
void cdda_preflight_storage_close(void);
#endif
