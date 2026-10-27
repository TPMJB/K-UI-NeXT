/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_SDK_SCI_RUNTIME_H
#define KUI_NATIVE_SDK_SCI_RUNTIME_H

#ifndef KUI_NATIVE_SDK_SCI_ROUTE
#define KUI_NATIVE_SDK_SCI_ROUTE 0
#endif
#if KUI_NATIVE_SDK_SCI_ROUTE != 0 && KUI_NATIVE_SDK_SCI_ROUTE != 1
#error "KUI_NATIVE_SDK_SCI_ROUTE must be exactly 0 or 1"
#endif
#ifndef KUI_NATIVE_SDK_SCI_STRUCTURAL
#define KUI_NATIVE_SDK_SCI_STRUCTURAL 0
#endif
#if KUI_NATIVE_SDK_SCI_STRUCTURAL != 0 && KUI_NATIVE_SDK_SCI_STRUCTURAL != 1
#error "KUI_NATIVE_SDK_SCI_STRUCTURAL must be exactly 0 or 1"
#endif
#if KUI_NATIVE_SDK_SCI_STRUCTURAL && !KUI_NATIVE_SDK_SCI_ROUTE
#error "Structural SDK SCI admission requires the SDK callback route"
#endif
#ifndef KUI_NATIVE_STANDALONE_READER
#define KUI_NATIVE_STANDALONE_READER 0
#endif
#if KUI_NATIVE_STANDALONE_READER != 0 && KUI_NATIVE_STANDALONE_READER != 1
#error "Standalone reader fit selector must be exactly zero or one"
#endif
#if KUI_NATIVE_STANDALONE_READER && \
    (!KUI_NATIVE_SDK_SCI_ROUTE || !KUI_NATIVE_SDK_SCI_STRUCTURAL || \
     !KUI_RETAIL_LOW_RESIDENT || !KUI_RETAIL_ASYNC || \
     KUI_VMU_NATIVE_BACKGROUND || KUI_NATIVE_READAHEAD || \
     KUI_VMU_NATIVE_PERSISTENT_PROBE || defined(KUI_RETAIL_CE))
#error "Standalone reader fit requires native low SDK SCI without VMU or read-ahead"
#endif
#if KUI_NATIVE_SDK_SCI_ROUTE
#if !KUI_NATIVE_LOADER_TIMELINE || !KUI_NATIVE_IRQ_REARM || \
    !KUI_RETAIL_NATIVE_HYBRID || (!KUI_VMU_NATIVE_BACKGROUND && !KUI_NATIVE_STANDALONE_READER) || \
    defined(KUI_RETAIL_CE) || KUI_NATIVE_SCI_PRIORITY7
#error "SDK SCI routing is restricted to the separate native timeline level1 experiment"
#endif
#endif

/* These plain integer constants are also assembly operands. The immutable
 * descriptor is referenced by the native resident entry word at offset36.
 * The stage publishes the cookie last, after both SDK callbacks are bound. */
#define KUI_NATIVE_SDK_SCI_DESCRIPTOR_MAGIC 0x4b535231
#define KUI_NATIVE_SDK_SCI_DESCRIPTOR_VERSION 1
#define KUI_NATIVE_SDK_SCI_DESCRIPTOR_BYTES 20
#define KUI_NATIVE_SDK_SCI_DESCRIPTOR_ENTRY_OFFSET 36
#define KUI_NATIVE_SDK_SCI_COOKIE 0x53434931
#define KUI_NATIVE_SDK_SCI_COOKIE_INDEX 12
#define KUI_NATIVE_SDK_SCI_DEFAULT_INDEX 13
#define KUI_NATIVE_SDK_SCI_VBR_INDEX 14
#define KUI_NATIVE_SDK_SCI_COOKIE_OFFSET 0x530
#define KUI_NATIVE_SDK_SCI_DEFAULT_OFFSET 0x534
#define KUI_NATIVE_SDK_SCI_VBR_OFFSET 0x538
#define KUI_NATIVE_SDK_SCI_OWNER_VBR 0x8c00f400
#define KUI_NATIVE_SDK_SCI_ERI_SLOT_OFFSET 0x25c
#define KUI_NATIVE_SDK_SCI_RXI_SLOT_OFFSET 0x260
#define KUI_NATIVE_SDK_SCI_VBR_FIRST 0x8c000000
#define KUI_NATIVE_SDK_SCI_VBR_LAST 0x8cfff900
#define KUI_NATIVE_SDK_SCI_VBR_SPAN 0x660

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdint.h>
/* The complete copied vector and SCI table fit in canonical P1 RAM. This
 * check precedes every dynamic-VBR table dereference, including lost-route
 * handling after a retained word or the game's VBR changes. */
static inline bool kui_native_sdk_sci_vbr_address(uint32_t vbr) {
    return !(vbr&255u) && vbr-UINT32_C(0x8c000000)<UINT32_C(0x00fffa00);
}
struct kui_native_sdk_sci_runtime_exports {
    uint32_t magic,version,bytes,callback,region_storage;
};
_Static_assert(sizeof(struct kui_native_sdk_sci_runtime_exports)==20,
    "SDK SCI runtime descriptor");
void kui_retail_sdk_sci_entry(void);
uint32_t kui_retail_sdk_sci_dispatch(uint32_t event);
#endif
#endif
