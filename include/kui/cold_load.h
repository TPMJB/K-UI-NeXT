/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_COLD_LOAD_H
#define KUI_COLD_LOAD_H
#include <stddef.h>
#include <stdint.h>

/* A temporary-loader call, after the owner has been retired and RAM published.
 * The three callback arguments retain their native integer/pointer ABI.
 * Only instruction caching may be enabled: mutable media state and payload
 * buffers remain uncached. Unknown CCR/MMU/entry profiles call through without
 * changing the caller's cache state. This reserves no game or worker RAM. */
uint32_t kui_cold_load_call3(uintptr_t entry,uintptr_t a,uintptr_t b,uintptr_t c);
uint32_t kui_cold_load_crc32(uint32_t previous,const void *data,size_t bytes);
#endif
