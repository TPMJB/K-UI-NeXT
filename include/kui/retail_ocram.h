/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_OCRAM_H
#define KUI_RETAIL_OCRAM_H
#include <stdbool.h>
#include <stdint.h>
#ifndef KUI_NATIVE_OCRAM_BOOTSTRAP
#define KUI_NATIVE_OCRAM_BOOTSTRAP 0
#endif
#if KUI_NATIVE_OCRAM_BOOTSTRAP != 0 && KUI_NATIVE_OCRAM_BOOTSTRAP != 1
#error "Native OCRAM bootstrap selector must be zero or one"
#endif
#if KUI_NATIVE_OCRAM_BOOTSTRAP && defined(KUI_RETAIL_CE)
#error "Native OCRAM bootstrap control retains the native reader"
#endif
/* SH-4 operand-cache RAM is CPU data space, never a main-RAM DMA alias.
 * OIX=0 uses bit13 and low12; bit12 aliases a 4KiB half. Keep every CPU
 * span inside one half while translation is off and cache RAM is enabled. */
static inline bool kui_retail_ocram_address(uint32_t address) {
    return address>=UINT32_C(0x7c000000) && address<UINT32_C(0x80000000);
}
static inline bool kui_retail_ocram_span(uint32_t address,uint32_t bytes,
                                        uint32_t mmucr,uint32_t ccr) {
    return kui_retail_ocram_address(address) && !(address&3u) && bytes &&
        !(mmucr&1u) && (ccr&UINT32_C(0xa0))==UINT32_C(0x20) &&
        bytes<=UINT32_C(0x1000)-(address&UINT32_C(0xfff));
}
#endif
