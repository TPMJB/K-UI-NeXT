/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test double for KOS's data-cache maintenance. It records what disc.c asked for, in
 * order, so a test can check the sequence (P = purge, I = invalidate; the fake BIOS adds D
 * when a DMA starts and C when it completes) as well as the ranges. Production uses the
 * pinned KOS header. */
#ifndef KUI_TEST_CACHE_H
#define KUI_TEST_CACHE_H
#include <stddef.h>
#include <stdint.h>
extern char test_cache_log[64];
extern unsigned test_cache_n;
extern uintptr_t test_cache_start[16];
extern size_t test_cache_bytes[16];
static inline void test_cache_note(char kind, uintptr_t start, size_t bytes) {
    if(test_cache_n < 15) {
        test_cache_start[test_cache_n] = start; test_cache_bytes[test_cache_n] = bytes;
        test_cache_log[test_cache_n++] = kind; test_cache_log[test_cache_n] = 0;
    }
}
static inline void arch_dcache_inval_range(uintptr_t start, size_t count) { test_cache_note('I', start, count); }
static inline void arch_dcache_purge_range(uintptr_t start, size_t count) { test_cache_note('P', start, count); }
#endif
