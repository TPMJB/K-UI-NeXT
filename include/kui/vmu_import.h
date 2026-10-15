/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_IMPORT_H
#define KUI_VMU_IMPORT_H
#include "kui/apps.h"
#include <stddef.h>
#define KUI_VMU_IMPORT_MAX_BYTES (255u * 512u)
enum kui_vmu_save_format { KUI_VMU_SAVE_VMS, KUI_VMU_SAVE_VMI, KUI_VMU_SAVE_DCI };
struct kui_vmu_save_metadata {
    uint8_t entry[32];
    char resource[9];
    uint32_t source_bytes;
};
struct kui_vmu_save_info {
    uint8_t entry[32];
    size_t payload_offset,payload_bytes,padded_bytes;
    bool timestamp_known,name_from_filename;
};
/* Independently authored parsers of documented file layouts. DCI parsing
 * reverses each four-byte group of its payload in place; its directory is not
 * swapped. VMI metadata is supplied by kui_vmu_save_parse_vmi. Bare VMS supports
 * CRC-valid data packages only, using an explicit safe SD filename stem. */
bool kui_vmu_save_parse_vmi(const uint8_t *bytes,size_t length,
    struct kui_vmu_save_metadata *out,char *error,size_t error_cap);
bool kui_vmu_save_parse(enum kui_vmu_save_format format,uint8_t *bytes,size_t length,
    const struct kui_vmu_save_metadata *metadata,const char *filename_stem,
    struct kui_vmu_save_info *out,char *error,size_t error_cap);
/* Preview reads every source twice and pins both path and complete contents.
 * Confirmation requires that preview and stages a new verified K-UI backup
 * bundle only. No physical VMU is accessed. Paths use the full Files limit. */
void kui_vmu_import_run(const char *path,bool commit,struct kui_vmu_view *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
#endif
