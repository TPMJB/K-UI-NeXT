/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_STAGE_PLACEMENT_H
#define KUI_VMU_NATIVE_STAGE_PLACEMENT_H
#include "kui/vmu_native_heap.h"

/* Admit only the reviewed first-initializer/fresh-tail-allocation footprint.
 * stage_begin/end must bound the complete still-live linked stage, including
 * BSS and its private installer stack. This does not reserve that memory from
 * later game allocations, prove an allocator family, or grant a heap lease.
 * The caller must return from the installer before normal allocation resumes.
 * All spans are compared by physical main-RAM address; the linked stage's
 * exclusive endpoints must use one consistent U0/P1/P2 alias. */
bool kui_vmu_native_stage_allocation_safe(const struct kui_vmu_native_heap *heap,
    uint32_t exec_bytes,uint32_t stage_begin,uint32_t stage_end,
    uint32_t arena_begin,uint32_t arena_bytes);
/* The bottom transaction allocates an upper placeholder, allocates the exact
 * remaining lower chunk, then releases the placeholder. Only the reviewed
 * allocator metadata is touched in the placeholder; its payload is never
 * copied or cleared. Exclude the complete retained chunk AND the adjacent
 * placeholder header from the live stage. Ordinary allocations resume only
 * after the placeholder is released and the installer returns. */
bool kui_vmu_native_stage_bottom_allocation_safe(const struct kui_vmu_native_heap *heap,
    uint32_t exec_bytes,uint32_t stage_begin,uint32_t stage_end,
    uint32_t arena_begin,uint32_t arena_bytes);
#endif
