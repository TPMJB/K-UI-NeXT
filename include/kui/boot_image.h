/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_BOOT_IMAGE_H
#define KUI_BOOT_IMAGE_H
#include "kui/media.h"
#include "kui/runtime.h"
#include "kui/storage.h"

enum kui_boot_mode { KUI_BOOT_MODE_NORMAL, KUI_BOOT_MODE_RECOVERY, KUI_BOOT_MODE_TOOLS,
    KUI_BOOT_MODE_AUTOBOOT };

/* Only unattended CD-origin startup consults boot.kui. A card-loaded menu
 * goes directly to runtime/recovery, even if that same override is present. */
enum kui_boot_mode kui_boot_autostart_mode(bool from_card);

/* Fixed filename policy; tools/recovery each try only their requested image.
 * CD autoboot tries boot, runtime, then recovery on the same selected source.
 * Returns NULL for an invalid mode or after the final attempt. */
const char *kui_boot_image_mode_path(enum kui_boot_mode mode, unsigned attempt);
/* Enumerate exactly the requested physical source, or SCIF/SCI/IDE for AUTO.
 * AUTO is returned as an end/invalid-filter sentinel, never as a device. */
unsigned kui_boot_transport_at(unsigned filter, unsigned attempt);

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
/* As above, with explicit normal/recovery/card-tools selection. Tools uses
 * the same v1 envelope and storage handoff as runtime images, not a new ABI. */
enum kui_runtime_result kui_boot_image_read_mode(const struct kui_media_ops *raw,
    unsigned transport, enum kui_boot_mode mode, struct kui_runtime_image *out,
    kui_log_fn log, kui_cancel_fn cancelled);
#endif
