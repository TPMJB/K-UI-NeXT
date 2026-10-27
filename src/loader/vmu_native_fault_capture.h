/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_FAULT_CAPTURE_H
#define KUI_VMU_NATIVE_FAULT_CAPTURE_H
#include <stdbool.h>
#include <stdint.h>
#include "kui/vmu_native_discovery.h"

/* Optional exact-revision diagnosis, never an admission requirement. Call once
 * under IMASK15 after all ordinary worker admission has succeeded. No pointers
 * into the retired installer are retained. 0: disabled/different immutable
 * image; 1: installed; -1: unsupported or changed live owner, untouched. */
int kui_vmu_native_fault_capture_install(bool enabled,
    const struct kui_vmu_native_discovery *driver);

/* Assembly-owned scratch. It is private P2 state, not an owner-stack frame.
 * The saved exception control registers remain untouched on the chain path. */
extern volatile uint32_t kui_vmu_native_fault_capture_state[8];
void kui_vmu_native_fault_capture_gateway(void);
#endif
