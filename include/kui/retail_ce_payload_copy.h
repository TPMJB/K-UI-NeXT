/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_CE_PAYLOAD_COPY_H
#define KUI_RETAIL_CE_PAYLOAD_COPY_H
#include <stdint.h>
#include <string.h>

/* A CE CPU buffer keeps its original virtual address, permissions and cache
 * policy. U0/P3 pages can back either RAM or VRAM; a numeric virtual address
 * cannot select the bus width. Sega's VRAM interface forbids byte stores.
 * Preserve the other byte at split edges with aligned volatile halfwords.
 * An aligned halfword cannot straddle a CE page, including an odd last byte;
 * no translation probe, physical alias or neighboring-page access is needed.
 * The existing caller must admit the complete payload and own its address
 * space. This sink grants no new mapping or interrupt-context permission. */
static void __attribute__((noinline,noipa)) kui_retail_ce_vram_write(uint8_t *destination,
    const uint8_t *source,uint32_t count) {
    typedef uint16_t half __attribute__((__may_alias__));
    if(count && ((uintptr_t)destination&1u)) {
        volatile half *out=(volatile half *)((uintptr_t)destination-1u);
        *out=(uint16_t)((*out&255u)|((uint16_t)*source++<<8));
        ++destination;--count;
    }
    /* The steady path needs no destination read. Source bytes may be
     * unaligned, while every destination store remains a legal halfword. */
    while(count>=2u) {
        *(volatile half *)destination=(uint16_t)(source[0]|((uint16_t)source[1]<<8));
        destination+=2u;source+=2u;count-=2u;
    }
    if(count) {
        volatile half *out=(volatile half *)destination;
        *out=(uint16_t)((*out&0xff00u)|*source);
    }
    __asm__ volatile("":::"memory");
}

static inline void kui_retail_ce_payload_copy(uint8_t *destination,
    const uint8_t *source,uint32_t count) {
    uint32_t area=(uint32_t)(uintptr_t)destination&UINT32_C(0xff000000);
    /* map_guest has already checked and normalized explicit main-RAM aliases.
     * DMA retains that fast path; translated CPU buffers retain their VA. */
    if(area==UINT32_C(0x8c000000) || area==UINT32_C(0xac000000))
        memcpy(destination,source,count);
    else kui_retail_ce_vram_write(destination,source,count);
}
#endif
