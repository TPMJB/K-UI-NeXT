/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_CACHE_H
#define KUI_TOY_PILOT_CACHE_H

#ifndef KUI_TOY_PILOT_LOADER_TRACE
#define KUI_TOY_PILOT_LOADER_TRACE 0
#endif
#if KUI_TOY_PILOT_LOADER_TRACE != 0 && KUI_TOY_PILOT_LOADER_TRACE != 1
#error Toy loader trace must be zero or one
#endif

#ifndef KUI_TOY_PILOT_PRIVATE_P2
#define KUI_TOY_PILOT_PRIVATE_P2 0
#endif
#ifndef KUI_TOY_PILOT_NATIVE_CACHE
#define KUI_TOY_PILOT_NATIVE_CACHE 0
#endif
#if (KUI_TOY_PILOT_PRIVATE_P2 != 0 && KUI_TOY_PILOT_PRIVATE_P2 != 1) || \
    (KUI_TOY_PILOT_NATIVE_CACHE != 0 && KUI_TOY_PILOT_NATIVE_CACHE != 1)
#error Toy cache profile values must be zero or one
#endif
#if KUI_TOY_PILOT_NATIVE_CACHE && !KUI_TOY_PILOT_PRIVATE_P2
#error Native Toy copy-back requires the isolated private-state profile
#endif

#define KUI_TOY_PILOT_DATA_ALIAS (KUI_TOY_PILOT_PRIVATE_P2 * 0x20000000)
#if KUI_TOY_PILOT_NATIVE_CACHE
#define KUI_TOY_PILOT_CACHE_POLICY_SELECTED 0x00000105
#else
#define KUI_TOY_PILOT_CACHE_POLICY_SELECTED 0x00000101
#endif

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdint.h>

/* Ownership remains expressed in the original P1 reservation. Only the
 * exact main-RAM P2 alias is canonicalized; arbitrary guest addresses are
 * never converted into an admitted pointer by stripping broad bit masks. */
static inline uint32_t kui_toy_pilot_cached_address(uint32_t address) {
    return address >= UINT32_C(0xac000000) && address < UINT32_C(0xad000000) ?
        address - UINT32_C(0x20000000) : address;
}
static inline bool kui_toy_pilot_state_address(uint32_t address,
    uint32_t begin, uint32_t end) {
    return begin >= UINT32_C(0x8c000000) && end <= UINT32_C(0x8d000000) &&
        begin < end && address >= begin + KUI_TOY_PILOT_DATA_ALIAS &&
        address < end + KUI_TOY_PILOT_DATA_ALIAS;
}

/* The caller already owns the short masked native metadata transaction.
 * Publish only its small SH-RAM write span before returning to native code.
 * Shared cache-line bytes are written back, not discarded. This primitive
 * does not acquire a G2/DMA lease or touch sound RAM. */
static inline void kui_toy_pilot_cache_publish(uint32_t address, uint32_t bytes) {
#if KUI_TOY_PILOT_PRIVATE_P2
#ifdef KUI_TOY_PILOT_CACHE_TEST
    extern void kui_toy_pilot_cache_test_publish(uint32_t, uint32_t);
    kui_toy_pilot_cache_test_publish(address, bytes);
#else
    uint32_t end = address + bytes;
    for(uint32_t line = address & ~UINT32_C(31); line < end; line += 32u)
        __asm__ __volatile__("ocbp @%0" : : "r"(line) : "memory");
#endif
#else
    (void)address; (void)bytes;
#endif
}
#endif
#endif
