/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_TOGGLE_H
#define KUI_VMU_NATIVE_TOGGLE_H
#include <stddef.h>
#include <stdint.h>
#define KUI_VMU_NATIVE_TOGGLE_MAGIC0 UINT32_C(0x5649554b) /* KUIV */
#define KUI_VMU_NATIVE_TOGGLE_MAGIC1 UINT32_C(0x3130544d) /* MT01 */
#define KUI_VMU_NATIVE_TOGGLE_BYTES 32u
/* A matching General module can keep its reader/cache while passing every
 * physical Maple endpoint through. This record grants no storage authority. */
struct kui_vmu_native_toggle_capability {
    uint32_t magic0,magic1,version,bytes,off_mode,lease_bytes,cache_bytes,flags;
};
_Static_assert(sizeof(struct kui_vmu_native_toggle_capability)==32u,"VMU toggle capability ABI");
static inline uint32_t kui_vmu_native_toggle_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
/* 1 unique exact record, 0 absent, -1 ambiguous; offset is untouched on refusal. */
static inline int kui_vmu_native_toggle_find(const void *data,size_t bytes,size_t *offset) {
    const uint8_t *p=data;size_t found=0u;unsigned count=0u;
    if(!p || bytes<KUI_VMU_NATIVE_TOGGLE_BYTES) return 0;
    for(size_t at=0u;at<=bytes-KUI_VMU_NATIVE_TOGGLE_BYTES;at++) {
        if(kui_vmu_native_toggle_u32(p+at)!=KUI_VMU_NATIVE_TOGGLE_MAGIC0 ||
           kui_vmu_native_toggle_u32(p+at+4u)!=KUI_VMU_NATIVE_TOGGLE_MAGIC1 ||
           kui_vmu_native_toggle_u32(p+at+8u)!=1u ||
           kui_vmu_native_toggle_u32(p+at+12u)!=32u ||
           kui_vmu_native_toggle_u32(p+at+16u)!=7u ||
           kui_vmu_native_toggle_u32(p+at+20u)!=UINT32_C(0x51000) ||
           kui_vmu_native_toggle_u32(p+at+24u)!=UINT32_C(0x30000) ||
           kui_vmu_native_toggle_u32(p+at+28u)!=1u) continue;
        found=at;if(++count>1u) return -1;
    }
    if(count && offset) *offset=found;
    return count?1:0;
}
#endif
