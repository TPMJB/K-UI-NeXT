/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MAINTENANCE_H
#define KUI_MAINTENANCE_H
#include "kui/apps.h"
#define KUI_MAINTENANCE_FLASH_BYTES 131072u
#define KUI_MAINTENANCE_BIOS_BYTES 2097152u
enum kui_maintenance_action {KUI_MAINTENANCE_INSPECT=0,KUI_MAINTENANCE_FLASH_BACKUP=1,KUI_MAINTENANCE_BIOS_BACKUP=2};
struct kui_maintenance_identity {int region,language,audio,autostart;bool settings_valid;uint32_t start[5],size[5];};
struct kui_maintenance_source {
    void *ctx;
    bool (*identity)(void *,struct kui_maintenance_identity *);
    /* Exact-count contract; return zero only when all requested bytes read. */
    int (*read)(void *,bool bios,uint32_t offset,uint8_t *data,size_t bytes);
};
bool kui_maintenance_layout_valid(const struct kui_maintenance_identity *identity);
/* Mounted-media engine. Never calls any console write/erase API. */
void kui_maintenance_execute(unsigned action,const struct kui_maintenance_source *source,
    struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
/* Single-I/O-worker adapter; owns SD connect/mount only for backup actions. */
void kui_maintenance_run(unsigned action,struct kui_app_status *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
/* Read-only comparison against an existing local SD file. Accepts a safe,
 * canonical card-root or 0:/ path up to the Files picker's full path limit,
 * and requires exactly 2 MiB (BIOS) or 128 KiB (flash).
 * Two streaming passes check every byte and recheck source/file stability.
 * A completed mismatch has complete=true, passed=false and errors=0; count
 * and first differing byte offset are shown in the message and detail lines.
 * Neither engine nor adapter creates reports, directories or any SD files. */
void kui_maintenance_compare_execute(const char *path,bool bios,
    const struct kui_maintenance_source *source,struct kui_app_status *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
void kui_maintenance_compare_run(const char *path,bool bios,
    struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,
    kui_app_progress_fn progress);
#endif
