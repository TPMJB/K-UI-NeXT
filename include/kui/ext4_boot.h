/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_EXT4_BOOT_H
#define KUI_EXT4_BOOT_H
#include "kui/runtime.h"
#include "kui/media.h"
enum kui_ext4_boot_result {
    KUI_EXT4_BOOT_OK, KUI_EXT4_BOOT_NOT_FOUND, KUI_EXT4_BOOT_INVALID,
    KUI_EXT4_BOOT_IO, KUI_EXT4_BOOT_RUNTIME, KUI_EXT4_BOOT_CANCELLED,
    KUI_EXT4_BOOT_MEMORY
};
/* Single-threaded CD bootstrap only. raw must remain connected for the call.
 * Reads /KUI/runtime.kui from one unambiguous clean Linux volume. Never invokes
 * raw.write/raw.sync, replays a journal or executes the loaded image.
 * Success owns out->data; every failure leaves out empty. */
enum kui_ext4_boot_result kui_ext4_boot_read(const struct kui_media_ops *raw,
    struct kui_runtime_image *out, kui_log_fn log, kui_cancel_fn cancelled);
/* Same rules as above, for an absolute filesystem path such as
 * /KUI/recovery.kui (without the FatFs drive prefix). */
enum kui_ext4_boot_result kui_ext4_boot_read_path(const struct kui_media_ops *raw,
    const char *path, struct kui_runtime_image *out, kui_log_fn log,
    kui_cancel_fn cancelled);
#endif
