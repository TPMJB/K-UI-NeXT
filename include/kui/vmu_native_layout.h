/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_LAYOUT_H
#define KUI_VMU_NATIVE_LAYOUT_H
#include "kui/vmu_native_runtime.h"
#define KUI_VMU_NATIVE_LAYOUT_VERSION 1u
#define KUI_VMU_NATIVE_LAYOUT_INFILE_VERSION 2u
#define KUI_VMU_NATIVE_LAYOUT_INPUTS 24u
/* Immutable provenance for the reviewed SDK initializers. Explicit mode's
 * raw pointer array and indices are both in the preloaded executable.
 * Uniform mode has inputs==1: input_array/index_array name the immutable
 * RX/TX literal slots, and input_values[0]/indices[0] hold their raw bases.
 * Binding checks their exact admitted values, reproduces the initializer's
 * align/P2/two-bank assignment and validates every live manager entry.
 * Version 1 permits writable rings only outside the executable. Version 2
 * additionally certifies that at least one exact SDK ring lies wholly in
 * the immutable source image and that every such ring was zero-filled.
 * Fresh discovery proves its initializer-derived bounds and object/ring
 * separation before initialization; live binding preserves these bounds
 * after the SDK has populated the rings. Cached version-2 metadata must
 * replay that fresh proof against the exact source image and detector epoch.
 * Neither operation writes game memory. */
struct kui_vmu_native_layout {
    uint32_t version,bytes,initializer,initializer_wrapper,initializer_call;
    uint32_t tx_base,rx_layout,rx_base,rx_slots[KUI_VMU_NATIVE_RX_SLOTS];
    uint32_t gun_flag,timer1_callback,timer2_callback;
    uint32_t inputs,input_array,index_array,indices[KUI_VMU_NATIVE_LAYOUT_INPUTS];
    uint32_t input_values[KUI_VMU_NATIVE_LAYOUT_INPUTS];
};
bool kui_vmu_native_layout_discover(const void *image,size_t bytes,
    const struct kui_vmu_native_discovery *,struct kui_vmu_native_layout *);
/* Resolve the driver and layout together from the same still-immutable image.
 * This performs driver discovery/hash/uniqueness exactly once. It cannot admit
 * an untrusted cached driver: both outputs are produced by this call. On a
 * normal failure both outputs are cleared; overlapping output/image storage
 * is rejected without writes. */
bool kui_vmu_native_discover_layout(const void *image,size_t bytes,uint32_t load,
    struct kui_vmu_native_discovery *,struct kui_vmu_native_layout *);
/* Layout must be freshly resolved from the immutable executable. An installed
 * hook/lifetime adapter remains independently necessary. Out is modified only
 * on success; the caller's prepopulated lease fields are preserved. */
bool kui_vmu_native_layout_bind(const struct kui_vmu_native_layout *,
    const struct kui_vmu_native_discovery *,kui_vmu_native_runtime_read,void *,
    struct kui_vmu_native_runtime_config *);
enum kui_vmu_native_layout_bind_code {
    KUI_VMU_LAYOUT_BIND_OK=0,KUI_VMU_LAYOUT_BIND_ARGUMENT=1,
    KUI_VMU_LAYOUT_BIND_SCHEMA=2,KUI_VMU_LAYOUT_BIND_SPAN=3,
    KUI_VMU_LAYOUT_BIND_ARRAY_LOCATION=4,KUI_VMU_LAYOUT_BIND_ARRAY_CONFLICT=5,
    KUI_VMU_LAYOUT_BIND_INDEX=6,KUI_VMU_LAYOUT_BIND_INPUT_READ=7,
    KUI_VMU_LAYOUT_BIND_INPUT_VALUE=8,KUI_VMU_LAYOUT_BIND_BUFFER_SPAN=9,
    KUI_VMU_LAYOUT_BIND_BUFFER_CONFLICT=10,KUI_VMU_LAYOUT_BIND_SLOT_PROVENANCE=11,
    KUI_VMU_LAYOUT_BIND_MANAGER_READ=12,KUI_VMU_LAYOUT_BIND_MANAGER_VALUE=13,
    KUI_VMU_LAYOUT_BIND_TX_READ=14,KUI_VMU_LAYOUT_BIND_TX_VALUE=15,
    KUI_VMU_LAYOUT_BIND_RX_READ=16,KUI_VMU_LAYOUT_BIND_RX_VALUE=17,
    KUI_VMU_LAYOUT_BIND_SLOT_READ=18,KUI_VMU_LAYOUT_BIND_SLOT_VALUE=19,
    KUI_VMU_LAYOUT_BIND_INPUT_CHANGED=20
};
/* First refusal only. For schema failures address names the copied metadata
 * field; for live failures it names the attempted game word; for conflicts
 * value/expected carry the protected span's begin/length. Success clears it. */
struct kui_vmu_native_layout_diagnostic {uint32_t code,address,value,expected;};
bool kui_vmu_native_layout_bind_checked(const struct kui_vmu_native_layout *,
    const struct kui_vmu_native_discovery *,kui_vmu_native_runtime_read,void *,
    struct kui_vmu_native_runtime_config *,struct kui_vmu_native_layout_diagnostic *);
#endif
