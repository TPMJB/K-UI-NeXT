/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_BOOT_H
#define KUI_RETAIL_BOOT_H
#include "kui/retail_image.h"
#include <string.h>

/* The high stage alone uses this helper. Raw source sectors retain their
 * strict address/header checks. Cooked sectors have no headers; preparation
 * supplies the full executable's CRC, checked separately after loading. */
static inline enum kui_retail_header kui_retail_boot_copy(
    const uint8_t *source, uint8_t *destination, uint32_t lba,
    uint32_t count, uint32_t source_bytes, uint32_t *failed_lba) {
    if(source_bytes == KUI_GAME_DATA_BYTES) {
        memcpy(destination, source, (size_t)count * KUI_GAME_DATA_BYTES);
        return KUI_RETAIL_HEADER_OK;
    }
    for(uint32_t i = 0; i < count; ++i) {
        const uint8_t *sector = source + (size_t)i * KUI_GAME_RAW_BYTES;
        enum kui_retail_header result = kui_retail_sector_header(sector, lba + i);
        if(result != KUI_RETAIL_HEADER_OK) {
            *failed_lba = lba + i;
            return result;
        }
        memcpy(destination + (size_t)i * KUI_GAME_DATA_BYTES,
               sector + 16u, KUI_GAME_DATA_BYTES);
    }
    return KUI_RETAIL_HEADER_OK;
}
#endif
