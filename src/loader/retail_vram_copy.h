/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_VRAM_COPY_H
#define KUI_RETAIL_VRAM_COPY_H
#include <stdint.h>
#include <stddef.h>

/* Sega Hardware Specification Outline section3.4 forbids byte VRAM
 * accesses. Cursor callbacks may split at arbitrary file/sector offsets:
 * pack RAM source bytes and use only aligned volatile halfword VRAM stores.
 * RMW preserves the neighboring byte, including odd callback boundaries.
 * The whole DMA piece
 * remains independently32-byte aligned and range checked before any I/O. */
static void __attribute__((noinline)) native_vram_write(uint8_t *destination,
    const uint8_t *source,uint32_t count) {
    typedef uint16_t half __attribute__((__may_alias__));
    while(count--) {
        volatile half *out=(volatile half *)((uintptr_t)destination&~(uintptr_t)1u);
        uint16_t value=*out;
        if((uintptr_t)destination&1u)
            value=(uint16_t)((value&255u)|((uint16_t)*source++<<8));
        else value=(uint16_t)((value&0xff00u)|*source++);
        ++destination;
        *out=value;
    }
    __asm__ volatile("":::"memory");
}
#endif
