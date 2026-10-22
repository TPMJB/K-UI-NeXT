/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_HEAP_H
#define KUI_VMU_NATIVE_HEAP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_NATIVE_HEAP_VERSION 1u
#define KUI_VMU_NATIVE_HEAP_TAIL32 1u
#define KUI_VMU_NATIVE_HEAP_DISPATCH2 1u
#define KUI_VMU_NATIVE_HEAP_FREE_LIMIT 1024u
enum kui_vmu_native_heap_result {
    KUI_VMU_NATIVE_HEAP_NONE=0, KUI_VMU_NATIVE_HEAP_FOUND=1,
    KUI_VMU_NATIVE_HEAP_INVALID=-1, KUI_VMU_NATIVE_HEAP_AMBIGUOUS=-2
};
/* Immutable family metadata only. This grants no permission to call code,
 * patch an initializer, overwrite RAM, or retain memory across a reset.
 * A zero initializer_slot means no unique immutable pointer was present. */
struct kui_vmu_native_heap {
    uint32_t version,bytes,family,layout,image_bytes,load;
    uint32_t initializer,initializer_slot,initializer_target,allocate,release;
    uint32_t dispatch_cell,dispatch_table,sentinel,cursor_cell;
    uint32_t begin_cell,bytes_cell,begin_copy_cell;
};
enum kui_vmu_native_heap_result kui_vmu_native_heap_discover(
    const void *,size_t,uint32_t,struct kui_vmu_native_heap *);
struct kui_vmu_native_heap_call {uint32_t load,call,return_pr,entry_begin,entry_bytes;};
bool kui_vmu_native_heap_call_discover(const void *,size_t,
    const struct kui_vmu_native_heap *,struct kui_vmu_native_heap_call *);

/* The caller must exclude allocator mutation across the entire check; the
 * reader must return cached owner state coherently, without changing it.
 * Every requested address is an aligned U0/P1/P2 main-RAM word. No actual
 * game calls or writes occur in this module. */
typedef bool (*kui_vmu_native_heap_read)(void *,uint32_t,uint32_t *);
struct kui_vmu_native_heap_lease {
    uint32_t version,bytes,phase,initializations;
    struct kui_vmu_native_heap owner;
    uint32_t begin,heap_bytes,lease_bytes,header,payload,units,remaining;
};
/* initializations is an independently maintained, live initializer-entry
 * count. Exactly the first initialization is supported. A caller must keep
 * observing reinitialization; metadata cannot detect an unobserved reset.
 * These checks establish this one allocator lease, never overall lifetime,
 * DMA exclusion, stage placement, or the validity of a worker relocation. */
bool kui_vmu_native_heap_fresh(const struct kui_vmu_native_heap *,
    kui_vmu_native_heap_read,void *,uint32_t begin,uint32_t heap_bytes,
    uint32_t initializations,uint32_t lease_bytes,struct kui_vmu_native_heap_lease *);
bool kui_vmu_native_heap_preallocate(const struct kui_vmu_native_heap_lease *,
    kui_vmu_native_heap_read,void *,uint32_t initializations);
bool kui_vmu_native_heap_postallocate(struct kui_vmu_native_heap_lease *,
    kui_vmu_native_heap_read,void *,uint32_t initializations,uint32_t actual_payload);
bool kui_vmu_native_heap_lease_valid(const struct kui_vmu_native_heap_lease *,
    kui_vmu_native_heap_read,void *,uint32_t initializations);
#endif
