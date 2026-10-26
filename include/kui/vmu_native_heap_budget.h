/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_HEAP_BUDGET_H
#define KUI_VMU_NATIVE_HEAP_BUDGET_H
#include "kui/vmu_native_heap.h"

enum kui_vmu_native_heap_budget_result {
    KUI_VMU_NATIVE_HEAP_BUDGET_NONE=0,
    KUI_VMU_NATIVE_HEAP_BUDGET_FOUND=1,
    KUI_VMU_NATIVE_HEAP_BUDGET_INVALID=-1
};
/* A cold, one-use plan for the reviewed remaining-RAM child-pool caller.
 * This is not ownership authority and must never reach the retained worker.
 * The caller independently proves its first initializer return, the complete
 * original SDK allocator, disjoint installation memory and owned lease. */
struct kui_vmu_native_heap_budget {
    uint32_t reserve_address,reserve_before,reserve_after;
    uint32_t child_bytes_before,child_bytes_after;
};
/* No code is called and no source/owner memory is written. A positive result
 * proves the complete bounded caller, SDK allocation dispatch, child-pool
 * initializer and coherent original arena arguments. It also proves that the
 * changed reserve has one PC-relative consumer and no immutable address-of
 * reference. The exact allocated physical lease cost (payload plus original
 * SDK header) is subtracted from both the child's SDK allocation request and
 * its own initialization size. Unrelated callers return NONE; a recognizable
 * but incomplete/changed family refuses. The live reader returns aligned
 * U0/P1/P2 main-RAM words coherently while the caller excludes mutation.
 *
 * After acquiring its independently validated lease, the caller may publish
 * exactly reserve_after at reserve_address only if it still reads precisely
 * reserve_before. No retry or second application is permitted. This budgets
 * actual memory capacity; it does not establish future child-pool demand. */
enum kui_vmu_native_heap_budget_result kui_vmu_native_heap_budget_prepare(
    const void *image,size_t image_bytes,const struct kui_vmu_native_heap *,
    const struct kui_vmu_native_heap_call *,kui_vmu_native_heap_read,void *,
    uint32_t lease_cost,struct kui_vmu_native_heap_budget *);
#endif
