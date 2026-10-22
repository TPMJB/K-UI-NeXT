/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/ce_load_plan.h"
#include <string.h>

#define RAM_PHYSICAL 0x0c000000u
#define RAM_CACHED 0x8c000000u
#define RAM_UNCACHED 0xac000000u
#define RAM_BYTES 0x01000000u
#define SECTOR_BYTES 2048u

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/* Do not mask arbitrary addresses into RAM: CE thread virtual addresses are
 * not known physical aliases. Check the complete range before canonicalizing. */
static bool cached_range(uint32_t address, uint32_t bytes, uint32_t *cached) {
    uint32_t base;
    if(address >= RAM_PHYSICAL && address < RAM_PHYSICAL + RAM_BYTES)
        base = RAM_PHYSICAL;
    else if(address >= RAM_CACHED && address < RAM_CACHED + RAM_BYTES)
        base = RAM_CACHED;
    else if(address >= RAM_UNCACHED && address < RAM_UNCACHED + RAM_BYTES)
        base = RAM_UNCACHED;
    else return false;
    uint32_t offset = address - base;
    if(!bytes || bytes > RAM_BYTES - offset) return false;
    *cached = RAM_CACHED + offset;
    return true;
}
static bool overlap(uint32_t a, uint32_t an, uint32_t b, uint32_t bn) {
    /* All inputs are already bounded within the same 16 MiB RAM alias. */
    return a < b + bn && b < a + an;
}
enum kui_ce_load_result kui_ce_load_plan_build(bool windows_ce,
    const uint8_t *prefix, size_t prefix_bytes, uint64_t file_bytes,
    const struct kui_ce_live_range *live, size_t live_count,
    struct kui_ce_load_plan *out) {
    if(!out) return KUI_CE_LOAD_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if(!prefix || !live || !live_count ||
       live_count > KUI_CE_LOAD_MAX_LIVE_RANGES) return KUI_CE_LOAD_ARGUMENT;
    if(!windows_ce) return KUI_CE_LOAD_PROFILE;
    if(prefix_bytes < KUI_CE_LOAD_PREFIX_BYTES) return KUI_CE_LOAD_HEADER;
    if(get32(prefix + 0x10) != 1u ||
       get32(prefix + 0x18) != KUI_CE_LOAD_PREFIX_BYTES)
        return KUI_CE_LOAD_UNSUPPORTED;
    uint32_t bytes = get32(prefix + 0x1c);
    if(bytes < 2u || file_bytes != (uint64_t)KUI_CE_LOAD_PREFIX_BYTES + bytes)
        return KUI_CE_LOAD_FILE_RANGE;
    /* Wide rounding rejects overflow before either the file size or the RAM
     * span is narrowed. Exact body bytes and allocated span stay distinct. */
    uint64_t rounded = ((uint64_t)bytes + SECTOR_BYTES - 1u) /
                       SECTOR_BYTES * SECTOR_BYTES;
    struct kui_ce_load_plan p = {0};
    if(rounded > UINT32_MAX ||
       !cached_range(get32(prefix + 0x14), (uint32_t)rounded, &p.body.address) ||
       p.body.address != KUI_CE_LOAD_BODY_ADDRESS)
        return KUI_CE_LOAD_ADDRESS;
    if(!cached_range(get32(prefix + 0x20), 2u, &p.entry_address) ||
       (p.entry_address & 1u) || p.entry_address < p.body.address ||
       p.entry_address - p.body.address > bytes - 2u)
        return KUI_CE_LOAD_ENTRY;
    p.prefix = (struct kui_ce_load_region){
        0u, KUI_CE_LOAD_PREFIX_BYTES, 1u,
        KUI_CE_LOAD_PREFIX_ADDRESS, KUI_CE_LOAD_PREFIX_BYTES};
    p.body.file_offset = KUI_CE_LOAD_PREFIX_BYTES;
    p.body.bytes = bytes;
    p.body.rounded_bytes = (uint32_t)rounded;
    p.body.sector_count = (uint32_t)rounded / SECTOR_BYTES;
    if(overlap(p.prefix.address, p.prefix.rounded_bytes,
               p.body.address, p.body.rounded_bytes)) return KUI_CE_LOAD_OVERLAP;
    for(size_t i = 0; i < live_count; ++i) {
        uint32_t address;
        if(!cached_range(live[i].address, live[i].bytes, &address))
            return KUI_CE_LOAD_LAYOUT;
        if(overlap(p.prefix.address, p.prefix.rounded_bytes, address, live[i].bytes) ||
           overlap(p.body.address, p.body.rounded_bytes, address, live[i].bytes))
            return KUI_CE_LOAD_OVERLAP;
    }
    *out = p;
    return KUI_CE_LOAD_OK;
}
