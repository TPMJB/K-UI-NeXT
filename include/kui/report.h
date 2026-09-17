/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_REPORT_H
#define KUI_REPORT_H
#include "kui/probe.h"
enum kui_report_result { KUI_REPORT_FAILED, KUI_REPORT_STOPPED, KUI_REPORT_SAVED };
/* Sole I/O worker, connected media, no other open files. Uses a fresh probe
 * directory and publishes diagnostics.txt only after a full write/sync/close.
 * Cancellation/failure may leave diagnostics.tmp; existing reports and capture
 * files are never opened. The result describes the log save, not the capture. */
enum kui_report_result kui_report_save(const void *data,size_t bytes,char path[96],
    kui_log_fn log,kui_cancel_fn cancelled);
#endif
