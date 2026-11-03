/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_PROBLEM_REPORT_H
#define KUI_PROBLEM_REPORT_H
#include "kui/apps.h"

#define KUI_PROBLEM_REPORT_ROOT "0:/KUI/reports/support"
#define KUI_PROBLEM_REPORT_PATH_CAP 128u
#define KUI_PROBLEM_REPORT_LOG_MAX (128u * 1024u)
struct kui_problem_report_options { bool include_latest_screenshot; };
struct kui_problem_report_view {
    struct kui_app_status status;
    char path[KUI_PROBLEM_REPORT_PATH_CAP];
    unsigned files,unavailable;
    uint64_t bytes;
};
/* Sole storage worker, mounted volume. log_data is a frozen session log.
 * Collects only fixed launch/scan reports, package headers and an optional
 * menu screenshot. Does not mount, traverse arbitrary folders, or alter sources.
 * A new directory becomes public only after every included file is reread
 * and checked; incomplete .part directories are never completed reports. */
bool kui_problem_report_export_mounted(const struct kui_problem_report_options *,
    const char *version,const char *build,const void *log_data,size_t log_bytes,
    struct kui_problem_report_view *,kui_log_fn,kui_cancel_fn);
#endif
