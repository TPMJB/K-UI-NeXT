/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_STORAGE_POLICY_H
#define KUI_STORAGE_POLICY_H
#include "kui/storage.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* open checks both the device and supported filesystem. Once selected, a
 * failure never redirects a later write to another card. */
struct kui_storage_probe_ops {
    void *ctx;
    bool (*open)(void *ctx,unsigned transport);
};
bool kui_storage_discover(unsigned *selected,const struct kui_storage_probe_ops *ops);

/* Initialized data marker in a runtime image, patched only after the image's
 * original checksum has passed. No fixed RAM address or on-card file changes. */
#define KUI_STORAGE_BOOT_MAGIC1 UINT32_C(0x5349554b)
#define KUI_STORAGE_BOOT_MAGIC2 UINT32_C(0x544f4f42)
struct kui_storage_boot_marker {uint32_t magic1,magic2,version,transport,inverse;};
#define KUI_STORAGE_BOOT_INITIALIZER {KUI_STORAGE_BOOT_MAGIC1,KUI_STORAGE_BOOT_MAGIC2,1u,KUI_STORAGE_AUTO,~(uint32_t)KUI_STORAGE_AUTO}
bool kui_storage_patch_boot(void *payload,size_t bytes,unsigned transport);
unsigned kui_storage_boot_transport(const volatile struct kui_storage_boot_marker *marker);
#endif
