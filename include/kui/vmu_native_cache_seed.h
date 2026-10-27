/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_CACHE_SEED_H
#define KUI_VMU_NATIVE_CACHE_SEED_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define KUI_VMU_NATIVE_CACHE_SEED_BYTES 1120u
#define KUI_VMU_NATIVE_CACHE_SEED_WIRE_OFFSET 64u
#define KUI_VMU_NATIVE_CACHE_SEED_DIGEST_OFFSET 24u
/* A typed optional container in the immutable launch module. The launcher
 * pins a previously generated ordinary-config cache here before shutdown.
 * No live state, ownership grant or SD writer is carried in this container. */
void kui_vmu_native_cache_seed_template(uint8_t out[KUI_VMU_NATIVE_CACHE_SEED_BYTES]);
size_t kui_vmu_native_cache_seed_find(const void *module,size_t bytes);
/* Existing slot must be a canonical empty/filled container; output is
 * unchanged on failure. wire may overlap slot: fill snapshots before write. */
bool kui_vmu_native_cache_seed_fill(void *slot,size_t bytes,const void *wire,size_t wire_bytes);
/* Returns pointers into the still-owned slot, only for a complete filled
 * container. Empty/malformed slots leave outputs unchanged. */
bool kui_vmu_native_cache_seed_read(const void *slot,size_t bytes,
    const uint8_t **wire,const uint8_t **digest);
#endif
