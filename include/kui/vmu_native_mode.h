/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_MODE_H
#define KUI_VMU_NATIVE_MODE_H
#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_NATIVE_MODE_BYTES 32u
enum kui_vmu_native_mode {
    KUI_VMU_NATIVE_MODE_INVALID=-1,
    KUI_VMU_NATIVE_MODE_NORMAL=0,
    KUI_VMU_NATIVE_MODE_CONTROL=1,
    KUI_VMU_NATIVE_MODE_PROFILE=2,
    KUI_VMU_NATIVE_MODE_LEASE_ONLY=3
};
/* Canonical immutable launch mode. No pointer, storage authority or live
 * lifetime is serialized here. Invalid versions, lengths and reserved bytes
 * are refused rather than treated as a normal/control fallback. */
enum kui_vmu_native_mode kui_vmu_native_mode_decode(const void *,size_t);
#endif
