/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_APPS_H
#define KUI_APPS_H
#include "kui/probe.h"
#include <stdbool.h>
#include <stdint.h>
#define KUI_APP_LINES 12u
#define KUI_APP_LINE_CAP 80u
#define KUI_VMU_ROWS 8u
#define KUI_VMU_BACKUP_PATH_CAP 192u
struct kui_app_status {
    char message[128];
    char lines[KUI_APP_LINES][KUI_APP_LINE_CAP];
    unsigned line_count;
    uint64_t done,total;
    unsigned errors;
    bool complete,passed,stopped;
};
typedef void (*kui_app_progress_fn)(const struct kui_app_status *status);
struct kui_vmu_entry {char name[16];uint32_t bytes;};
struct kui_vmu_view {
    struct kui_app_status status;
    struct kui_vmu_entry entries[KUI_VMU_ROWS];
    unsigned slot,page,count,total,free_blocks;
    bool present,restore_ready;
};
struct kui_vmu_backup_entry {
    char name[16],folder[16],path[KUI_VMU_BACKUP_PATH_CAP];
    uint32_t bytes;
};
struct kui_vmu_backup_view {
    struct kui_app_status status;
    struct kui_vmu_backup_entry entries[KUI_VMU_ROWS];
    unsigned page,count,total;
};
void kui_memory_app_run(struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
void kui_network_app_run(struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel);
/* action 0=list, 1=backup selected row, 2=backup all saves. These are read-only
 * on the VMU. New backups include .crc records required for restore. */
void kui_vmu_app_run(unsigned action,unsigned slot,unsigned page,unsigned selected,
    struct kui_vmu_view *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
void kui_vmu_backups_run(unsigned page,struct kui_vmu_backup_view *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
/* Preview (commit=false) reads/checks the entire backup and destination. Commit
 * requires that exact preview, rechecks both, and adds a new file only. Existing
 * names are refused. Never formats, deletes, or overwrites. Once VMU metadata
 * commit starts, Stop is deferred through verification. */
void kui_vmu_restore_run(const char *path,unsigned slot,bool commit,
    struct kui_vmu_view *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
#endif
