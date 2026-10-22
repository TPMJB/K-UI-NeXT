/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CE_LOAD_PLAN_H
#define KUI_CE_LOAD_PLAN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_CE_LOAD_PREFIX_BYTES 2048u
#define KUI_CE_LOAD_PREFIX_ADDRESS 0x8ce01000u
#define KUI_CE_LOAD_BODY_ADDRESS 0x8c010000u
#define KUI_CE_LOAD_MAX_LIVE_RANGES 16u

/* Read-only development planner for the one-section raw-GD profile described
 * in the owner's CE kernel analysis. This is not a general CE image parser,
 * does not identify the driver, and does not establish a safe boot contract.
 * No kernel bytes, patches or executable entry are included in this module. */
struct kui_ce_live_range {
    uint32_t address, bytes;
};
struct kui_ce_load_region {
    uint32_t file_offset, bytes, sector_count;
    /* Cached RAM alias; rounded_bytes includes the last sector's padding. */
    uint32_t address, rounded_bytes;
};
struct kui_ce_load_plan {
    struct kui_ce_load_region prefix, body;
    uint32_t entry_address;
};
enum kui_ce_load_result {
    KUI_CE_LOAD_OK,
    KUI_CE_LOAD_ARGUMENT,
    KUI_CE_LOAD_PROFILE,
    KUI_CE_LOAD_HEADER,
    KUI_CE_LOAD_UNSUPPORTED,
    KUI_CE_LOAD_FILE_RANGE,
    KUI_CE_LOAD_ADDRESS,
    KUI_CE_LOAD_ENTRY,
    KUI_CE_LOAD_LAYOUT,
    KUI_CE_LOAD_OVERLAP
};

/* windows_ce must come from validated IP metadata, not a filename. The caller
 * supplies the entire 2048-byte prefix and the boot file's actual byte length.
 * The reported layout is count at 0x10, then one {address,offset,bytes} tuple,
 * then entry at 0x20. Only offset 0x800 and body at physical 0x0c010000
 * (or its P1/P2 alias) are accepted; the body must consume the rest of the file.
 * Opaque prefix fields are not an identity or integrity check.
 *
 * live must enumerate EVERY still-live stage/code/BSS/stack/source buffer.
 * It must contain 1..MAX_LIVE_RANGES nonempty ranges. Physical RAM and its
 * P1/P2 aliases are compared as the same storage; virtual/MMU mappings and
 * hardware mirrors are refused. Checking the supplied ranges cannot prove
 * that the caller's list is complete or that CE later preserves any range.
 * The future reader must verify sectors and copy only each region's bytes;
 * rounded_bytes reserves padding but never authorizes reading beyond EOF.
 * Failure clears out. This function performs no I/O or writes to Dreamcast RAM.
 */
enum kui_ce_load_result kui_ce_load_plan_build(bool windows_ce,
    const uint8_t *prefix, size_t prefix_bytes, uint64_t file_bytes,
    const struct kui_ce_live_range *live, size_t live_count,
    struct kui_ce_load_plan *out);
#endif
