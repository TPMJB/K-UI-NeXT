/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_BOOT_H
#define KUI_RETAIL_BOOT_H
#include "kui/retail_image.h"
#include <string.h>

/* MIL-CD scrambling permutes 32-byte slices in descending power-of-two
 * windows, up to 2 MiB. Reference: KallistiOS utils/scramble/scramble.c.
 * Descramble in place with a bounded 128 KiB permutation plus 8 KiB visited
 * bitmap, supplied by the high stage; no game-time resident memory is used.
 * Call only for an explicitly marked scrambled executable, never by extension. */
#define KUI_RETAIL_SCRAMBLE_SLICES 65536u
static inline void kui_retail_boot_descramble(uint8_t *data, uint32_t bytes,
    uint16_t index[KUI_RETAIL_SCRAMBLE_SLICES], uint8_t seen[KUI_RETAIL_SCRAMBLE_SLICES/8u]) {
    uint32_t seed=bytes & 65535u;
    for(uint32_t window=2u*1024u*1024u;window>=32u;window>>=1) {
        while(bytes>=window) {
            uint32_t slices=window/32u;
            for(uint32_t i=0;i<slices;i++) index[i]=(uint16_t)i;
            for(uint32_t i=slices;i--;) {
                seed=(seed*2109u+9273u)&32767u;
                uint32_t other=(((seed+49152u)&65535u)*i)>>16;
                uint16_t swap=index[i];index[i]=index[other];index[other]=swap;
            }
            memset(seen,0,(slices+7u)/8u);
            for(uint32_t i=0;i<slices;i++) {
                if(seen[i/8u] & (1u<<(i%8u))) continue;
                uint8_t saved[32];memcpy(saved,data+i*32u,32u);
                uint32_t at=i;
                do {
                    uint32_t next=index[slices-1u-at];
                    uint8_t replaced[32];memcpy(replaced,data+next*32u,32u);
                    memcpy(data+next*32u,saved,32u);memcpy(saved,replaced,32u);
                    seen[at/8u]|=(uint8_t)(1u<<(at%8u));at=next;
                } while(at!=i);
            }
            data+=window;bytes-=window;
        }
    }
}

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
