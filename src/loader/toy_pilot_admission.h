/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_ADMISSION_H
#define KUI_TOY_PILOT_ADMISSION_H
#include "kui/retail_image.h"
#include "kui/retail_loader_layout.h"
#include "kui/toy_pilot_boot.h"

#define TOY_BOOT_BYTES 748444u
#define TOY_BOOT_CRC UINT32_C(0xcdc493b3)

enum kui_toy_pilot_launch_refusal {
    KUI_TOY_PILOT_LAUNCH_READER = 1u,
    KUI_TOY_PILOT_LAUNCH_FORMAT = 2u
};
/* Check the decoded launch options before selecting a resident or touching
 * storage. This explains unsupported choices; it does not replace the exact
 * image, executable and driver identities required before installing hooks. */
static inline uint32_t kui_toy_pilot_launch_refusal(const struct kui_retail_manifest *m) {
    if(!m || m->track_count>KUI_RETAIL_MANIFEST_SLOTS)
        return KUI_TOY_PILOT_LAUNCH_FORMAT;
    uint32_t reason=m->reader!=KUI_RETAIL_READER_STANDARD?
        KUI_TOY_PILOT_LAUNCH_READER:0u;
    for(uint32_t i=0;i<m->track_count;i++)
        if(kui_retail_track_sector_bytes(&m->slots[i].track)!=2352u)
            reason|=KUI_TOY_PILOT_LAUNCH_FORMAT;
    return reason;
}

/* The exact title initializes CCR from this data word after handoff. Its
 * movie callbacks discard operand-cache tags. Keep both caches enabled,
 * but require P1 write-through so those callbacks cannot discard dirty
 * authored owner/stack state. No generic cache-mode override is admitted. */
static inline uint32_t kui_toy_pilot_cache_policy(uint32_t policy) {
    return policy==KUI_TOY_BOOT_CACHE_POLICY_NATIVE?KUI_TOY_BOOT_CACHE_POLICY_SAFE:0u;
}

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
