/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_SCRATCH_H
#define KUI_TOY_PILOT_SCRATCH_H
#include <stdbool.h>
#include <stdint.h>
#include "kui/retail_gd.h"
#define KUI_TOY_SCRATCH_BYTES 16u
#define KUI_TOY_SCRATCH_STACK_BYTES 8192u
#define KUI_TOY_SCRATCH_GUARD_BYTES 64u
#define KUI_TOY_SCRATCH_CALLER_BYTES 36u
#define KUI_TOY_SCRATCH_ANCHOR_BYTES 16u
_Static_assert(KUI_GD_REQUEST==0 && KUI_GD_CHECK==1,
    "scratch direction follows the scalar GD ABI");

/* The installed stage proves worker_end equals the linked stack top and
 * reserves exactly 8192 stack bytes. Native IRQs inherit that stack while
 * a worker visit runs. Each admitted veneer owns only its current 16-byte
 * SP scratch: the guard, resident caller saves and bridge anchor stay out.
 * The caller supplies top only from protected INSTALLED stage control, not
 * from a guest argument. Bit0 packs the permitted direction into an aligned
 * address. */
static inline uint32_t kui_toy_pilot_scratch_capability(uint32_t function,
    uint32_t command,uint32_t param,uint32_t pr,uint32_t sp,uint32_t top) {
    if(!((function==KUI_GD_CHECK && pr==0x8c0bd374u) ||
         (function==KUI_GD_REQUEST && command==KUI_RETAIL_GD_REQ_STAT &&
          pr==0x8c0bd57eu))) return 0u;
    if((sp&3u) || sp!=param ||
       sp-(top-KUI_TOY_SCRATCH_STACK_BYTES+KUI_TOY_SCRATCH_GUARD_BYTES+
           KUI_TOY_SCRATCH_CALLER_BYTES)>
       KUI_TOY_SCRATCH_STACK_BYTES-KUI_TOY_SCRATCH_GUARD_BYTES-
           KUI_TOY_SCRATCH_CALLER_BYTES-KUI_TOY_SCRATCH_ANCHOR_BYTES-
           KUI_TOY_SCRATCH_BYTES) return 0u;
    return sp|function;
}
static inline bool kui_toy_pilot_scratch_maps(uint32_t capability,
    uint32_t address,uint32_t bytes,int writing) {
    return capability && bytes==KUI_TOY_SCRATCH_BYTES && writing==(int)(capability&1u) &&
        address==(capability&~1u);
}
#endif
