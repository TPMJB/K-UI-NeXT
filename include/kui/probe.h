/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_PROBE_H
#define KUI_PROBE_H
#include "kui/core.h"
#include "ff.h"

typedef void (*kui_log_fn)(const char *format, ...);
typedef bool (*kui_cancel_fn)(void);
bool kui_mount(FATFS *fs, kui_log_fn log);
bool kui_new_probe_dir(char path[64], kui_log_fn log);
bool kui_write_new_file(const char *path, const void *data, size_t size,
                         kui_log_fn log);
bool kui_storage_probe(kui_log_fn log, kui_cancel_fn cancelled);

#endif
