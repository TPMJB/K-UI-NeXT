/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_SDK_COHERENCE_H
#define KUI_VMU_NATIVE_SDK_COHERENCE_H
#include "kui/vmu_native_passive.h"

#define KUI_VMU_NATIVE_SDK_COHERENCE_READY UINT32_C(0x4e434f31)
/* Private P2 state: lockcell P1, manager cell/raw initial, initializer resume;
 * saved R2/R3/SR, ready; flag P2, layout kind, initializer, success count.
 * Configuration is prepared only after ordinary launch admission. The gate
 * owns the mutable scratch while IMASK15 excludes other SDK callers. */
extern volatile uint32_t kui_vmu_native_sdk_coherence_state[12];
int kui_vmu_native_sdk_coherence_prepare(const struct kui_vmu_native_passive_config *);
void kui_vmu_native_sdk_coherence_uniform_gate(void);
void kui_vmu_native_sdk_coherence_variable_gate(void);
#endif
