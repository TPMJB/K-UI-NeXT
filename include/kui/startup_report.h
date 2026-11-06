/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_STARTUP_REPORT_H
#define KUI_STARTUP_REPORT_H
#include <stdbool.h>
#include <stddef.h>
#define KUI_STARTUP_REPORT_PATH "0:/KUI/startup.txt"
#define KUI_STARTUP_REPORT_PREVIOUS_PATH "0:/KUI/startup-previous.txt"
#define KUI_STARTUP_REPORT_MAX_BYTES 65536u
#define KUI_STARTUP_REPORT_RECORD_MAX 16384u
/* The startup caller owns one mounted volume and has not started app workers.
 * Append a bounded record and sync/close it; rotate only these two report
 * files at the size limit. No mount, transport change or update state write
 * occurs here. A failed report must never prevent startup. */
bool kui_startup_report_append_mounted(const void *data,size_t bytes);
#endif
