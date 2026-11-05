/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_VMU_STANDARD_H
#define KUI_NATIVE_VMU_STANDARD_H
#include "kui/runtime.h"
#include <stdbool.h>
/* Current build-bound manifest plus existing delivered paced SCI72 modules.
 * The manifest is generated while building a coordinated software cohort;
 * launch never adds an installed module to its own allowlist. Recognizes whole
 * loaded immutable payload, allowing its one valid4096-byte preparation
 * profile; historical entries also allow their six exact
 * build-label fields to vary. No file/device access or mutation. */
bool kui_native_vmu_standard_recognize(const struct kui_runtime_image *package,size_t *profile_slot);
#endif
