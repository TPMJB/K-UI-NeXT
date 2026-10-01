/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_BOOT_IMAGE_H
#define KUI_BOOT_IMAGE_H
#include "kui/media.h"
#include "kui/runtime.h"

/* Pure filename policy, in FatFs drive-qualified form. Normal boot tries the
 * runtime then recovery; explicit recovery never substitutes the runtime.
 * Returns NULL after the selected policy's last attempt. */
const char *kui_boot_image_path(bool recovery_only, unsigned attempt);

/* Single bootstrap worker; raw stays connected throughout. Selects a bounded
 * read-only boot volume, validates an image and patches its RAM-only transport
 * marker. A FAT boot candidate takes precedence over an ext4 data candidate,
 * even when no usable image exists on FAT. Never executes or repairs anything.
 * Success owns out->data; every failure leaves out empty. Filesystem mounts
 * and the temporary FatFs media view are released before returning. */
enum kui_runtime_result kui_boot_image_read(const struct kui_media_ops *raw,
    unsigned transport, bool recovery_only, struct kui_runtime_image *out,
    kui_log_fn log, kui_cancel_fn cancelled);
#endif
