/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_ADMISSION_H
#define KUI_TOY_PILOT_ADMISSION_H
#include "kui/retail_image.h"
#include "kui/retail_loader_layout.h"
#include "kui/toy_pilot_boot.h"

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

/* Admit the source callback and both ends of its unchanged registration/call
 * contract before modifying any title word. The replacement is an actual
 * linked low-resident terminal entry, never an owner-supplied address. */
static inline uint32_t kui_toy_pilot_reset_target(uint32_t callback,
    uint32_t registration,uint32_t invocation,uint32_t terminal) {
    return callback==KUI_TOY_BOOT_RESET_CALLBACK_NATIVE &&
        registration==KUI_TOY_BOOT_RESET_REGISTER_WORD &&
        invocation==KUI_TOY_BOOT_RESET_INVOKE_WORD && !(terminal&1u) &&
        terminal>=KUI_RETAIL_LOW_RESIDENT_ADDRESS &&
        terminal<KUI_RETAIL_LOW_STANDARD_LIMIT ? terminal:0u;
}
#endif
