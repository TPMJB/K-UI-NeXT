/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_PATCH_H
#define KUI_VMU_NATIVE_PATCH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Conservative immutable-image guard. Excludes encoded direct branches and
 * aligned main-RAM literals into a displaced instruction window's interior.
 * It does not establish arbitrary indirect control flow. */
bool kui_vmu_native_patch_no_interior(const void *,size_t,uint32_t load,
    uint32_t begin,uint32_t bytes);
#endif
