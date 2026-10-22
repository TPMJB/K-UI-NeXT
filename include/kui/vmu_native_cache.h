/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_CACHE_H
#define KUI_VMU_NATIVE_CACHE_H
#include "kui/vmu_native_discovery.h"
#define KUI_VMU_NATIVE_CACHE_BYTES 128u
/* Canonical little-endian static discovery metadata only. No live manager,
 * allocation or capability state is serialized. Invalid inputs leave output
 * untouched; these codecs support overlapping host buffers via local copies. */
bool kui_vmu_native_cache_encode(const struct kui_vmu_native_discovery *,
    void *wire,size_t bytes);
bool kui_vmu_native_cache_decode(struct kui_vmu_native_discovery *,
    const void *wire,size_t bytes);
/* Decode and bind to the exact immutable executable. A cache hit still
 * repeats all discovery/uniqueness checks and grants no patch authority. */
bool kui_vmu_native_cache_load(struct kui_vmu_native_discovery *,
    const void *wire,size_t bytes,const void *image,size_t image_bytes,uint32_t load);
#endif
