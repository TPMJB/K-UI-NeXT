/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_STANDALONE_CAPABILITY_H
#define KUI_NATIVE_STANDALONE_CAPABILITY_H
#include "kui/retail_image.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* A separately installed New (SDK SCI) reader with no virtual card and no
 * game-RAM reservation. The launcher uses it only for a New launch whose
 * complete combined VMU proof refused and whose executable has the reviewed
 * SDK SCI family; every other launch keeps its existing module. */
#define KUI_NATIVE_STANDALONE_CAP_MAGIC0 UINT32_C(0x3153444b) /* KDS1 */
#define KUI_NATIVE_STANDALONE_CAP_MAGIC1 UINT32_C(0x454e4f4c) /* LONE */
#define KUI_NATIVE_STANDALONE_CAP_BYTES 32u
struct kui_native_standalone_capability {
    uint32_t magic0,magic1,version,bytes,reader,lease_bytes,virtual_cards,flags;
};
_Static_assert(sizeof(struct kui_native_standalone_capability)==KUI_NATIVE_STANDALONE_CAP_BYTES,
    "Standalone reader capability wire ABI");
static inline uint32_t kui_native_standalone_u32(const volatile uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline bool kui_native_standalone_descriptor_valid(const volatile void *data,size_t bytes) {
    const volatile uint8_t *p=data;
    return p && bytes>=KUI_NATIVE_STANDALONE_CAP_BYTES &&
        kui_native_standalone_u32(p)==KUI_NATIVE_STANDALONE_CAP_MAGIC0 &&
        kui_native_standalone_u32(p+4u)==KUI_NATIVE_STANDALONE_CAP_MAGIC1 &&
        kui_native_standalone_u32(p+8u)==1u &&
        kui_native_standalone_u32(p+12u)==KUI_NATIVE_STANDALONE_CAP_BYTES &&
        kui_native_standalone_u32(p+16u)==KUI_RETAIL_READER_ASYNC &&
        kui_native_standalone_u32(p+20u)==0u && kui_native_standalone_u32(p+24u)==0u &&
        kui_native_standalone_u32(p+28u)==0u;
}
/* 1 is a unique complete descriptor, 0 absent, -1 ambiguous.
 * Scan each byte so an overlapping or unaligned duplicate cannot hide. */
static inline int kui_native_standalone_descriptor_find(const void *data,size_t bytes,size_t *offset) {
    const volatile uint8_t *p=data;size_t found=0u;bool have=false;
    if(!p || bytes<8u)return 0;
    for(size_t i=0u;i<=bytes-8u;i++) {
        if(kui_native_standalone_u32(p+i)!=KUI_NATIVE_STANDALONE_CAP_MAGIC0 ||
           kui_native_standalone_u32(p+i+4u)!=KUI_NATIVE_STANDALONE_CAP_MAGIC1)continue;
        if(!kui_native_standalone_descriptor_valid(p+i,bytes-i))continue;
        if(have)return -1;
        have=true;found=i;
    }
    if(have && offset)*offset=found;
    return have?1:0;
}
#endif
