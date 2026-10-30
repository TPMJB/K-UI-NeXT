/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_VMU_STANDARD_H
#define KUI_NATIVE_VMU_STANDARD_H
#include "kui/runtime.h"
#include <stdbool.h>
/* Existing delivered paced SCI72 Standard modules only. Recognizes the whole
 * loaded immutable payload, allowing its one valid4096-byte prepared profile
 * and six exact build-label fields to vary. No file/device access or mutation. */
bool kui_native_vmu_standard_recognize(const struct kui_runtime_image *package,size_t *profile_slot);
#endif
