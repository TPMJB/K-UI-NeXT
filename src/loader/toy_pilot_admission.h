/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_ADMISSION_H
#define KUI_TOY_PILOT_ADMISSION_H
#include "kui/retail_image.h"

#define TOY_BOOT_BYTES 748444u
#define TOY_BOOT_CRC UINT32_C(0xcdc493b3)

/* The ordinary raw-GDI launcher leaves the optional source CRC unset.
 * The loaded executable's exact identity is mandatory in either case;
 * the stage also checks its full SHA-256 before installing any hook. */
static inline int kui_toy_pilot_boot_identity(
    const struct kui_retail_manifest *m, uint32_t loaded_bytes,
    uint32_t loaded_crc) {
    return m && loaded_bytes == TOY_BOOT_BYTES && loaded_crc == TOY_BOOT_CRC &&
        m->boot_bytes == TOY_BOOT_BYTES &&
        (!(m->flags & KUI_RETAIL_IMAGE_BOOT_CRC) ||
         m->boot_crc32 == TOY_BOOT_CRC);
}
#endif
