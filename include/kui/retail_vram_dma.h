/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_VRAM_DMA_H
#define KUI_RETAIL_VRAM_DMA_H
#include <stdint.h>

/* Retail Dreamcast PVR memory windows, as documented by KOS pvr_regs.h:
 * 04000000 is the 64-bit view, 05000000 the 32-bit view, each 8 MiB.
 * Preserve that view and its offset. CPU MMU addresses, TA apertures,
 * register windows and mirrors outside those exact spans are not admitted.
 * This pure helper performs no cache operation and never reads the window. */
static uint32_t __attribute__((noinline,unused)) kui_retail_vram_dma_span(uint32_t address,
    uint32_t bytes) {
    uint32_t segment=address>>29;
    if(segment && (segment&~1u)!=4u)return 0u;
    /* Ignore only alias bits and the04/05 bank-view bit. Bit23 must be
     * clear: this excludes both upper8MiB holes and all PVR registers. */
    if((address&UINT32_C(0x1e800000))!=UINT32_C(0x04000000))return 0u;
    uint32_t offset=address&UINT32_C(0x007fffff);
    if(!bytes || bytes-1u>UINT32_C(0x007fffff)-offset)return 0u;
    return address|UINT32_C(0xa0000000);
}
static inline __attribute__((always_inline)) uint32_t kui_retail_vram_dma_map(uint32_t address,
    uint32_t bytes,uint32_t alignment) {
    if(!alignment || (alignment&(alignment-1u)) ||
       ((address|bytes)&(alignment-1u)))return 0u;
    return kui_retail_vram_dma_span(address,bytes);
}
#endif
