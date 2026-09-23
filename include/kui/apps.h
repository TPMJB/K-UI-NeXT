/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_APPS_H
#define KUI_APPS_H
#include "kui/probe.h"
#include <stdbool.h>
#include <stdint.h>
#define KUI_APP_LINES 12u
#define KUI_APP_LINE_CAP 80u
#define KUI_VMU_ROWS 8u
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
    bool present;
};
void kui_memory_app_run(struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
void kui_network_app_run(struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel);
/* action 0=list, 1=backup selected row, 2=backup all saves. No VMU writes. */
void kui_vmu_app_run(unsigned action,unsigned slot,unsigned page,unsigned selected,
    struct kui_vmu_view *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
#endif
