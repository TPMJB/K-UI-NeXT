/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_SCAN_BYTES_H
#define KUI_NATIVE_SCAN_BYTES_H
#include <stdint.h>
/* Immutable-image decoders only. Callers retain their existing range checks.
 * SH-4 cannot load unaligned words; preserve exact byte accesses there and
 * on big-endian hosts. may_alias permits arbitrary executable bytes. */
static inline __attribute__((__always_inline__)) uint16_t kui_native_scan_le16(const uint8_t *p) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    typedef uint16_t scan_half __attribute__((__may_alias__));
    if(!((uintptr_t)p&1u))return *(const scan_half *)(const void *)p;
#endif
    return (uint16_t)(p[0]|(uint16_t)p[1]<<8);
}
static inline __attribute__((__always_inline__)) uint32_t kui_native_scan_le32(const uint8_t *p) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    typedef uint32_t scan_word __attribute__((__may_alias__));
    if(!((uintptr_t)p&3u))return *(const scan_word *)(const void *)p;
#endif
    return (uint32_t)kui_native_scan_le16(p)|(uint32_t)kui_native_scan_le16(p+2u)<<16;
}
#endif
