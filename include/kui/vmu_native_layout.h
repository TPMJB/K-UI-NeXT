/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_LAYOUT_H
#define KUI_VMU_NATIVE_LAYOUT_H
#include "kui/vmu_native_runtime.h"
#define KUI_VMU_NATIVE_LAYOUT_VERSION 1u
#define KUI_VMU_NATIVE_LAYOUT_INPUTS 24u
/* Immutable provenance for the reviewed variable SDK initializer. Buffer
 * pointers themselves can originate in BSS; binding reads the exact admitted
 * input array, reproduces the initializer's align/P2/two-bank assignment and
 * validates every live manager entry. Neither operation writes game memory. */
struct kui_vmu_native_layout {
    uint32_t version,bytes,initializer,initializer_wrapper,initializer_call;
    uint32_t tx_base,rx_layout,rx_base,rx_slots[KUI_VMU_NATIVE_RX_SLOTS];
    uint32_t gun_flag,timer1_callback,timer2_callback;
    uint32_t inputs,input_array,index_array,indices[KUI_VMU_NATIVE_LAYOUT_INPUTS];
};
bool kui_vmu_native_layout_discover(const void *image,size_t bytes,
    const struct kui_vmu_native_discovery *,struct kui_vmu_native_layout *);
/* Layout must be freshly resolved from the immutable executable. An installed
 * hook/lifetime adapter remains independently necessary. Out is modified only
 * on success; the caller's prepopulated lease fields are preserved. */
bool kui_vmu_native_layout_bind(const struct kui_vmu_native_layout *,
    const struct kui_vmu_native_discovery *,kui_vmu_native_runtime_read,void *,
    struct kui_vmu_native_runtime_config *);
#endif
