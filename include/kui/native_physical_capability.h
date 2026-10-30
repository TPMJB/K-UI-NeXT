/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_PHYSICAL_CAPABILITY_H
#define KUI_NATIVE_PHYSICAL_CAPABILITY_H
#include "kui/retail_image.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* A separate ordinary Standard loader. No virtual card, native VMU worker,
 * retained game-RAM lease, SDK-family admission or reader cache is promised. */
#define KUI_NATIVE_PHYSICAL_CAP_MAGIC0 UINT32_C(0x5049554b) /* KUIP */
#define KUI_NATIVE_PHYSICAL_CAP_MAGIC1 UINT32_C(0x31594850) /* PHY1 */
#define KUI_NATIVE_PHYSICAL_CAP_BYTES 32u
#define KUI_NATIVE_PHYSICAL_TRANSPORT_MASK 7u /* SCIF=0, SCI=1, IDE=2 */
#define KUI_NATIVE_PHYSICAL_CAP_WORDS \
    KUI_NATIVE_PHYSICAL_CAP_MAGIC0,KUI_NATIVE_PHYSICAL_CAP_MAGIC1,1u, \
    KUI_NATIVE_PHYSICAL_CAP_BYTES,KUI_RETAIL_READER_STANDARD,0u,0u,0u
struct kui_native_physical_capability {
    uint32_t magic0,magic1,version,bytes,reader,lease_bytes,virtual_cards,flags;
};
_Static_assert(sizeof(struct kui_native_physical_capability)==KUI_NATIVE_PHYSICAL_CAP_BYTES,
    "Physical Standard capability wire ABI");
static inline uint32_t kui_native_physical_u32(const volatile uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline bool kui_native_physical_supports_transport(unsigned transport) {
    return transport<3u && (KUI_NATIVE_PHYSICAL_TRANSPORT_MASK&(1u<<transport))!=0u;
}
static inline bool kui_native_physical_launch_valid(unsigned reader,unsigned transport) {
    return reader==KUI_RETAIL_READER_STANDARD && kui_native_physical_supports_transport(transport);
}
static inline bool kui_native_physical_descriptor_valid(const volatile void *data,size_t bytes) {
    const volatile uint8_t *p=data;
    return p && bytes>=KUI_NATIVE_PHYSICAL_CAP_BYTES &&
        kui_native_physical_u32(p)==KUI_NATIVE_PHYSICAL_CAP_MAGIC0 &&
        kui_native_physical_u32(p+4u)==KUI_NATIVE_PHYSICAL_CAP_MAGIC1 &&
        kui_native_physical_u32(p+8u)==1u &&
        kui_native_physical_u32(p+12u)==KUI_NATIVE_PHYSICAL_CAP_BYTES &&
        kui_native_physical_u32(p+16u)==KUI_RETAIL_READER_STANDARD &&
        kui_native_physical_u32(p+20u)==0u && kui_native_physical_u32(p+24u)==0u &&
        kui_native_physical_u32(p+28u)==0u;
}
static inline bool kui_native_physical_capability_valid(const volatile void *data,size_t bytes) {
    return kui_native_physical_descriptor_valid(data,bytes);
}
/* Match complete records only; literal magic words or truncated records do
 * not grant admission. Count unaligned complete duplicates as ambiguous. */
static inline int kui_native_physical_descriptor_find(const void *data,size_t bytes,size_t *offset) {
    const volatile uint8_t *p=data;size_t found=0u;bool have=false;
    if(!p || bytes<KUI_NATIVE_PHYSICAL_CAP_BYTES)return 0;
    for(size_t i=0u;i<=bytes-KUI_NATIVE_PHYSICAL_CAP_BYTES;i++) {
        if(!kui_native_physical_descriptor_valid(p+i,bytes-i))continue;
        if(have)return -1;
        have=true;found=i;
    }
    if(have && offset)*offset=found;
    return have?1:0;
}
#endif
