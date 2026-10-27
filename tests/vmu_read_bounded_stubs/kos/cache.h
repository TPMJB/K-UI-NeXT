/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_BOUNDED_CACHE_H
#define KUI_TEST_BOUNDED_CACHE_H
#include <stddef.h>
#include <stdint.h>
void dcache_inval_range(uintptr_t start,size_t count);
#endif
