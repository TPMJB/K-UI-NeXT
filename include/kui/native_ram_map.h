/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_RAM_MAP_H
#define KUI_NATIVE_RAM_MAP_H
#include "kui/retail_loader_layout.h"
#include <stdint.h>

/* Console RAM-map test for games without an SDK heap lease. Just before the
 * game starts, the standalone stage writes an address-derived word to every
 * 128 KiB page above the loaded executable that its own image, BSS and stack
 * do not occupy. At menu return the resident reports, per page, whether every
 * word still holds that value. Any write by the game, its DMA or the reader
 * clears the page's bit for the rest of the session. A set bit is evidence
 * that one session never wrote the page; it reserves nothing. */
#define KUI_NATIVE_RAM_MAP_BEGIN UINT32_C(0x8c000000)
#define KUI_NATIVE_RAM_MAP_PAGE_SHIFT 17
#define KUI_NATIVE_RAM_MAP_PAGE_BYTES (UINT32_C(1)<<KUI_NATIVE_RAM_MAP_PAGE_SHIFT)
/* One bit per page, 32 pages (4 MiB) per word: four words, one screen row. */
#define KUI_NATIVE_RAM_MAP_WORDS \
    ((KUI_RETAIL_RAM_END-KUI_NATIVE_RAM_MAP_BEGIN)>>(KUI_NATIVE_RAM_MAP_PAGE_SHIFT+5))
#define KUI_NATIVE_RAM_MAP_KEY UINT32_C(0x4b55494e)

static inline uint32_t kui_native_ram_map_word(uint32_t address) {
    return address^KUI_NATIVE_RAM_MAP_KEY;
}
#endif
