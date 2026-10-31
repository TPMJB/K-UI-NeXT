/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_stage_placement.h"
#include "kui/vmu_native_background.h"

#define RAM_FIRST UINT32_C(0x0c010000)
#define RAM_END UINT32_C(0x0d000000)
#define EXEC_MAX UINT32_C(0x00c00000)
#define LEASE_WITH_HEADER (KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES+UINT32_C(32))

static bool alias(uint32_t address) {
    uint32_t area=address&UINT32_C(0xe0000000);
    return area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000);
}
static uint32_t physical(uint32_t address) {return address&UINT32_C(0x1fffffff);}
static bool span(uint32_t address,uint32_t bytes) {
    uint32_t begin=physical(address);
    return alias(address) && bytes && begin>=RAM_FIRST && begin<RAM_END && bytes<=RAM_END-begin;
}
static bool overlap(uint32_t begin,uint32_t end,uint32_t other,uint32_t other_end) {
    return begin<other_end && other<end;
}
static bool excludes_stage(uint32_t address,uint32_t bytes,uint32_t stage,uint32_t stage_end) {
    return span(address,bytes) && !overlap(physical(address),physical(address)+bytes,stage,stage_end);
}
bool kui_vmu_native_stage_allocation_safe(const struct kui_vmu_native_heap *heap,
    uint32_t exec_bytes,uint32_t stage_begin,uint32_t stage_end,
    uint32_t arena_begin,uint32_t arena_bytes) {
    uint32_t stage=physical(stage_begin),limit=physical(stage_end),begin=physical(arena_begin);
    if(!heap || !exec_bytes || exec_bytes>EXEC_MAX ||
       !alias(stage_begin) || !alias(stage_end) ||
       ((stage_begin^stage_end)&UINT32_C(0xe0000000)) ||
       stage<RAM_FIRST || stage>=RAM_END || limit<=stage || limit>RAM_END ||
       overlap(stage,limit,RAM_FIRST,RAM_FIRST+exec_bytes) ||
       arena_bytes<LEASE_WITH_HEADER+32u || !span(arena_begin,arena_bytes) ||
       !excludes_stage(arena_begin,64u,stage,limit) || heap->sentinel<4u ||
       !excludes_stage(heap->sentinel-4u,0x34u,stage,limit) ||
       !excludes_stage(heap->dispatch_table,8u,stage,limit)) return false;
    uint32_t end=begin+arena_bytes,header=end-LEASE_WITH_HEADER;
    return !overlap(header,end,stage,limit) &&
        !overlap(begin,end,RAM_FIRST,RAM_FIRST+exec_bytes);
}

bool kui_vmu_native_stage_bottom_allocation_safe(const struct kui_vmu_native_heap *heap,
    uint32_t exec_bytes,uint32_t stage_begin,uint32_t stage_end,
    uint32_t arena_begin,uint32_t arena_bytes) {
    uint32_t stage=physical(stage_begin),limit=physical(stage_end),begin=physical(arena_begin);
    /* A nonempty upper placeholder needs its own header and at least32 bytes
     * of payload. The original arena identity and exclusive upper end stay
     * unchanged throughout this transaction. */
    if(!heap || !exec_bytes || exec_bytes>EXEC_MAX ||
       !alias(stage_begin) || !alias(stage_end) ||
       ((stage_begin^stage_end)&UINT32_C(0xe0000000)) ||
       stage<RAM_FIRST || stage>=RAM_END || limit<=stage || limit>RAM_END ||
       overlap(stage,limit,RAM_FIRST,RAM_FIRST+exec_bytes) ||
       arena_bytes<LEASE_WITH_HEADER+64u || !span(arena_begin,arena_bytes) ||
       (arena_begin&31u) || (arena_bytes&31u) || heap->sentinel<4u ||
       !excludes_stage(arena_begin,LEASE_WITH_HEADER+32u,stage,limit) ||
       !excludes_stage(heap->sentinel-4u,0x34u,stage,limit) ||
       !excludes_stage(heap->dispatch_table,8u,stage,limit)) return false;
    return !overlap(begin,begin+arena_bytes,RAM_FIRST,RAM_FIRST+exec_bytes);
}
/* An arena lease is the whole arena and holds no allocator metadata. Admit
 * 32-byte aligned main RAM outside both the live stage and the loaded
 * executable; the lease itself names the exact arena and its size. */
bool kui_vmu_native_stage_arena_safe(uint32_t exec_bytes,uint32_t stage_begin,uint32_t stage_end,
    uint32_t arena_begin,uint32_t arena_bytes) {
    uint32_t stage=physical(stage_begin),limit=physical(stage_end),begin=physical(arena_begin);
    if(!exec_bytes || exec_bytes>EXEC_MAX ||
       !alias(stage_begin) || !alias(stage_end) ||
       ((stage_begin^stage_end)&UINT32_C(0xe0000000)) ||
       stage<RAM_FIRST || stage>=RAM_END || limit<=stage || limit>RAM_END ||
       overlap(stage,limit,RAM_FIRST,RAM_FIRST+exec_bytes) ||
       !span(arena_begin,arena_bytes) || ((arena_begin|arena_bytes)&31u)) return false;
    return !overlap(begin,begin+arena_bytes,stage,limit) &&
        !overlap(begin,begin+arena_bytes,RAM_FIRST,RAM_FIRST+exec_bytes);
}
