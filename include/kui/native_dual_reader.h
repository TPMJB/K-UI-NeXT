/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_DUAL_READERS_H
#define KUI_NATIVE_DUAL_READERS_H
#ifndef KUI_NATIVE_DUAL_READERS
#define KUI_NATIVE_DUAL_READERS 0
#endif
#if KUI_NATIVE_DUAL_READERS != 0 && KUI_NATIVE_DUAL_READERS != 1
#error "KUI_NATIVE_DUAL_READERS must be exactly zero or one"
#endif
#if KUI_NATIVE_DUAL_READERS && (defined(KUI_RETAIL_CE) || KUI_RETAIL_ZSO)
#error "The dual native reader module excludes CE and compressed images"
#endif
#ifndef __ASSEMBLER__
#include "kui/retail_image.h"
#include <stdbool.h>
#include <stddef.h>
#define KUI_NATIVE_DUAL_MAGIC0 UINT32_C(0x3152444b)
#define KUI_NATIVE_DUAL_MAGIC1 UINT32_C(0x4c415544)
#define KUI_NATIVE_DUAL_CAPABILITY_BYTES 32u
#define KUI_NATIVE_DUAL_LEASE_BYTES UINT32_C(0x60000)
struct kui_native_dual_capability {
    uint32_t magic0,magic1,version,bytes,new_reader,legacy_reader,lease_bytes,flags;
};
_Static_assert(sizeof(struct kui_native_dual_capability)==KUI_NATIVE_DUAL_CAPABILITY_BYTES,
    "Dual reader capability wire ABI");
static inline uint32_t kui_native_dual_u32(const volatile uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline bool kui_native_dual_descriptor_valid(const volatile void *data,size_t bytes) {
    const volatile uint8_t *p=data;
    return p && bytes>=KUI_NATIVE_DUAL_CAPABILITY_BYTES &&
        kui_native_dual_u32(p)==KUI_NATIVE_DUAL_MAGIC0 &&
        kui_native_dual_u32(p+4u)==KUI_NATIVE_DUAL_MAGIC1 &&
        kui_native_dual_u32(p+8u)==1u &&
        kui_native_dual_u32(p+12u)==KUI_NATIVE_DUAL_CAPABILITY_BYTES &&
        kui_native_dual_u32(p+16u)==KUI_RETAIL_READER_ASYNC &&
        kui_native_dual_u32(p+20u)==KUI_RETAIL_READER_ASYNC_EAGER &&
        kui_native_dual_u32(p+24u)==KUI_NATIVE_DUAL_LEASE_BYTES &&
        kui_native_dual_u32(p+28u)==0u;
}
/* 1 is a unique complete descriptor, 0 absent, -1 ambiguous.
 * Scan each byte so an overlapping or unaligned duplicate cannot hide. */
static inline int kui_native_dual_descriptor_find(const void *data,size_t bytes,size_t *offset) {
    const volatile uint8_t *p=data;size_t found=0u;bool have=false;
    if(!p || bytes<8u)return 0;
    for(size_t i=0u;i<=bytes-8u;i++) {
        if(kui_native_dual_u32(p+i)!=KUI_NATIVE_DUAL_MAGIC0 ||
           kui_native_dual_u32(p+i+4u)!=KUI_NATIVE_DUAL_MAGIC1)continue;
        if(!kui_native_dual_descriptor_valid(p+i,bytes-i))continue;
        if(have)return -1;
        have=true;found=i;
    }
    if(have && offset)*offset=found;
    return have?1:0;
}
static inline bool kui_native_reader_uses_sdk(uint32_t reader) {
    return reader==KUI_RETAIL_READER_ASYNC;
}
static inline bool kui_native_dual_reader_valid(uint32_t reader,uint32_t transport) {
    return transport==KUI_STORAGE_SCI &&
        (kui_native_reader_uses_sdk(reader) || reader==KUI_RETAIL_READER_ASYNC_EAGER);
}
#endif
#endif
